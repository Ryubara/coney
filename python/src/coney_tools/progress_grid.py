# SPDX-License-Identifier: GPL-3.0-or-later
"""The progress picture: one square per function of the original's own code, coloured by how far Coney has got.

The model is built from what the tracker already measures (coney_tools.progress and progress_research), so a square's
colour never invents a definition. A function is in exactly one state, the furthest it has reached:

* `audited`: the fidelity audit found Coney's version matches the original. No machine-readable audit result is on
  `main` yet, so `load_audited` returns nothing and the state is not drawn (the hook for the audit team, see there);
* `reimplemented`: recorded with `coney` in the function database, so Coney has code with an `@orig` tag for it;
* `understood`: named in Ghidra and cited with an evidence level (the tracker's "Understood" measure);
* `researched`: placed in a source directory or file by the source map (the tracker's "Researched" measure);
* `untouched`: none of the above.

Blocks are the tracker's subsystems (the original's top-level source directories) in address order, the unplaced
code last. Each function also knows its source file when the source map's per-file ranges
(research/source-files.toml) place it, and the Coney source line that carries its `@orig` tag, for the page's tooltips.
Nothing generated is committed: the docs build calls `build_grid` and writes the pictures and numbers into the site
(progress_site.py).

Research: docs/research/source-map.md (the file ranges, as research/source-files.toml),
docs/guides/research-workflow.md#understood
"""

from __future__ import annotations

import math
from collections.abc import Iterable
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any

from coney_tools import progress, progress_picture, progress_render, source_files
from coney_tools.progress import UNATTRIBUTED, Progress
from coney_tools.source_files import FileRange

#: States from furthest to least; a function shows the first one that holds.
STATES = ("audited", "reimplemented", "understood", "researched", "untouched")
#: One letter per state in the compact function table the page's script reads.
STATE_CODES = {"audited": "a", "reimplemented": "r", "understood": "u", "researched": "p", "untouched": "t"}
STATE_LABELS = {
    "audited": "Audited",
    "reimplemented": "Reimplemented",
    "understood": "Understood",
    "researched": "Researched",
    "untouched": "Untouched",
}
STATE_MEANINGS = {
    "audited": "Coney's version was checked against the original and matches.",
    "reimplemented": "Coney has code for it that cites its address with an `@orig` tag.",
    "understood": "Named in Ghidra and cited on a research page with an evidence level.",
    "researched": "Placed in a source directory or file by the source map; not yet understood.",
    "untouched": "Not placed and not understood.",
}
#: Fill of each state, light and dark, validated with the dataviz skill's palette checker. The docs site repeats these
#: in docs/stylesheets/progress-grid.css as custom properties (a test holds them equal); this table is for the
#: standalone files.
PALETTE = {
    "audited": ("#8250df", "#a371f7"),
    "reimplemented": ("#2ea043", "#3fb950"),
    "understood": ("#3b82f6", "#58a6ff"),
    "researched": ("#b8860b", "#d29922"),
    "untouched": ("#6e7681", "#8b949e"),
}
CLASSES = {state: f"cg-s-{state}" for state in STATES}
#: Subsystems whose code the source map has not tied to a file or directory: not "researched" however it is read.
UNPLACED = frozenset({UNATTRIBUTED, "link-once"})
#: Bytes one square stands for in the picture by size (a function gets at least one square).
BYTES_PER_SQUARE = 512


# --- the audit hook ---------------------------------------------------------------------------------------------------


def load_audited(root: Path) -> frozenset[int]:
    """Addresses of the functions whose Coney version the fidelity audit found equal to the original.

    The hook for the audit team: `coney-tools orig index` (branch tools-orig-index) lists the `@orig` tags and the
    queue of what to check, but no audit result is stored in a machine-readable place on `main` yet. When one exists,
    read it here and return its addresses; `build_grid` then draws the `audited` state and the legend shows it. Until
    then nothing is audited, and nothing else changes.
    """
    del root
    return frozenset()


# --- the model --------------------------------------------------------------------------------------------------------


@dataclass(frozen=True)
class GridFunction:
    """One function of the original's own code and the furthest state it has reached."""

    address: int
    size: int
    name: str  # our name in Ghidra; empty while it is still Ghidra's default
    subsystem: str
    state: str
    understood: bool
    placed: bool
    source_file: str = ""  # the original's file, from the source map's tables
    coney: str = ""  # `src/x/y.cpp:42` where the `@orig` tag is, for a reimplemented function

    @property
    def reimplemented(self) -> bool:
        """True when Coney has code for it, whatever the audit found."""
        return self.state in ("audited", "reimplemented")


