# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for the Changelog page that `mkdocs_hooks.py` writes from the git history: the pure rendering function, on
synthetic commits. No git repository or MkDocs build is needed."""

from __future__ import annotations

import importlib.util
from datetime import UTC, datetime
from pathlib import Path
from types import ModuleType

import pytest

HOOKS_PATH = Path(__file__).resolve().parents[2] / "mkdocs_hooks.py"


# The hooks file sits at the repository root, outside the package, so it is loaded by path.
@pytest.fixture(scope="module")
def hooks() -> ModuleType:
    spec = importlib.util.spec_from_file_location("mkdocs_hooks", HOOKS_PATH)
    assert spec is not None
    assert spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


# A Unix time for a UTC date and hour, so the tests state days, not numbers.
def at(day: str, hour: int = 12) -> int:
    return int(datetime.fromisoformat(f"{day}T{hour:02d}:00:00").replace(tzinfo=UTC).timestamp())


def test_newest_day_is_open_and_older_days_are_closed(hooks: ModuleType) -> None:
    text = hooks.render_changelog(
        [("aaaaaaa", at("2026-10-05"), "core: Add a"), ("bbbbbbb", at("2026-10-04"), "docs: Fix b")]
    )
    assert text.index('???+ note "2026-10-05"') < text.index('??? note "2026-10-04"')
    assert text.count("???+") == 1


def test_commits_group_by_utc_day_and_keep_their_order(hooks: ModuleType) -> None:
    text = hooks.render_changelog(
        [
            ("1111111", at("2026-10-05", 23), "core: Add one"),
            ("2222222", at("2026-10-05", 1), "core: Add two"),
            ("3333333", at("2026-10-04", 23), "core: Add three"),
        ]
    )
    assert text.count("note ") == 2
    assert text.index("1111111") < text.index("2222222") < text.index("3333333")
    # Bullets sit inside the block (four-space indent).
    assert r"    - \[`1111111`\] core: Add one" in text


def test_the_day_is_utc_not_local(hooks: ModuleType) -> None:
    # 23:30 UTC on the 4th is the 5th in UTC+2; the page must say the 4th.
    stamp = int(datetime(2026, 10, 4, 23, 30, tzinfo=UTC).timestamp())
    assert '"2026-10-04"' in hooks.render_changelog([("abcdef0", stamp, "core: Add x")])


def test_hash_links_to_the_commit_when_the_repo_is_known(hooks: ModuleType) -> None:
    commits = [("abc1234", at("2026-10-05"), "core: Add x")]
    assert r"\[[abc1234](https://example.test/o/r/commit/abc1234)\] core: Add x" in hooks.render_changelog(
        commits, "https://example.test/o/r/"
    )
    assert r"\[`abc1234`\] core: Add x" in hooks.render_changelog(commits)
    assert "](" not in hooks.render_changelog(commits).split("???+")[1]


def test_markdown_in_a_subject_is_escaped(hooks: ModuleType) -> None:
    text = hooks.render_changelog([("abc1234", at("2026-10-05"), "core: Fix *a* [b](c) <i> `d` _e_")])
    assert r"core: Fix \*a\* \[b\]\(c\) \<i\> \`d\` \_e\_" in text


def test_empty_history_renders_a_note(hooks: ModuleType) -> None:
    assert "No commits are available" in hooks.render_changelog([])
    assert "part of the git history" in hooks.render_changelog([], note="Only part of the git history.")
    assert 'note "' not in hooks.render_changelog([])
