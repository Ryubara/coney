# SPDX-License-Identifier: GPL-3.0-or-later
"""The research data files as a whole: what `research render --check` checks and what the docs build generates.

The data files are the function database (`research/functions/`, research_functions.py), the source files of the
executable (`research/source-files.toml`, source_files.py) and the glossary of the player's words
(`research/glossary.yaml`, research_glossary.py). `check_root` runs the checks of all three against the checkout;
`generated_pages` makes every page the docs build adds from them (the function lists and the find pages), and
`fill_source_map` fills the marker of the source map page with the file tables. The command (`research_cli.py`) and the
build (`mkdocs_hooks.py`) both go through here, so they cannot disagree.

Research: docs/guides/research-function-database.md
"""

from __future__ import annotations

from pathlib import Path

from coney_tools import (
    progress,
    progress_research,
    research_find,
    research_functions,
    research_functions_check,
    research_glossary,
    research_render,
    source_files,
)
from coney_tools.config import ConfigError
from coney_tools.conventions import load_conventions
from coney_tools.progress import ProgressError
from coney_tools.research_functions_check import Findings
from coney_tools.research_owners import Owners
from coney_tools.research_pages import Corpus

#: The source map page, where the file tables go, below the checkout's root.
SOURCE_MAP = Path("docs") / research_find.SOURCE_MAP_PAGE


def _owners(root: Path) -> Owners:
    """The owners of the addresses in the checkout at `root`: its pages' `owns` and its source files' ranges."""
    research = load_conventions(root).research
    return Owners(Corpus.from_root(root, research), source_files.load_ranges(root, Path(research.files.source_files)))


def check_source_files(root: Path) -> list[str]:
    """What is wrong with the source files data of the checkout at `root`, one line each; none when it has no data.

    The data must match totals.toml, and the source map page must have the marker where the tables go.
    """
    try:
        research = load_conventions(root).research
        path = Path(research.files.source_files)
        if not (root / path).is_file():
            return []
        data = source_files.load(root, path)
        totals = progress.load_totals(root / progress.TOTALS_FILE)
    except ConfigError as error:
        return str(error).splitlines()
    found = [f"{path.as_posix()}: {problem}" for problem in source_files.check_totals(data, totals)]
    page = root / SOURCE_MAP
    if page.is_file():
        try:
            research_find.fill_source_map(page.read_text(encoding="utf-8", errors="replace"), data)
        except ConfigError as error:
            found.append(str(error))
    return found


def check_glossary(root: Path) -> list[str]:
    """What is wrong with the glossary of the checkout at `root`, one line each; none when it has no glossary.

    Each term's page and section must exist, and each function it lists must be one the Ghidra listing has.
    """
    try:
        research = load_conventions(root).research
        glossary = research_glossary.load(root, Path(research.files.glossary))
        listing = progress_research.load_listing(root / research.files.ghidra_functions)
    except (ProgressError, ConfigError) as error:
        return str(error).splitlines()
    known = {f.address for f in listing} if listing is not None else None
    return research_glossary.check(
        glossary,
        research_functions_check.Pages(root).problem,
        None if known is None else known.__contains__,
        research.files.glossary,
    )


def check_root(root: Path) -> Findings:
    """The findings of every research data file of the checkout at `root`."""
    findings = research_functions_check.check_root(root)
    findings.errors.extend(check_source_files(root))
    findings.errors.extend(check_glossary(root))
    return findings


def generated_pages(root: Path) -> dict[str, str]:
    """Every page the docs build makes from the research data, by its path below docs/ (the function lists).

    The caller has run `check_root` and found no error: the pages are made from checked data only.
    """
    research = load_conventions(root).research
    systems = research_functions.load_all(root, research_functions_check.functions_dir(root))
    pages = research_render.render_pages(root, systems)
    listing = progress_research.load_listing(root / research.files.ghidra_functions) or []
    if listing:
        pages[research_find.ADDRESSES_PAGE] = research_find.render_addresses(listing, _owners(root))
    glossary_file = root / research.files.glossary
    if glossary_file.is_file():
        glossary = research_glossary.load(root, Path(research.files.glossary))
        names = {f.address: f.name for f in listing if f.named}
        pages[research_find.WORDS_PAGE] = research_find.render_words(glossary, names)
    return pages


def fill_source_map(root: Path, markdown: str) -> str:
    """The source map page's `markdown` with the file tables of the checkout at `root` in place of its marker."""
    research = load_conventions(root).research
    data = source_files.load(root, Path(research.files.source_files))
    ranges = {r.path: r for r in data.ranges()}
    owners = _owners(root)

    def owner_of(file: source_files.SourceFile) -> str:
        """The page that owns the whole of `file`, or '' when none does or the file has no address range."""
        known = ranges.get(file.path)
        claim = owners.file_owner(known) if known is not None else None
        return claim.page if claim else ""

    return research_find.fill_source_map(markdown, data, owner_of)


def filled_pages(root: Path) -> dict[str, str]:
    """The committed pages the build fills from the research data, by their path below docs/ (the source map), for a
    checkout that has the data and the page."""
    page = root / SOURCE_MAP
    research = load_conventions(root).research
    if not page.is_file() or not (root / research.files.source_files).is_file():
        return {}
    return {research_find.SOURCE_MAP_PAGE: fill_source_map(root, page.read_text(encoding="utf-8"))}