@dataclass(frozen=True)
class GridBlock:
    """The functions of one subsystem, in address order."""

    name: str
    functions: tuple[GridFunction, ...]

    @property
    def label(self) -> str:
        """The name as the picture shows it."""
        return "Unattributed" if self.name == UNATTRIBUTED else self.name

    def count(self, state: str) -> int:
        """Functions whose furthest state is `state`."""
        return sum(1 for f in self.functions if f.state == state)

    def measure(self, kind: str) -> tuple[int, int]:
        """(functions, bytes) that have reached `kind`: counting every state at or beyond it, as the tracker does.

        `reimplemented` includes the audited ones; `understood` includes the reimplemented ones that also meet the
        tracker's test; `researched` is the functions the source map placed.
        """
        chosen = [f for f in self.functions if _reached(f, kind)]
        return len(chosen), sum(f.size for f in chosen)

    @property
    def bytes(self) -> int:
        """Bytes of the block's functions."""
        return sum(f.size for f in self.functions)


def _reached(f: GridFunction, kind: str) -> bool:
    """Whether `f` counts toward the measure `kind`."""
    if kind == "audited":
        return f.state == "audited"
    if kind == "reimplemented":
        return f.reimplemented
    if kind == "understood":
        return f.understood
    return f.placed


MEASURES = ("audited", "reimplemented", "understood", "researched")


@dataclass
class Grid:
    """Every block of the picture and what the listing could not place."""

    blocks: list[GridBlock]
    unlisted_reimplemented: int = 0  # recorded in the database but missing from the Ghidra listing: not drawn
    audited_known: bool = False
    functions: list[GridFunction] = field(default_factory=list)

    def total(self) -> tuple[int, int]:
        """(functions, bytes) drawn."""
        return len(self.functions), sum(f.size for f in self.functions)

    def measure(self, kind: str) -> tuple[int, int]:
        """(functions, bytes) that have reached `kind` over all blocks."""
        chosen = [f for f in self.functions if _reached(f, kind)]
        return len(chosen), sum(f.size for f in chosen)


def build_grid(
    state: Progress, root: Path, audited: Iterable[int] = (), files: Iterable[FileRange] | None = None
) -> Grid | None:
    """The picture's model for the checkout at `root`, or None when it has no Ghidra listing (nothing to draw).

    `audited` is the set of addresses the fidelity audit passed (`load_audited`). `files` are the source files'
    ranges; they are read from `research/source-files.toml` under `root` when not given.
    """
    understanding = state.understanding
    if understanding is None:
        return None
    audit = frozenset(audited)
    reimplemented = {f.address for f in state.functions}
    ranges = sorted(files, key=lambda r: r.start) if files is not None else list(source_files.load_ranges(root))
    starts = [r.start for r in ranges]
    coney: dict[int, str] = {}
    for tag in progress.scan_orig_tags(root):
        coney.setdefault(tag.address, tag.where)

    by_block: dict[str, list[GridFunction]] = {}
    for status in understanding.statuses:
        f = status.function
        placed = status.subsystem not in UNPLACED
        if f.address in audit:
            level = "audited"
        elif f.address in reimplemented:
            level = "reimplemented"
        elif status.understood:
            level = "understood"
        elif placed:
            level = "researched"
        else:
            level = "untouched"
        by_block.setdefault(status.subsystem, []).append(
            GridFunction(
                f.address,
                f.size,
                f.name if f.named else "",
                status.subsystem,
                level,
                status.understood,
                placed,
                source_files.file_of(ranges, starts, f.address),
                coney.get(f.address, ""),
            )
        )
    blocks = [GridBlock(name, tuple(fns)) for name, fns in by_block.items()]
    # Address order, so the picture follows the executable; code in no subsystem range comes last.
    blocks.sort(key=lambda b: (b.name == UNATTRIBUTED, b.functions[0].address))
    drawn = {f.address for b in blocks for f in b.functions}
    return Grid(
        blocks,
        sum(1 for a in reimplemented if a not in drawn),
        bool(audit),
        [f for b in blocks for f in b.functions],
    )


# --- numbers ----------------------------------------------------------------------------------------------------------


def short_percent(part: int, whole: int) -> str:
    """A whole-number percentage that rounds down, so only a finished block shows 100%."""
    if whole <= 0:
        return "n/a"
    return f"{math.floor(100 * part / whole)}%"


def _state_summary(block: GridBlock, states: Iterable[str]) -> str:
    """`Reimplemented 12 · Understood 30` for the states the block has, in order."""
    return " · ".join(f"{STATE_LABELS[s]} {block.count(s):,}" for s in states if block.count(s))


