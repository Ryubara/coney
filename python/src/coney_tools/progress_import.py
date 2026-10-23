# SPDX-License-Identifier: GPL-3.0-or-later
"""The one-off move of `docs/progress/functions.toml` into the function database (issue #778).

The progress tracker used to count the functions Coney reimplements from a list of its own,
`docs/progress/functions.toml` (address, name, subsystem, size). The function database
(`research/functions/<system>.toml`) now says what a function is, what the original does and where Coney ports it, so
the list is retired and the tracker reads the entries that have a `coney` field. This module does the move, and stays
for a branch that still adds rows to the old file:

* `read_legacy` reads the old file;
* `plan` turns its rows into database entries: the entry of a function the database already lists gets `coney`; any
  other function gets a new entry, taken from the research that cites it (see `_ranked`) and put in the file of the
  system its page belongs to (`system_of`);
* `legacy_functions` gives the history (progress_history.py) the reimplemented list of a day before the move.

A field of the old file maps like this: `address` is the entry's address; `name` becomes Ghidra's name for the address
(the entry is checked against the listing, `research render --check`); `subsystem` is worked out from the address
(totals.toml) and checked no more; `size` is the Ghidra listing's size of the function, so the tracker measures the
reimplemented code in the same unit as the Understood measure. The `coney` field comes from the `@orig` tag in `src/`,
and `status` is `legacy`: the port predates the side-by-side comparison.

Research: docs/guides/research-function-database.md#ported
"""

from __future__ import annotations

import re
import tomllib
from collections import Counter
from collections.abc import Iterable, Mapping
from dataclasses import dataclass, field, replace
from pathlib import Path

from coney_tools import docs_page, progress, research_functions, research_import
from coney_tools.orig_index import IndexRow
from coney_tools.progress import Function, ProgressError
from coney_tools.progress_research import ListedFunction

#: The page that describes the script bindings, where a binding's wrapper points when no page cites it.
BINDINGS_PAGE = "scripting.md#bindings"
#: The note on an entry whose page cites the function but states no evidence level.
UNGRADED_NOTE = "no evidence level was stated when the entry was made from the progress list (issue 778)"
_ADDRESS = re.compile(r"\b0x([0-9a-fA-F]{8})\b")
_BINDING_WRAPPER = re.compile(r"^\s*wrapper:\s*0x([0-9a-fA-F]{8})\b", re.MULTILINE)
_BINDING_LEVEL = re.compile(r"^\s*evidence:\s*(\S+)", re.MULTILINE)
#: A choice between the sources of an entry, best first: a table row of a page, a paragraph that states a level, a
#: script binding's evidence, a page that only mentions the address.
_RANK = {"table": 0, "prose": 1, "binding": 2, "cited": 3}


@dataclass(frozen=True)
class LegacyRow:
    """One `[[function]]` of the retired list."""

    address: int
    name: str
    subsystem: str
    size: int | None


@dataclass(frozen=True)
class Candidate:
    """An entry that research could give a function, and where it came from (a key of `_RANK`)."""

    via: str
    entry: research_functions.Entry


@dataclass
class Report:
    """What the move did, for the person who reviews it."""

    rows: int = 0
    added: int = 0  # new entries
    updated: int = 0  # entries the database had, now with `coney`
    unchanged: int = 0  # entries that already had it
    sources: Counter[str] = field(default_factory=Counter)  # new entries by where their page and level came from
    systems: Counter[str] = field(default_factory=Counter)  # new entries by system
    renamed: list[str] = field(default_factory=list)  # rows whose name is not Ghidra's
    unlisted: list[str] = field(default_factory=list)  # rows the Ghidra listing has no function entry for
    ungraded: list[str] = field(default_factory=list)  # entries made with the weakest level for want of one
    skipped: list[str] = field(default_factory=list)  # rows that got no entry, with the reason
    untagged: list[str] = field(default_factory=list)  # rows with no `@orig` tag in `src/`


@dataclass
class Plan:
    """The database after the move, and the report."""

    systems: list[research_functions.SystemFunctions]
    changed: set[str]  # the systems whose file the move writes
    report: Report


# --- the retired file ---------------------------------------------------------------------------------------------


