# SPDX-License-Identifier: GPL-3.0-or-later
"""The checks on the function database that need the checkout around it.

research_functions.py holds the schema, which a file satisfies on its own. These checks compare the entries with the
rest of the repository, so a pointer cannot rot:

* the entry's page is a file below docs/research (or, for a script binding's wrapper, the generated reference page
  under docs/references/bindings) and its section is an anchor of that page;
* the address is a function the shared Ghidra project lists (docs/progress/ghidra-functions.tsv) and the name is the
  one Ghidra has: the fix for a difference is the page or a refreshed listing, never a second name; a ported entry
  (`coney`) at an address in a gap between functions is a warning, since the port is already tagged there; inside a
  listed function it is an error;
* the source file that `coney` names exists, and the page a gap's question names has that section;
* no committed page sits where the docs build writes a system's generated list;
* the entry's page owns the address (its front matter's `owns`, the most specific owner winning), once the page
  says what it owns: `docs check` runs the same check on the page (`research_owners.check_function_entries`, one of
  research_check's `EXTRA_RULES`).

`coney-tools research render --check` runs them; they are errors (exit 1) except a name Ghidra has not got yet, and an
address it has no function at for an entry that is ported (`coney`).

Research: docs/guides/research-function-database.md
"""

from __future__ import annotations

import bisect
import posixpath
from collections.abc import Iterable
from dataclasses import dataclass, field
from pathlib import Path

from coney_tools import progress_research, source_files
from coney_tools.config import ConfigError
from coney_tools.conventions import load_conventions
from coney_tools.docs_page import read_page
from coney_tools.progress import ProgressError
from coney_tools.research_functions import CONEY, QUESTION, Entry, ResearchFunctionsError, SystemFunctions, load_all
from coney_tools.research_owners import Owners
from coney_tools.research_pages import Corpus

RESEARCH_DIR = Path("docs/research")


@dataclass
class Findings:
    """What the checks found: errors fail the check, warnings are printed."""

    errors: list[str] = field(default_factory=list)
    warnings: list[str] = field(default_factory=list)


def generated_page(system: str) -> str:
    """Where the docs build writes a system's function list, below docs/research."""
    return f"{system}/functions.md"


class Pages:
    """The anchors of the pages the entries point at, each page read once."""

    def __init__(self, root: Path) -> None:
        self._root = root
        self._anchors: dict[str, set[str] | None] = {}

    def anchors(self, page: str) -> set[str] | None:
        """The anchors of `page` (below docs/research, or `../references/bindings/<group>.md` for a binding's
        reference page), or None when it is not a file."""
        if page not in self._anchors:
            path = self._root / posixpath.normpath(posixpath.join(RESEARCH_DIR.as_posix(), page))
            try:
                self._anchors[page] = read_page(path.read_text(encoding="utf-8")).anchors if path.is_file() else None
            except (OSError, UnicodeDecodeError):
                self._anchors[page] = None
        return self._anchors[page]

    def problem(self, target: str) -> str:
        """Why `page.md#section` does not resolve, or an empty string when it does."""
        page, _, section = target.partition("#")
        known = self.anchors(page)
        shown = posixpath.normpath(posixpath.join(RESEARCH_DIR.as_posix(), page))
        if known is None:
            return f"the page {shown} does not exist"
        if section and section not in known:
            return f"the page {shown} has no section {section!r}"
        return ""


def entries_by_page(systems: Iterable[SystemFunctions]) -> dict[str, tuple[Entry, ...]]:
    """The entries of `systems` by the page each points at (`docs/research/...`), for `ResearchEnv.functions`."""
    found: dict[str, list[Entry]] = {}
    for system in systems:
        for entry in system.entries:
            found.setdefault(entry.doc_file, []).append(entry)
    return {page: tuple(entries) for page, entries in found.items()}


def _container(
    starts: list[int], listed: dict[int, progress_research.ListedFunction], address: int
) -> progress_research.ListedFunction | None:
    """The listed function whose body holds `address` (past its start), or None when it is in a gap."""
    index = bisect.bisect_right(starts, address) - 1
    if index < 0:
        return None
    function = listed[starts[index]]
    return function if function.address < address < function.address + function.size else None


