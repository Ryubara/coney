# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for coney_tools.audit_local: the workspace checks on a made-up workspace of git repositories."""

import shutil
from pathlib import Path

import pytest
from audit_support import CONVENTIONS, found, items
from conventions_support import conventions_with
from test_workspace_clean import Space, Usage, git, tip

from coney_tools import audit_local as al
from coney_tools.config import ConfigError
from coney_tools.conventions import Conventions


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


def scratch_file(space: Space, folder: str, name: str, size: int = 1024) -> None:
    """A file `name` of `size` bytes in the scratch folder `folder`."""
    path = space.root / "scratch" / folder / name
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(b"\0" * size)


def test_game_captures_in_the_scratch_folder_of_a_gone_branch_are_reported(space: Space) -> None:
    scratch_file(space, "old-track", "states/a.p2s", 2048)
    scratch_file(space, "old-track", "trace.trace")
    scratch_file(space, "old-track", "notes.md")
    (problem,) = found(al.finished_scratch, context(space))
    assert problem.item == "scratch/old-track"
    assert "2 game-derived file(s)" in problem.message and "no branch of that name is left" in problem.message


def test_the_scratch_folder_of_a_merged_branch_is_reported(space: Space) -> None:
    git(space.checkout, "branch", "done", "origin/main")
    scratch_file(space, "done", "dump.bin")
    (problem,) = found(al.finished_scratch, context(space))
    assert problem.item == "scratch/done" and "its branch is merged" in problem.message


def test_the_scratch_folder_of_unmerged_or_live_work_is_not_reported(space: Space) -> None:
    path = space.worktree("unmerged", commits=1)
    shutil.rmtree(path)
    git(space.checkout, "worktree", "prune")  # the branch stays, with a commit that origin/main lacks
    space.worktree("live")
    for folder in ("unmerged", "live"):
        scratch_file(space, folder, "state.p2s")
    assert found(al.finished_scratch, context(space)) == []


def test_screenshots_and_text_in_a_finished_scratch_folder_are_not_reported(space: Space) -> None:
    for name in ("shots/a.png", "shots/b.gif", "clip.mp4", "notes.md", "run.log"):
        scratch_file(space, "old-track", name)
    assert found(al.finished_scratch, context(space)) == []


def test_a_shared_scratch_folder_is_never_reported(space: Space) -> None:
    scratch_file(space, "pcsx2-states", "slot1.p2s")
    assert found(al.finished_scratch, context(space)) == []
    plain = conventions_with(("workspace.shared_scratch", ["downloads"]))
    assert items(found(al.finished_scratch, context(space, plain))) == ["scratch/pcsx2-states"]


def test_the_game_capture_types_are_the_ones_in_the_conventions(space: Space) -> None:
    scratch_file(space, "old-track", "state.p2s")
    custom = conventions_with(("workspace.game_capture_suffixes", [".dmp"]))
    assert found(al.finished_scratch, context(space, custom)) == []
    scratch_file(space, "old-track", "heap.DMP")
    assert items(found(al.finished_scratch, context(space, custom))) == ["scratch/old-track"]


def test_the_audit_changes_nothing(space: Space) -> None:
    path = space.worktree("done", build=1000)
    scratch_file(space, "old", "a.p2s")
    problems = found(al.finished_scratch, context(space))
    assert items(problems) == ["scratch/old"]
    assert path.is_dir() and (path / "build" / "dev" / "out.bin").is_file()
    assert (space.root / "scratch" / "old" / "a.p2s").is_file()
