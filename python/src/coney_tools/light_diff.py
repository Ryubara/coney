# SPDX-License-Identifier: GPL-3.0-or-later
"""Compares two light-selection files: the original's (`coney-tools trace lights`) and a frame of Coney's lighting lab
(*Dump this frame*, `lighting-<n>-selections.csv`), written in the same columns (docs/guides/realtime-lighting.md
#test-selection).

Each of the original's rows (one call of `LightManager_SelectLights`, 0x0017de10) is matched to the Coney row of the
same caller whose sphere centre is nearest, within 5 cm; a row with none is counted as unmatched. A trace spans several
frames, so several of its rows may match one Coney row. Each matched pair's lights are compared slot by slot: the same
RenderWare type, the position and radius within 1 cm and each colour component within 1/255. `trace diff` runs this
when both of its files are selections files, and exits 1 on any difference.
"""

from __future__ import annotations

import csv
import math
from collections.abc import Sequence
from dataclasses import dataclass, field
from pathlib import Path

from coney_tools.light_trace import CALLERS, Light, number, selection_columns

#: How far apart two spheres' centres may be and still be one atomic, metres.
MATCH_DISTANCE = 0.05
#: How far a light's position and radius may be apart, metres, and each of its colour components.
POSITION_TOLERANCE = 0.01
COLOUR_TOLERANCE = 1.0 / 255.0
#: The names of RenderWare's light types in the report.
KINDS = {1: "directional", 2: "ambient", 0x80: "point", 0x81: "spot", 0x82: "spot"}
_SLOTS = 8


class LightDiffError(Exception):
    """A file that is not a selections file, or cannot be read."""


@dataclass(frozen=True)
class Row:
    """One call of a selections file: the caller, the sphere's centre and the lights chosen."""

    caller: int
    centre: tuple[float, float, float]
    lights: tuple[Light, ...]


@dataclass(frozen=True)
class Difference:
    """A matched pair whose lights differ: the original's row, the first slot that differs and both lights there."""

    row: int
    caller: int
    slot: int
    original: Light | None
    coney: Light | None


@dataclass
class Result:
    """The comparison: the rows of each file, the original's rows matched, the pairs equal and each that differs."""

    original: int = 0
    coney: int = 0
    matched: int = 0
    equal: int = 0
    differences: list[Difference] = field(default_factory=list)

    @property
    def ok(self) -> bool:
        """Something was compared and no list differs."""
        return self.matched > 0 and not self.differences


def _header(path: Path) -> list[str]:
    """The first row of `path`. Raises LightDiffError when it cannot be read."""
    try:
        with path.open(encoding="utf-8", newline="") as handle:
            return next(csv.reader(handle), [])
    except OSError as error:
        raise LightDiffError(f"{path}: {error.strerror or error}") from error


def is_selections(path: Path) -> bool:
    """Whether `path` is in the selections' columns (a light trace or a lab dump), not a per-update trace."""
    return _header(path) == selection_columns()


def _light(cells: Sequence[str]) -> Light:
    """A light from its nine cells: type, radius, colour, position."""
    values = [float(cell) for cell in cells[1:]]
    return Light(
        int(cells[0]), values[0], (values[1], values[2], values[3], values[4]), (values[5], values[6], values[7])
    )


def load(path: Path) -> list[Row]:
    """The rows of the selections file `path`. Raises LightDiffError for another file or a cell that is not a number."""
    if not is_selections(path):
        raise LightDiffError(f"{path}: not a selections file (the columns of trace lights and the lab's dump)")
    rows = []
    with path.open(encoding="utf-8", newline="") as handle:
        reader = csv.reader(handle)
        next(reader)
        for line, cells in enumerate(reader, start=2):
            try:
                count = int(cells[10])
                lights = tuple(_light(cells[11 + slot * 9 : 20 + slot * 9]) for slot in range(min(count, _SLOTS)))
                rows.append(Row(int(cells[0], 16), (float(cells[2]), float(cells[3]), float(cells[4])), lights))
            except (ValueError, IndexError) as error:
                raise LightDiffError(f"{path} line {line}: not a selection ({error})") from error
    return rows


def same_light(a: Light, b: Light) -> bool:
    """Whether `a` and `b` are the same light within the tolerances."""
    return (
        a.type == b.type
        and abs(a.radius - b.radius) <= POSITION_TOLERANCE
        and all(abs(x - y) <= POSITION_TOLERANCE for x, y in zip(a.position, b.position, strict=True))
        and all(abs(x - y) <= COLOUR_TOLERANCE for x, y in zip(a.colour, b.colour, strict=True))
    )


def _match(row: Row, candidates: Sequence[Row]) -> Row | None:
    """The candidate of `row`'s caller whose centre is nearest `row`'s, within MATCH_DISTANCE, or None."""
    best, best_distance = None, MATCH_DISTANCE
    for candidate in candidates:
        if candidate.caller != row.caller:
            continue
        distance = math.dist(candidate.centre, row.centre)
        if distance <= best_distance:
            best, best_distance = candidate, distance
    return best


def compare(original: Sequence[Row], coney: Sequence[Row]) -> Result:
    """Match each of the original's rows to Coney's and compare the lights of each pair."""
    result = Result(original=len(original), coney=len(coney))
    for index, row in enumerate(original):
        partner = _match(row, coney)
        if partner is None:
            continue
        result.matched += 1
        slot = 0
        while slot < min(len(row.lights), len(partner.lights)) and same_light(row.lights[slot], partner.lights[slot]):
            slot += 1
        if slot == len(row.lights) == len(partner.lights):
            result.equal += 1
            continue
        mine = row.lights[slot] if slot < len(row.lights) else None
        theirs = partner.lights[slot] if slot < len(partner.lights) else None
        result.differences.append(Difference(index, row.caller, slot, mine, theirs))
    return result


def _describe(light: Light | None) -> str:
    """A light in a line of the report: its kind, and for a point or spot light its position and radius."""
    if light is None:
        return "none"
    kind = KINDS.get(light.type, f"type {light.type}")
    if light.type < 0x80:
        return kind
    return f"{kind} ({', '.join(map(number, light.position))}) r {number(light.radius)}"


def report(result: Result) -> str:
    """The comparison as text: the counts, then one line per matched pair that differs."""
    lines = [
        f"atomics: original {result.original}, Coney {result.coney}, matched {result.matched}; "
        f"lists equal {result.equal}, different {len(result.differences)}"
    ]
    for difference in result.differences:
        caller = CALLERS.get(difference.caller, "other")
        lines.append(
            f"  row {difference.row} ({caller}) slot {difference.slot}: "
            f"{_describe(difference.original)} against {_describe(difference.coney)}"
        )
    if result.matched == 0:
        lines.append("  no atomic matched: nothing was compared")
    return "\n".join(lines)
