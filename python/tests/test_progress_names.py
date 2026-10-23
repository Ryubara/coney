# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for the name rule of `progress check`: the `@orig` tag spells the database's (Ghidra's) name.

Tags that already differ when the branch leaves its base are a warning with a count; a tag the branch adds or renames
that differs fails the check, the way `orig check` holds only what the branch changes.
"""

from __future__ import annotations

import shutil
from pathlib import Path

import pytest
from test_progress import ONE_FUNCTION, ONE_LISTING, entry, make_checkout
from test_progress_history import commit, git

from coney_tools.cli import main

pytestmark = pytest.mark.skipif(shutil.which("git") is None, reason="needs git")

OLD_SPELLING = "/// Does it.\n/// @orig 0x00001010 Thing::Did (Thing.cpp)\nvoid doThing();\n"
FUNCTIONS = ONE_FUNCTION + entry(0x1020, "Other::Do", "src/core/other.h:other")
OTHER_TAG = "/// @orig 0x00001020 Other::Do (Other.cpp)\nvoid other();\n"
LISTING = ONE_LISTING + "0x00001020\t64\tOther::Do\t0\n"


def branch_repo(tmp_path: Path) -> Path:
    """A repository whose `main` has a tag that spells its name differently, and a branch `work` at the same commit."""
    repo = make_checkout(tmp_path / "repo", FUNCTIONS, OLD_SPELLING, LISTING)
    (repo / "src" / "core" / "other.h").write_text(OTHER_TAG, encoding="utf-8")
    git(repo, "init", "-q", "-b", "main")
    commit(repo, "2026-10-05T09:00:00")
    git(repo, "checkout", "-q", "-b", "work")
    return repo


def run(repo: Path, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]) -> tuple[int, str]:
    monkeypatch.chdir(repo)
    code = main(["progress", "check", "--base", "main"])
    return code, capsys.readouterr().out


def test_a_name_that_differed_at_the_base_is_a_warning_with_its_count(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    code, out = run(branch_repo(tmp_path), monkeypatch, capsys)
    assert code == 0
    assert "warning: src/core/thing.h:2: @orig 0x00001010 names 'Thing::Did', the database 'Thing::Do'" in out
    assert "1 @orig tag(s) spell a name differently from the database; none is new since main" in out


def test_a_tag_the_branch_adds_with_another_spelling_fails(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    repo = branch_repo(tmp_path)
    (repo / "src" / "core" / "other.h").write_text(OTHER_TAG.replace("Other::Do", "Other::Done"), encoding="utf-8")
    code, out = run(repo, monkeypatch, capsys)
    assert code == 1
    assert "problem: src/core/other.h:1: @orig 0x00001020 names 'Other::Done', the database 'Other::Do'" in out
    assert "1 new since main" in out and "problem: src/core/thing.h" not in out  # the old one stays a warning


def test_a_tag_moved_to_another_file_is_not_new(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    """The same tag in a renamed file keeps being a warning: the rule looks at the tag, not the line it is on."""
    repo = branch_repo(tmp_path)
    git(repo, "mv", "src/core/thing.h", "src/core/moved.h")
    functions = (repo / "research/functions/alpha.toml").read_text(encoding="utf-8")
    (repo / "research/functions/alpha.toml").write_text(
        functions.replace("src/core/thing.h", "src/core/moved.h"), encoding="utf-8"
    )
    code, out = run(repo, monkeypatch, capsys)
    assert code == 0 and "none is new since main" in out


def test_without_the_base_the_names_are_only_warned_about(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    """A checkout with no such base (a copy of the files) cannot say what is new: it says so and does not fail."""
    root = make_checkout(tmp_path / "plain", ONE_FUNCTION, OLD_SPELLING, ONE_LISTING)
    monkeypatch.chdir(root)
    assert main(["progress", "check", "--base", "main"]) == 0
    out = capsys.readouterr().out
    assert "cannot compare with main" in out and "not enforced" in out


def test_an_unreadable_base_fails_when_the_base_is_required(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    """CI passes --require-base: a checkout that cannot name the base would otherwise skip the rule and pass."""
    root = make_checkout(tmp_path / "plain", ONE_FUNCTION, OLD_SPELLING, ONE_LISTING)
    monkeypatch.chdir(root)
    assert main(["progress", "check", "--base", "main", "--require-base"]) == 1
    out = capsys.readouterr().out
    assert "problem: cannot compare with main" in out and "not enforced" not in out


def test_a_readable_base_passes_when_the_base_is_required(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    repo = branch_repo(tmp_path)
    monkeypatch.chdir(repo)
    assert main(["progress", "check", "--base", "main", "--require-base"]) == 0
    assert "none is new since main" in capsys.readouterr().out


def test_the_ci_workflow_requires_the_base() -> None:
    """The python workflow checks out two commits, so `progress check` must name HEAD^1 and insist on it."""
    workflow = (Path(__file__).resolve().parents[2] / ".github" / "workflows" / "python.yml").read_text(
        encoding="utf-8"
    )
    assert "progress check --base HEAD^1 --require-base" in workflow