def read_legacy(path: Path) -> list[LegacyRow]:
    """The rows of the retired list at `path`. Raises ProgressError for a missing file or a malformed row."""
    try:
        data = tomllib.loads(path.read_bytes().decode("utf-8"))
    except FileNotFoundError as error:
        raise ProgressError(f"{path}: missing") from error
    except (tomllib.TOMLDecodeError, UnicodeDecodeError, OSError) as error:
        raise ProgressError(f"{path}: {error}") from error
    rows = []
    for number, item in enumerate(data.get("function", []), start=1):
        where = f"{path}: function {number}"
        address, name, size = item.get("address"), item.get("name"), item.get("size")
        if not isinstance(address, int) or isinstance(address, bool) or address < 0:
            raise ProgressError(f"{where}: address must be a non-negative integer, got {address!r}")
        if not isinstance(name, str) or not name:
            raise ProgressError(f"{where}: name must be a non-empty string, got {name!r}")
        if size is not None and (not isinstance(size, int) or isinstance(size, bool) or size < 0):
            raise ProgressError(f"{where}: size must be a non-negative integer, got {size!r}")
        rows.append(LegacyRow(address, name, str(item.get("subsystem", "")), size))
    return rows


def legacy_functions(root: Path, listing: list[ListedFunction] | None) -> list[Function] | None:
    """The reimplemented functions of a checkout that still has the retired list, or None when it has not.

    A size is the Ghidra listing's where it lists the function (as the tracker measures now), else the one the row
    recorded, so a day before the move is measured by today's definition as far as its inputs allow.
    """
    path = root / progress.LEGACY_FUNCTIONS_FILE
    if not path.is_file():
        return None
    sizes = {f.address: f.size for f in listing or []}
    return [
        Function(row.address, row.name, row.subsystem, sizes.get(row.address, row.size)) for row in read_legacy(path)
    ]


# --- the systems --------------------------------------------------------------------------------------------------


def system_of(page: str, pages: set[str]) -> str:
    """The system a page below docs/research belongs to: its name, or the longest prefix of it that is a page too.

    `combat-grabs.md` belongs to `combat` when `combat.md` is a page, `hud-functions-lua.md` to `hud`; `file-io.md`
    stays `file-io`. The folder of a page (`formats/animation.md`) is not a system and is left out.
    """
    stem = page.partition("#")[0].rpartition("/")[2].removesuffix(".md")
    parts = stem.split("-")
    for count in range(len(parts) - 1, 0, -1):
        prefix = "-".join(parts[:count])
        if f"{prefix}.md" in pages:
            return prefix
    return stem


# --- what research says about a function --------------------------------------------------------------------------


def _table_candidates(pages: Mapping[str, str], wanted: set[int]) -> dict[int, list[Candidate]]:
    """The entries the pages' function tables give for the `wanted` addresses (research_import)."""
    found: dict[int, list[Candidate]] = {}
    for page, text in sorted(pages.items()):
        for entry in research_import.import_tables(text, page, source=page).entries:
            if entry.address in wanted:
                found.setdefault(entry.address, []).append(Candidate("table", entry))
    return found


def _prose_candidates(pages: Mapping[str, str], wanted: set[int]) -> dict[int, list[Candidate]]:
    """The entries the other mentions of the `wanted` addresses give: a paragraph, list item or row that states a level
    is graded; one that does not is a citation only (`cited`, the weakest level and a note saying so)."""
    found: dict[int, list[Candidate]] = {}
    for page, text in sorted(pages.items()):
        parsed = docs_page.read_page(text)
        anchors = docs_page.heading_ids(parsed)
        for block in parsed.blocks:
            if block.kind == "heading":
                continue
            addresses = {int(m.group(1), 16) for m in _ADDRESS.finditer(block.text)} & wanted
            if not addresses:
                continue
            anchor = anchors[block.section] if block.section >= 0 else ""
            target = f"{page}#{anchor}" if anchor else page
            graded = research_import.level_of(block.text)
            for address in sorted(addresses):
                if graded is not None and (entry := _prose_entry(address, target, graded)) is not None:
                    found.setdefault(address, []).append(Candidate("prose", entry))
                else:
                    cited = research_functions.Entry(address, "", target, "speculative", note=UNGRADED_NOTE)
                    found.setdefault(address, []).append(Candidate("cited", cited))
    return found


