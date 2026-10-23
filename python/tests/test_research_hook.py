# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests that the docs build (`mkdocs_hooks.py`) makes the function list pages from the database."""

from __future__ import annotations

from pathlib import Path

import pytest
from test_progress_docs import load_hooks
from test_research_functions_check import ENTRY, make_root


def test_the_build_gets_one_page_per_system(tmp_path: Path) -> None:
    """The pages are keyed by their path below docs/, ready to be added to the site as generated files."""
    hooks = load_hooks()
    pages = hooks.research_function_pages(make_root(tmp_path))
    assert list(pages) == ["research/combat/functions.md"]
    assert "`Grab_Start`" in pages["research/combat/functions.md"]


def test_a_checkout_without_a_database_adds_no_pages(tmp_path: Path) -> None:
    """Nothing is generated until a system file exists."""
    assert load_hooks().research_function_pages(tmp_path) == {}


def test_a_database_with_problems_stops_the_build(tmp_path: Path) -> None:
    """The build fails with the problems named, as `research render --check` would."""
    root = make_root(tmp_path, ENTRY.replace("#grab", "#nowhere"))
    with pytest.raises(RuntimeError, match="nowhere"):
        load_hooks().research_function_pages(root)