def summary(grid: Grid) -> dict[str, Any]:
    """The raw numbers of the picture as plain data (`progress.json`): the same measures the page's tables show."""
    functions, size = grid.total()

    measures = {}
    for kind in MEASURES:
        n, b = grid.measure(kind)
        measures[kind] = {
            "functions": n,
            "bytes": b,
            "percent": round(100.0 * n / functions, 3) if functions else 0.0,
            "percent_bytes": round(100.0 * b / size, 3) if size else 0.0,
        }
    return {
        "schema": 1,
        "functions": functions,
        "bytes": size,
        "states": {s: sum(1 for f in grid.functions if f.state == s) for s in STATES},
        "measures": measures,
        "audited_known": grid.audited_known,
        "unlisted_reimplemented": grid.unlisted_reimplemented,
        "blocks": [
            {
                "name": b.name,
                "functions": len(b.functions),
                "bytes": b.bytes,
                "states": {s: b.count(s) for s in STATES},
                "measures": {
                    kind: {
                        "functions": b.measure(kind)[0],
                        "bytes": b.measure(kind)[1],
                        "percent": round(100.0 * b.measure(kind)[0] / len(b.functions), 3),
                    }
                    for kind in MEASURES
                },
            }
            for b in grid.blocks
        ],
    }


def functions_table(grid: Grid) -> dict[str, Any]:
    """The compact per-function table the page's script reads for tooltips (`functions.json`).

    Per block, parallel arrays: `a` addresses (the first, then the difference to the previous one), `z` sizes, `s` a
    string of state letters, `n` our names ("" while Ghidra's default), `f` indexes into `files` (-1 for none) and `c`
    a sparse map of function index to an index into `coney` (the source line carrying its `@orig` tag).
    """
    files: dict[str, int] = {}
    coney: dict[str, int] = {}
    blocks = []
    for b in grid.blocks:
        addresses: list[int] = []
        previous = 0
        for f in b.functions:
            addresses.append(f.address - previous)
            previous = f.address
        tags = {}
        for i, f in enumerate(b.functions):
            if f.coney:
                tags[str(i)] = coney.setdefault(f.coney, len(coney))
        blocks.append(
            {
                "name": b.label,
                "a": addresses,
                "z": [f.size for f in b.functions],
                "s": "".join(STATE_CODES[f.state] for f in b.functions),
                "n": [f.name for f in b.functions],
                "f": [files.setdefault(f.source_file, len(files)) if f.source_file else -1 for f in b.functions],
                "c": tags,
            }
        )
    return {
        "schema": 1,
        "unit": BYTES_PER_SQUARE,
        "states": {code: STATE_LABELS[state] for state, code in STATE_CODES.items()},
        "files": list(files),
        "coney": list(coney),
        "blocks": blocks,
    }


# --- the drawings -----------------------------------------------------------------------------------------------------


def square_count(size: int) -> int:
    """Squares a function takes in the picture by size: one per started `BYTES_PER_SQUARE`, at least one."""
    return max(1, math.ceil(size / BYTES_PER_SQUARE))


def picture_blocks(grid: Grid, by_size: bool) -> list[progress_picture.Block]:
    """Blocks to draw: a square per function, or (`by_size`) one per `BYTES_PER_SQUARE` bytes (at least one)."""
    blocks = []
    for b in grid.blocks:
        done, _ = b.measure("reimplemented")
        n = len(b.functions)
        lines = [f"{b.label}: {done:,}/{n:,} reimplemented ({progress_render.percent(done, n)})"]
        lines.append(_state_summary(b, STATES))
        if by_size:
            cells = [f.state for f in b.functions for _ in range(square_count(f.size))]
            lines.append(f"{b.bytes:,} bytes; one square is up to {BYTES_PER_SQUARE} bytes")
        else:
            cells = [f.state for f in b.functions]
        blocks.append(
            progress_picture.Block(
                f"{b.label} {short_percent(done, n)}", "\n".join(line for line in lines if line), cells
            )
        )
    return blocks


def render_functions_svg(grid: Grid, by_size: bool, *, uid: str, standalone: bool = False) -> str:
    """The picture of every function, or of every `BYTES_PER_SQUARE` bytes (`by_size`), as SVG."""
    count, size = grid.total()
    if by_size:
        what = f"one square per {BYTES_PER_SQUARE} bytes of the original's own code ({size:,} bytes)"
    else:
        what = f"one square per function of the original's own code ({count:,} functions)"
    done, _ = grid.measure("reimplemented")
    return progress_picture.render_svg(
        picture_blocks(grid, by_size),
        uid=uid,
        title="Coney's progress, " + what,
        description=f"{what}, grouped by source directory in address order and coloured by state. "
        f"{done:,} of {count:,} functions are reimplemented.",
        classes=CLASSES,
        palette=PALETTE,
        standalone=standalone,
        data_view="size" if by_size else "functions",
    )