def _prose_entry(address: int, target: str, graded: tuple[str, str, str]) -> research_functions.Entry | None:
    """An entry for a mention that states the level `graded` (key, note, reason), or None for a `not needed` with no
    reason, which the schema refuses. The note is left out: for a paragraph it would be the whole paragraph."""
    level, _note, reason = graded
    if level == "not-needed":
        return research_functions.Entry(address, "", target, level, role=reason) if reason else None
    return research_functions.Entry(address, "", target, level)


def binding_candidates(bindings: Mapping[str, str], wanted: set[int]) -> dict[int, list[Candidate]]:
    """The entries the script binding files give for the wrappers among the `wanted` addresses.

    A wrapper is described by its binding (the reference pages are generated from them), not by a research page; its
    entry points at the page that describes the bindings, with the level the binding states.
    """
    found: dict[int, list[Candidate]] = {}
    for _name, text in sorted(bindings.items()):
        for chunk in re.split(r"\n- ", text):
            wrapper = _BINDING_WRAPPER.search(chunk)
            level = _BINDING_LEVEL.search(chunk)
            if wrapper and level and level.group(1) in research_functions.LEVELS and level.group(1) != "not-needed":
                address = int(wrapper.group(1), 16)
                if address in wanted:
                    entry = research_functions.Entry(address, "", BINDINGS_PAGE, level.group(1))
                    found.setdefault(address, []).append(Candidate("binding", entry))
    return found


def _ranked(candidates: list[Candidate], weight: Mapping[str, int]) -> list[Candidate]:
    """The candidates, best first: the best kind of source, then the page with the most reimplemented functions on it
    (their home page, where the others are probably described), then the page and section by name."""
    return sorted(candidates, key=lambda c: (_RANK[c.via], -weight.get(c.entry.page_path, 0), c.entry.page))


# --- the plan -----------------------------------------------------------------------------------------------------


def plan(
    rows: list[LegacyRow],
    *,
    listing: Mapping[int, ListedFunction],
    ports: Mapping[int, str],
    pages: Mapping[str, str],
    bindings: Mapping[str, str],
    existing: list[research_functions.SystemFunctions],
    functions_dir: Path = research_functions.FUNCTIONS_DIR,
) -> Plan:
    """The database after the rows of the retired list are moved into it.

    `listing` is the Ghidra listing by address, `ports` the `coney` value of each tagged address, `pages` the research
    pages (their text by path below docs/research, the source map and generated pages left out) and `bindings` the
    binding files' text; `existing` is the database as it stands. An entry that exists keeps everything it says and
    gains `coney` and `status` when it lacks them; a function with no entry gets one from `_ranked`.
    """
    report = Report(rows=len(rows))
    systems = {s.system: s for s in existing}
    owner = {e.address: s.system for s in existing for e in s.entries}
    changed: set[str] = set()
    page_names = set(pages)
    wanted = {row.address for row in rows if row.address not in owner or not _entry(systems, owner, row.address).coney}

    candidates = _table_candidates(pages, wanted)
    for other in (_prose_candidates(pages, wanted), binding_candidates(bindings, wanted)):
        for address, more in other.items():
            candidates.setdefault(address, []).extend(more)
    # A page's weight: how many of the functions to place have a graded source on it, so the functions of one subject
    # gather on the page that describes the subject.
    weight: Counter[str] = Counter()
    for found_here in candidates.values():
        for page in {c.entry.page_path for c in found_here if c.via != "cited"}:
            weight[page] += 1

    for row in rows:
        label = f"{row.address:#010x} {row.name}"
        ghidra = listing.get(row.address)
        name = ghidra.name if ghidra is not None and ghidra.named else row.name
        if ghidra is None:
            report.unlisted.append(label)
        elif name != row.name:
            report.renamed.append(f"{label}: Ghidra calls it {name}")
        port = ports.get(row.address, "")
        if not port:
            report.untagged.append(label)
            report.skipped.append(f"{label}: no @orig tag in src/, so there is no `coney` to record")
            continue
        if row.address in owner:
            system = owner[row.address]
            entry = _entry(systems, owner, row.address)
            if entry.coney:
                report.unchanged += 1
                continue
            systems[system] = _replace_entry(systems[system], replace(entry, coney=port, status="legacy"))
            changed.add(system)
            report.updated += 1
            continue
        found = candidates.get(row.address)
        if not found:
            report.skipped.append(f"{label}: no research page cites it, so there is no page and level for an entry")
            continue
        # The best candidate the schema accepts (a table's cell may carry what an entry may not, decompiler output).
        problems = []
        for chosen in _ranked(found, weight):
            entry = replace(chosen.entry, name=name, coney=port, status="legacy")
            if not (problem := research_import.entry_problem(entry)):
                break
            problems.append(problem)
        else:
            report.skipped.append(f"{label}: {problems[0]}")
            continue
        system = system_of(entry.page, page_names)
        file = systems.get(system) or research_functions.SystemFunctions(
            system, functions_dir / f"{system}.toml", "", ()
        )
        systems[system] = replace(file, entries=(*file.entries, entry))
        owner[row.address] = system
        changed.add(system)
        report.added += 1
        report.sources[chosen.via] += 1
        report.systems[system] += 1
        if chosen.via == "cited":
            report.ungraded.append(f"{label}: {entry.page}")
    ordered = [
        replace(s, entries=tuple(sorted(s.entries, key=lambda e: e.address))) for _name, s in sorted(systems.items())
    ]
    return Plan(ordered, changed, report)


