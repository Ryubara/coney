# SPDX-License-Identifier: GPL-3.0-or-later
"""The research data files as a whole: what `research render --check` checks and what the docs build generates.

The data files are the function database (`research/functions/`, research_functions.py) and the source files of the
executable (`research/source-files.toml`, source_files.py). `check_root` runs the checks of both against the checkout;
`generated_pages` makes every page the docs build adds from them (the function lists), and `fill_source_map` fills the
marker of the source map page with the file tables. The command (`research_cli.py`) and the build (`mkdocs_hooks.py`)
both go through here, so they cannot disagree.

Research: docs/guides/research-function-database.md
"""

from __future__ import annotations

from pathlib import Path

from coney_tools import (
    progress,
    research_find,
    research_functions,
    research_functions_check,
    research_render,
    source_files,
)
from coney_tools.config import ConfigError
from coney_tools.conventions import load_conventions
from coney_tools.research_functions_check import Findings

#: The source map page, where the file tables go, below the checkout's root.
SOURCE_MAP = Path("docs") / research_find.SOURCE_MAP_PAGE


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


def check_root(root: Path) -> Findings:
    """The findings of every research data file of the checkout at `root`."""
    findings = research_functions_check.check_root(root)
    findings.errors.extend(check_source_files(root))
    return findings


def generated_pages(root: Path) -> dict[str, str]:
    """Every page the docs build makes from the research data, by its path below docs/ (the function lists).

    The caller has run `check_root` and found no error: the pages are made from checked data only.
    """
    systems = research_functions.load_all(root, research_functions_check.functions_dir(root))
    return research_render.render_pages(root, systems)


def fill_source_map(root: Path, markdown: str) -> str:
    """The source map page's `markdown` with the file tables of the checkout at `root` in place of its marker."""
    research = load_conventions(root).research
    return research_find.fill_source_map(markdown, source_files.load(root, Path(research.files.source_files)))


def filled_pages(root: Path) -> dict[str, str]:
    """The committed pages the build fills from the research data, by their path below docs/ (the source map), for a
    checkout that has the data and the page."""
    page = root / SOURCE_MAP
    research = load_conventions(root).research
    if not page.is_file() or not (root / research.files.source_files).is_file():
        return {}
    return {research_find.SOURCE_MAP_PAGE: fill_source_map(root, page.read_text(encoding="utf-8"))}