def check(
    root: Path,
    systems: list[SystemFunctions],
    listing: list[progress_research.ListedFunction] | None,
    *,
    listing_file: str = progress_research.LISTING_FILE.as_posix(),
    owners: Owners | None = None,
) -> Findings:
    """Check `systems` against the checkout at `root`, the Ghidra `listing` (None when there is none, read from
    `listing_file`) and the pages' `owners` (none given: no ownership check)."""
    findings = Findings()
    pages = Pages(root)
    listed = {f.address: f for f in listing} if listing is not None else None
    starts = sorted(listed) if listed is not None else []
    for system in systems:
        where = system.path.as_posix()
        if (root / RESEARCH_DIR / generated_page(system.system)).exists():
            findings.errors.append(
                f"{where}: {(RESEARCH_DIR / generated_page(system.system)).as_posix()} is committed, but that page is "
                "generated by the docs build from this file; delete it"
            )
        for entry in system.entries:
            label = f"{where}: {entry.address:#010x} {entry.name}"
            if (problem := pages.problem(entry.page)) or (owners is not None and (problem := owners.problem(entry))):
                findings.errors.append(f"{label}: {problem}")
            for gap in entry.gaps:
                if gap.question and QUESTION.match(gap.question) and (problem := pages.problem(gap.question)):
                    findings.errors.append(f"{label}: gap question {gap.question}: {problem}")
            match = CONEY.match(entry.coney) if entry.coney else None
            if match and not (root / match["file"]).is_file():
                findings.errors.append(f"{label}: coney names {match['file']}, which is not a file")
            if listed is None:
                continue
            ghidra = listed.get(entry.address)
            if ghidra is None:
                container = _container(starts, listed, entry.address)
                message = (
                    f"{label}: not a function entry in {listing_file} "
                    "(a wrong address, or the listing needs `coney-tools progress ghidra`)"
                )
                if container is not None:
                    # Inside a function Ghidra lists, so not the start of another: the address is wrong.
                    findings.errors.append(
                        f"{label}: inside {container.name or 'a function'} at "
                        f"+{entry.address - container.address:#x} ({container.address:#010x}), a wrong address; "
                        "a function's entry is where the original's code for it starts"
                    )
                elif entry.coney:
                    # A port is tagged with the address it was read at, so the original has a function there that
                    # the shared Ghidra project may not define yet; any other entry Ghidra lacks is a typo.
                    findings.warnings.append(message + "; the port counts, with no size, until Ghidra defines it")
                else:
                    findings.errors.append(message)
            elif not ghidra.named:
                findings.warnings.append(
                    f"{label}: Ghidra's listing still calls it {ghidra.name or f'FUN_{entry.address:08x}'}; "
                    "rename it in Ghidra and refresh the listing"
                )
            elif ghidra.name != entry.name:
                findings.errors.append(
                    f"{label}: Ghidra calls it {ghidra.name}; change the entry, or refresh the listing if Ghidra "
                    "was renamed since"
                )
    return findings


def functions_dir(root: Path) -> Path:
    """The function database's folder in the checkout at `root`, from its research conventions (`files.functions`)."""
    return Path(load_conventions(root).research.files.functions)


def check_root(root: Path) -> Findings:
    """Load the function database of the checkout at `root` and check it; a malformed file is an error."""
    try:
        systems = load_all(root, functions_dir(root))
    except (ResearchFunctionsError, ConfigError) as error:
        return Findings(errors=str(error).splitlines())
    return check_systems(root, systems)


def check_systems(root: Path, systems: list[SystemFunctions]) -> Findings:
    """Check `systems` (the database as it stands, or as an import would leave it) against the checkout at `root`,
    with the paths of its research conventions."""
    try:
        research = load_conventions(root).research
        listing = progress_research.load_listing(root / research.files.ghidra_functions)
        ranges = source_files.load_ranges(root, Path(research.files.source_files))
    except (ProgressError, ConfigError) as error:
        return Findings(errors=str(error).splitlines())
    owners = Owners(Corpus.from_root(root, research), ranges)
    return check(root, systems, listing, listing_file=research.files.ghidra_functions, owners=owners)