def _entry(
    systems: Mapping[str, research_functions.SystemFunctions], owner: Mapping[int, str], address: int
) -> research_functions.Entry:
    """The database's entry for `address`, which `owner` says exists."""
    return next(e for e in systems[owner[address]].entries if e.address == address)


def _replace_entry(
    system: research_functions.SystemFunctions, entry: research_functions.Entry
) -> research_functions.SystemFunctions:
    """`system` with its entry at the address of `entry` replaced by it."""
    return replace(system, entries=tuple(entry if e.address == entry.address else e for e in system.entries))


# --- the checkout -------------------------------------------------------------------------------------------------


def read_pages(root: Path, skip: frozenset[str]) -> dict[str, str]:
    """The research pages under `root` by path below docs/research, without the pages named in `skip` and the pages
    with front matter: a page that says what it owns is checked to own each function it describes, and the move cannot
    tell whether it does, so those entries come with the page's own work.
    """
    base = root / "docs" / "research"
    pages = {}
    for path in sorted(base.rglob("*.md")) if base.is_dir() else []:
        name = path.relative_to(base).as_posix()
        text = path.read_text(encoding="utf-8", errors="replace")
        if name not in skip and not text.startswith("---"):
            pages[name] = text
    return pages


def read_bindings(root: Path) -> dict[str, str]:
    """The script binding files under `root` by file name."""
    base = root / "research" / "bindings"
    return {p.name: p.read_text(encoding="utf-8", errors="replace") for p in sorted(base.glob("*.yaml"))}


def ports_of(rows: Iterable[IndexRow]) -> dict[int, str]:
    """The `coney` value of each tagged address, from the rows of the `@orig` index.

    A function tagged in more than one place gets the tag above a function first, then one inside a function, then the
    rest, and among equals the first in path order. The value is the file and, after a colon, the function or class
    that carries the tag; a name that is not an identifier (a constant) leaves the file alone.
    """
    order = {"comment": 0, "body": 1, "other": 2}
    ports: dict[int, tuple[tuple[int, str, str], str]] = {}
    for row in rows:
        value = f"{row.path}:{row.coney}" if row.coney else row.path
        if not research_functions.CONEY.match(value) or not re.fullmatch(r"[A-Za-z_~][\w:~]*", row.coney or "_"):
            value = row.path
        key = (order[row.placement], row.path, row.coney)
        if row.address not in ports or key < ports[row.address][0]:
            ports[row.address] = (key, value)
    return {address: value for address, (_key, value) in ports.items()}


def write(plan_: Plan, root: Path) -> list[Path]:
    """Write the database files the plan changed under `root`; returns their paths."""
    written = []
    for system in plan_.systems:
        if system.system in plan_.changed:
            path = root / system.path
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(research_functions.dumps(system).encode("utf-8"))
            written.append(path)
    return written
