# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for the progress numbers that the docs build produces (`mkdocs_hooks.py`): the page's tables and the
README's shields.io endpoint badges. Nothing here needs MkDocs; the inputs are the made-up checkout of test_progress."""

from __future__ import annotations

import importlib.util
import json
import re
from pathlib import Path
from types import ModuleType, SimpleNamespace

import pytest
from test_progress import BLOCK, ONE_FUNCTION, ONE_TAG, make_checkout

from coney_tools import progress, progress_render

REPO = Path(__file__).resolve().parents[2]
HOOKS_PATH = REPO / "mkdocs_hooks.py"


def load_hooks() -> ModuleType:
    """mkdocs_hooks.py as a module (it sits outside any package)."""
    spec = importlib.util.spec_from_file_location("mkdocs_hooks", HOOKS_PATH)
    assert spec is not None and spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def page_of(src_uri: str) -> SimpleNamespace:
    """The part of a MkDocs page that the hook reads."""
    return SimpleNamespace(file=SimpleNamespace(src_uri=src_uri))


def test_the_page_is_filled_from_the_checkout(tmp_path: Path) -> None:
    """The hook loads the tracker for the checkout and puts the tables where the committed page has its marker."""
    hooks = load_hooks()
    state = hooks.load_progress(make_checkout(tmp_path, ONE_FUNCTION, ONE_TAG))
    filled = hooks.fill_progress_page(BLOCK, state)
    assert progress_render.PAGE_MARKER not in filled
    assert filled.startswith("before") and filled.endswith("after\n")
    assert "## Overall" in filled and "| `0x00001010` | `Thing::Do` | `Alpha` | 128 |" in filled


def test_only_the_progress_page_is_filled(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    """`on_page_markdown` fills the marker on progress/index.md and leaves every other page as it was."""
    hooks = load_hooks()
    state = hooks.load_progress(make_checkout(tmp_path, ONE_FUNCTION, ONE_TAG))
    monkeypatch.setattr(hooks, "_progress_state", state, raising=True)
    config = {"repo_url": None}
    filled = hooks.on_page_markdown(BLOCK, config, page=page_of("progress/index.md"))
    assert "## Overall" in filled
    other = hooks.on_page_markdown(BLOCK, config, page=page_of("roadmap.md"))
    assert other == BLOCK


def test_badge_files_are_shields_endpoints_in_the_site(tmp_path: Path) -> None:
    """Each badge is a file below progress/badges/ holding the JSON schema shields.io's endpoint badge reads."""
    hooks = load_hooks()
    state = hooks.load_progress(make_checkout(tmp_path, ONE_FUNCTION, ONE_TAG))
    files = hooks.progress_badge_files(state)
    assert sorted(files) == [
        "progress/badges/milestones.json",
        "progress/badges/reimplemented.json",
        "progress/badges/researched.json",
    ]
    for text in files.values():
        data = json.loads(text)
        assert data["schemaVersion"] == 1
        assert set(data) == {"schemaVersion", "label", "message", "color"}
    researched = json.loads(files["progress/badges/researched.json"])
    assert (researched["label"], researched["message"]) == ("researched", "33.3%")


def test_disagreeing_inputs_fail_the_build(tmp_path: Path) -> None:
    """A tag without an entry stops the docs build, naming the problem, instead of publishing wrong numbers."""
    hooks = load_hooks()
    with pytest.raises(RuntimeError, match=r"has no entry in functions\.toml"):
        hooks.load_progress(make_checkout(tmp_path, tags=ONE_TAG))
    with pytest.raises(RuntimeError, match=r"totals\.toml"):
        hooks.load_progress(tmp_path / "missing")


def test_the_repository_is_ready_for_the_build() -> None:
    """The real inputs agree, the committed page has its marker and the README's badges name files the build writes."""
    state = progress.load(REPO)
    assert state.problems == []
    page = (REPO / progress.PAGE_FILE).read_text(encoding="utf-8")
    assert page.count(progress_render.PAGE_MARKER) == 1
    assert progress_render.fill_page(page, progress_render.render_page(state), "page").count("| `0x") > 0
    readme = (REPO / "README.md").read_text(encoding="utf-8")
    written = set(progress_render.badge_files(state))
    named = set(re.findall(r"progress%2Fbadges%2F([a-z]+\.json)", readme))
    assert named and named <= written
    # No generated number is committed to the README.
    assert "progress:start" not in readme and "img.shields.io/badge/" not in readme


def test_the_hook_builds_the_pictures_for_the_progress_and_history_pages(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    """The pictures are inline on the progress page, the chart on the history page, and the data files are listed."""
    from test_progress import HISTORY_BLOCK
    from test_progress_grid import make_grid_checkout

    hooks = load_hooks()
    root = make_grid_checkout(tmp_path)
    monkeypatch.delenv("CONEY_PROGRESS_HISTORY", raising=False)
    monkeypatch.delenv("CONEY_PROGRESS_HISTORY_CACHE", raising=False)
    state = hooks.load_progress(root)
    site = hooks.build_progress_site(root, state, "https://example.invalid/r")
    assert "progress/functions.json" in site.files and "progress/progress.json" in site.files
    monkeypatch.setattr(hooks, "_progress_state", state, raising=True)
    monkeypatch.setattr(hooks, "_progress_site", site, raising=True)
    config = {"repo_url": None}
    progress_page = hooks.on_page_markdown(BLOCK, config, page=page_of("progress/index.md"))
    assert "<svg" in progress_page and "cg-s-reimplemented" in progress_page and "## Every function" in progress_page
    history_page = hooks.on_page_markdown(HISTORY_BLOCK, config, page=page_of("progress/history.md"))
    assert "cg-chart" in history_page and progress_render.HISTORY_MARKER not in history_page


def test_a_published_history_is_the_cache_of_the_next_build(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    """`CONEY_PROGRESS_HISTORY_CACHE` names the previous history.json: its earlier days stay in the new history."""
    from test_progress_grid import make_grid_checkout

    hooks = load_hooks()
    root = make_grid_checkout(tmp_path / "repo")
    state = hooks.load_progress(root)
    cache = tmp_path / "history.json"
    old = {
        "schema": 1,
        "points": [
            dict(
                date="2020-01-01",
                commit="abc",
                game_bytes=10,
                reimplemented_bytes=1,
                reimplemented_functions=1,
                researched_bytes=5,
            )
        ],
        "skipped": [],
    }
    cache.write_text(json.dumps(old), encoding="utf-8")
    monkeypatch.delenv("CONEY_PROGRESS_HISTORY", raising=False)
    monkeypatch.setenv("CONEY_PROGRESS_HISTORY_CACHE", str(cache))
    site = hooks.build_progress_site(root, state)
    dates = [p["date"] for p in json.loads(site.files["progress/history.json"])["points"]]
    assert dates[0] == "2020-01-01" and len(dates) == 2
    monkeypatch.setenv("CONEY_PROGRESS_HISTORY_CACHE", str(tmp_path / "missing.json"))
    assert len(json.loads(hooks.build_progress_site(root, state).files["progress/history.json"])["points"]) == 1
