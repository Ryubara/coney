# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for coney_tools.audit_local: the workspace checks on a made-up workspace of git repositories."""

import shutil
from dataclasses import replace
from pathlib import Path

import pytest
from audit_support import CONVENTIONS, found, items
from conventions_support import conventions_with
from test_workspace_clean import Space, Usage, git, tip

from coney_tools import audit_local as al
from coney_tools import workspace_clean
from coney_tools.config import ConfigError
from coney_tools.conventions import Conventions
from coney_tools.workspace_clean import plan


@pytest.fixture
def space(tmp_path: Path) -> Space:
    """A workspace with a checkout cloned from a made-up origin."""
    return Space(tmp_path)


def context(
    space: Space, conventions: Conventions = CONVENTIONS, heads: dict[str, list[str]] | None = None
) -> al.LocalContext:
    """The local context of the workspace; `heads` stands in for GitHub's merged pull requests."""
    known = heads or {}
    return al.build_context(space.checkout, None, conventions, lambda branch: known.get(branch))


def test_a_merged_worktree_is_reported_with_the_command_that_removes_it(space: Space) -> None:
    space.worktree("done")
    space.worktree("unmerged", commits=1)
    (problem,) = found(al.merged_worktrees, context(space))
    assert problem.item == "worktree done" and "merged into origin/main" in problem.message
    assert "coney-tools workspace clean --apply" in problem.message


def test_a_squash_merged_branch_is_reported_when_github_says_so(space: Space) -> None:
    path = space.worktree("squashed", commits=1)
    (problem,) = found(al.merged_worktrees, context(space, heads={"squashed": [tip(path)]}))
    assert problem.item == "worktree squashed" and "pull request merged" in problem.message


def test_a_busy_worktree_is_never_reported(space: Space) -> None:
    path = space.worktree("busy")
    (path / ".coney-busy").write_text("", encoding="utf-8")
    assert found(al.merged_worktrees, context(space)) == []


def test_the_busy_marker_is_the_one_in_the_conventions(space: Space) -> None:
    path = space.worktree("busy")
    (path / ".agent-busy").write_text("", encoding="utf-8")
    custom = conventions_with(("workspace.busy_marker", ".agent-busy"))
    assert found(al.merged_worktrees, context(space, custom)) == []
    (path / ".agent-busy").unlink()
    (path / ".coney-busy").write_text("", encoding="utf-8")
    assert items(found(al.merged_worktrees, context(space, custom))) == ["worktree busy"]


def test_merged_worktrees_are_not_reported_when_the_conventions_keep_them(space: Space) -> None:
    space.worktree("done")
    keep = conventions_with(("workspace.remove_worktree_when_merged", False))
    assert found(al.merged_worktrees, context(space, keep)) == []


def test_a_worktree_whose_folder_is_gone_is_reported(space: Space) -> None:
    path = space.worktree("vanished")
    shutil.rmtree(path)
    (problem,) = found(al.merged_worktrees, context(space))
    assert problem.item == "worktree vanished" and "folder is gone" in problem.message


def test_a_stray_file_among_the_worktrees_is_reported(space: Space) -> None:
    space.worktree("done", commits=1)
    (space.worktrees / "build_ci.log").write_text("log\n", encoding="utf-8")
    (space.worktrees / "old-folder").mkdir()
    problems = found(al.merged_worktrees, context(space))
    assert items(problems) == ["build_ci.log", "old-folder"]
    assert "is a file among the worktrees" in problems[0].message and "is a folder" in problems[1].message


def test_a_missing_origin_main_is_a_notice_and_judges_no_worktree(space: Space) -> None:
    space.worktree("done")
    git(space.checkout, "remote", "remove", "origin")
    local = context(space)
    assert local.entries is None and "origin/main is missing" in local.notices[0]
    assert found(al.merged_worktrees, local) == []


def test_outside_a_workspace_the_context_is_an_error(tmp_path: Path) -> None:
    with pytest.raises(ConfigError):
        al.build_context(tmp_path / "nowhere", None, CONVENTIONS)


