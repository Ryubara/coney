# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests that the docs build (`mkdocs_hooks.py`) makes the function list pages from the database."""

from __future__ import annotations

from pathlib import Path
from types import SimpleNamespace

import pytest
from test_progress_docs import load_hooks, page_of
from test_research_cli import with_source_files
from test_research_functions_check import ENTRY, make_root, with_conventions

from coney_tools import research_find

# Reads the checkout's own files of these classes (python/tests/repo_reads.py).
pytestmark = pytest.mark.reads("docs_only")


def test_the_build_gets_one_page_per_system_and_the_address_page(tmp_path: Path) -> None:
    """The pages are keyed by their path below docs/, ready to be added to the site as generated files."""
    hooks = load_hooks()
    pages = hooks.research_generated_pages(make_root(tmp_path))
    assert list(pages) == ["research/combat/functions.md", "research/find/addresses.md"]
    assert "`Grab_Start`" in pages["research/combat/functions.md"]
    assert "# Where each address is described" in pages["research/find/addresses.md"]


def test_the_words_page_is_made_once_the_glossary_exists(tmp_path: Path) -> None:
    """With a glossary file the build adds the words page, naming the functions from the Ghidra listing."""
    root = make_root(tmp_path)
    (root / "research").mkdir(exist_ok=True)
    (root / "research" / "glossary.yaml").write_text(
        "- term: grab\n  page: combat/grabs.md#grab\n  functions: [0x00100200]\n", encoding="utf-8"
    )
    pages = load_hooks().research_generated_pages(root)
    assert "research/find/words.md" in pages
    assert "`Grab_Start`" in pages["research/find/words.md"]


def test_a_glossary_naming_a_missing_section_stops_the_build(tmp_path: Path) -> None:
    """A word that points nowhere is a problem the build names, as `research render --check` would."""
    root = make_root(tmp_path)
    (root / "research").mkdir(exist_ok=True)
    (root / "research" / "glossary.yaml").write_text(
        "- term: grab\n  page: combat/grabs.md#nowhere\n", encoding="utf-8"
    )
    with pytest.raises(RuntimeError, match="nowhere"):
        load_hooks().research_generated_pages(root)


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


def test_the_source_map_names_the_page_that_owns_a_file(tmp_path: Path) -> None:
    """Once a page owns a source file, its row in the build's table links the page."""
    root = make_root(tmp_path)
    with_source_files(root)
    grabs = root / "docs" / "research" / "combat" / "grabs.md"
    front = "---\nkind: topic\nsystem: combat\nowns:\n  files: [Combat/Grab.cpp]\n---\n"
    grabs.write_text(front + grabs.read_text(encoding="utf-8"), encoding="utf-8")
    page = load_hooks().fill_source_map_page("# Map\n\n<!-- source-files:generated -->\n", root)
    assert "[combat/grabs.md](combat/grabs.md)" in page


def test_a_source_map_without_its_marker_stops_the_build(tmp_path: Path) -> None:
    """The page would show no files, so the build says what is missing."""
    root = make_root(tmp_path)
    with_source_files(root)
    with pytest.raises(RuntimeError, match="exactly one"):
        load_hooks().fill_source_map_page("# Map\n", root)


def test_the_hook_fills_the_page_the_tools_name_as_the_source_map(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    """The page is `research_find.SOURCE_MAP_PAGE`, not a second copy of its path in the hook."""
    root = make_root(tmp_path)
    with_source_files(root)
    monkeypatch.setattr(research_find, "SOURCE_MAP_PAGE", "research/elsewhere.md")
    config = SimpleNamespace(config_file_path=str(root / "mkdocs.yml"), get=lambda *_: None)
    marker = "# Map\n\n<!-- source-files:generated -->\n"
    hooks = load_hooks()
    assert "#### Combat" in hooks.on_page_markdown(marker, config, page=page_of("research/elsewhere.md"))
    assert hooks.on_page_markdown(marker, config, page=page_of("research/source-map.md")) == marker
