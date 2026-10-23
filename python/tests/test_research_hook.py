# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests that the docs build (`mkdocs_hooks.py`) makes the function list pages from the database."""

from __future__ import annotations

from pathlib import Path

import pytest
from test_progress_docs import load_hooks
from test_research_cli import with_source_files
from test_research_functions_check import ENTRY, make_root, with_conventions


def test_the_build_gets_one_page_per_system(tmp_path: Path) -> None:
    """The pages are keyed by their path below docs/, ready to be added to the site as generated files."""
    hooks = load_hooks()
    pages = hooks.research_generated_pages(make_root(tmp_path))
    assert list(pages) == ["research/combat/functions.md"]
    assert "`Grab_Start`" in pages["research/combat/functions.md"]


def test_a_checkout_without_a_database_adds_no_pages(tmp_path: Path) -> None:
    """Nothing is generated until a system file exists."""
    assert load_hooks().research_generated_pages(with_conventions(tmp_path)) == {}


def test_a_database_with_problems_stops_the_build(tmp_path: Path) -> None:
    """The build fails with the problems named, as `research render --check` would."""
    root = make_root(tmp_path, ENTRY.replace("#grab", "#nowhere"))
    with pytest.raises(RuntimeError, match="nowhere"):
        load_hooks().research_generated_pages(root)


def test_the_source_map_gets_its_file_tables_at_the_marker(tmp_path: Path) -> None:
    """The build fills the source map page's marker with the tables of research/source-files.toml."""
    root = make_root(tmp_path)
    with_source_files(root)
    page = load_hooks().fill_source_map_page("# Map\n\n<!-- source-files:generated -->\n", root)
    assert "#### Combat" in page
    assert "| `Grab.cpp` | `0x00100000`-`0x00100400` | 2 |  |  |" in page


def test_a_source_map_without_its_marker_stops_the_build(tmp_path: Path) -> None:
    """The page would show no files, so the build says what is missing."""
    root = make_root(tmp_path)
    with_source_files(root)
    with pytest.raises(RuntimeError, match="exactly one"):
        load_hooks().fill_source_map_page("# Map\n", root)