def test_disk_space_below_the_minimum_is_reported(space: Space, monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.setattr(shutil, "disk_usage", lambda path: Usage(100 * 1024**3, 0, 29 * 1024**3))
    (problem,) = found(al.disk_space, context(space))
    assert "29.0 GB free, the minimum is 30 GB" in problem.message and "workspace clean" in problem.message
    monkeypatch.setattr(shutil, "disk_usage", lambda path: Usage(100 * 1024**3, 0, 31 * 1024**3))
    assert found(al.disk_space, context(space)) == []


def test_the_minimum_is_the_one_in_the_conventions(space: Space, monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.setattr(shutil, "disk_usage", lambda path: Usage(100 * 1024**3, 0, 31 * 1024**3))
    assert items(found(al.disk_space, context(space, conventions_with(("workspace.disk_min_free_gb", 40))))) == [
        "workspace drive"
    ]


def test_captures_in_a_scratch_folder_without_a_worktree_are_reported(space: Space) -> None:
    scratch = space.root / "scratch"
    (scratch / "old-track" / "shots").mkdir(parents=True)
    (scratch / "old-track" / "shots" / "a.png").write_bytes(b"\0" * 2048)
    (scratch / "old-track" / "notes.md").write_text("notes\n", encoding="utf-8")
    (scratch / "text-only").mkdir()
    (scratch / "text-only" / "notes.md").write_text("notes\n", encoding="utf-8")
    space.worktree("live")
    (scratch / "live").mkdir()
    (scratch / "live" / "b.gif").write_bytes(b"\0")
    (problem,) = found(al.finished_scratch, context(space))
    assert problem.item == "scratch/old-track" and "1 capture file(s)" in problem.message


def test_the_audit_changes_nothing(space: Space) -> None:
    path = space.worktree("done", build=1000)
    (space.root / "scratch" / "old").mkdir(parents=True)
    (space.root / "scratch" / "old" / "a.png").write_bytes(b"\0")
    context(space)
    assert path.is_dir() and (path / "build" / "dev" / "out.bin").is_file()
    assert (space.root / "scratch" / "old" / "a.png").is_file()


# --- speed: what the audit does not read, and what it reads side by side --------------------------------------------


def test_the_audit_reads_the_activity_only_of_worktrees_it_may_report(
    space: Space, monkeypatch: pytest.MonkeyPatch
) -> None:
    """The audit never deletes a build folder, so it needs a worktree's last activity only to tell a merged one that
    was just made from main from one to report; walking every file of 90 worktrees took half a minute."""
    space.worktree("done")
    space.worktree("unmerged-1", commits=1, build=10)
    space.worktree("unmerged-2", commits=1)
    asked: list[Path] = []
    real = workspace_clean.last_activity

    def recording(path: Path) -> float:
        asked.append(path)
        return real(path)

    monkeypatch.setattr(workspace_clean, "last_activity", recording)
    (problem,) = found(al.merged_worktrees, context(space))
    assert problem.item == "worktree done"
    assert [path.name for path in asked] == ["done"]


def test_a_clean_still_reads_every_worktrees_activity_for_its_build_folder(
    space: Space, monkeypatch: pytest.MonkeyPatch
) -> None:
    space.worktree("unmerged", commits=1, build=10)
    asked: list[Path] = []
    real = workspace_clean.last_activity

    def recording(path: Path) -> float:
        asked.append(path)
        return real(path)

    monkeypatch.setattr(workspace_clean, "last_activity", recording)
    assert space.entries()["unmerged"].delete_build
    assert [path.name for path in asked] == ["unmerged"]


def test_worktrees_are_judged_side_by_side_with_the_same_answers(space: Space) -> None:
    names = [f"w{number:02d}" for number in range(12)]
    for number, name in enumerate(names):
        space.worktree(name, commits=number % 3, build=5 if number % 4 == 0 else 0)
    serial, _ = plan(replace(space.settings(), workers=1))
    parallel, _ = plan(replace(space.settings(), workers=6))
    assert [entry.name for entry in parallel] == sorted(names)
    assert [(entry.name, entry.why, entry.remove, entry.delete_build) for entry in parallel] == [
        (entry.name, entry.why, entry.remove, entry.delete_build) for entry in serial
    ]


def test_captures_are_counted_at_any_depth_by_suffix(tmp_path: Path) -> None:
    (tmp_path / "a" / "b").mkdir(parents=True)
    (tmp_path / "top.PNG").write_bytes(b"\0" * 10)
    (tmp_path / "a" / "clip.mp4").write_bytes(b"\0" * 100)
    (tmp_path / "a" / "b" / "deep.gif").write_bytes(b"\0" * 1000)
    (tmp_path / "a" / "b" / "notes.md").write_text("not a capture\n", encoding="utf-8")
    (tmp_path / "a" / "b" / "shot.png.txt").write_text("not a capture either\n", encoding="utf-8")
    assert al._captures(tmp_path) == (3, 1110)


def test_an_empty_or_unreadable_scratch_folder_has_no_captures(tmp_path: Path) -> None:
    assert al._captures(tmp_path) == (0, 0)
    assert al._captures(tmp_path / "gone") == (0, 0)


def test_scratch_folders_are_read_side_by_side_with_the_same_answers(space: Space) -> None:
    scratch = space.root / "scratch"
    for number in range(9):
        (scratch / f"track-{number}" / "shots").mkdir(parents=True)
        for shot in range(number):
            (scratch / f"track-{number}" / "shots" / f"{shot}.png").write_bytes(b"\0" * 7)
    space.worktree("track-3")
    folders = context(space).scratch
    assert [(folder.name, folder.captures, folder.capture_bytes, folder.has_worktree) for folder in folders] == [
        (f"track-{number}", number, number * 7, number == 3) for number in range(9)
    ]
