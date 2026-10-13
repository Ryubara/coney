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
    scratch_file(space, "done", "trace/dump.bin")
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


def test_the_message_lists_the_first_paths_and_counts_the_rest(space: Space) -> None:
    for name in ("states/b.p2s", "states/a.p2s", "c.trace", "d.trace", "e.trace", "f.trace", "g.dump"):
        scratch_file(space, "old-track", name)
    scratch_file(space, "old-track", "build/CMakeFiles/CMakeDetermineCompilerABI_C.bin")
    (problem,) = found(al.finished_scratch, context(space))
    assert "7 game-derived file(s)" in problem.message
    assert "c.trace, d.trace, e.trace, f.trace, g.dump (and 2 more)" in problem.message


def test_a_message_with_few_paths_lists_them_all(space: Space) -> None:
    scratch_file(space, "old-track", "states/a.p2s")
    scratch_file(space, "old-track", "trace/memory.bin")
    (problem,) = found(al.finished_scratch, context(space))
    assert "states/a.p2s, trace/memory.bin;" in problem.message and "more)" not in problem.message


def test_build_caches_and_terminal_captures_in_a_finished_scratch_folder_are_not_reported(space: Space) -> None:
    for name in (
        "build/dev/CMakeFiles/CMakeDetermineCompilerABI_C.bin",
        "ghidra/.gradle/8.5/fileHashes.bin",
        "ghidra/.gradle/8.5/last-build.bin",
        "experiment/raw-1.bin",
    ):
        scratch_file(space, "old-track", name)
    assert found(al.finished_scratch, context(space)) == []


def test_the_capture_folders_and_skipped_folders_are_the_ones_in_the_conventions(space: Space) -> None:
    scratch_file(space, "old-track", "dumps/a.bin")
    scratch_file(space, "old-track", "states/b.bin")
    custom = conventions_with(
        ("workspace.game_capture_folders", ["dumps"]), ("workspace.scratch_skip_folders", ["states"])
    )
    (problem,) = found(al.finished_scratch, context(space, custom))
    assert "1 game-derived file(s)" in problem.message and "dumps/a.bin" in problem.message


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


def rules(
    suffixes: tuple[str, ...] = (".p2s", ".trace", ".dump"),
    folder_suffixes: tuple[str, ...] = (".bin",),
    folders: tuple[str, ...] = ("states", "pcsx2-states", "trace", "pcsx2"),
    skip: tuple[str, ...] = ("CMakeFiles", "_deps", ".gradle", "build"),
) -> al.CaptureRules:
    """Capture rules as the conventions file states them, with the given parts changed."""
    return al.CaptureRules(
        frozenset(suffixes),
        frozenset(folder_suffixes),
        frozenset(name.lower() for name in folders),
        frozenset(name.lower() for name in skip),
    )


def test_game_files_are_counted_at_any_depth_by_suffix(tmp_path: Path) -> None:
    (tmp_path / "a" / "b").mkdir(parents=True)
    (tmp_path / "top.P2S").write_bytes(b"\0" * 10)
    (tmp_path / "a" / "run.trace").write_bytes(b"\0" * 100)
    (tmp_path / "a" / "b" / "deep.dump").write_bytes(b"\0" * 1000)
    (tmp_path / "a" / "b" / "shot.png").write_bytes(b"\0")
    (tmp_path / "a" / "b" / "state.p2s.txt").write_text("not a game file\n", encoding="utf-8")
    found_files = al._game_files(tmp_path, rules())
    assert (found_files.count, found_files.size) == (3, 1110)


def test_an_empty_or_unreadable_scratch_folder_has_no_game_files(tmp_path: Path) -> None:
    assert al._game_files(tmp_path, rules()).count == 0
    assert al._game_files(tmp_path / "gone", rules()).count == 0


def test_a_cmake_probe_file_is_not_a_game_capture(tmp_path: Path) -> None:
    probe = tmp_path / "build" / "CMakeFiles" / "3.30.0" / "CMakeDetermineCompilerABI_C.bin"
    probe.parent.mkdir(parents=True)
    probe.write_bytes(b"\0" * 64)
    deeper = tmp_path / "x" / "CMakeFiles" / "CMakeDetermineCompilerABI_CXX.bin"
    deeper.parent.mkdir(parents=True)
    deeper.write_bytes(b"\0" * 64)
    assert al._game_files(tmp_path, rules()).count == 0
    # Even under a capture folder, a build tree is not walked.
    nested = tmp_path / "trace" / "CMakeFiles" / "CMakeDetermineCompilerABI_C.bin"
    nested.parent.mkdir(parents=True)
    nested.write_bytes(b"\0")
    assert al._game_files(tmp_path, rules()).count == 0


def test_gradle_caches_are_not_game_captures(tmp_path: Path) -> None:
    for name in ("fileHashes.bin", "last-build.bin"):
        cache = tmp_path / "ghidra" / ".gradle" / "8.5" / "abcdef" / name
        cache.parent.mkdir(parents=True, exist_ok=True)
        cache.write_bytes(b"\0" * 32)
    assert al._game_files(tmp_path, rules()).count == 0


def test_a_terminal_capture_is_not_a_game_capture(tmp_path: Path) -> None:
    experiment = tmp_path / "tools-experiment"
    experiment.mkdir()
    (experiment / "raw-session.bin").write_bytes(b"\0" * 32)
    assert al._game_files(tmp_path, rules()).count == 0


def test_a_bin_file_counts_under_a_states_trace_or_pcsx2_folder(tmp_path: Path) -> None:
    for folder in ("states", "pcsx2-states", "trace", "pcsx2", "other-tree/Trace"):
        path = tmp_path / folder / "deeper" / "memory.bin"
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(b"\0" * 8)
    (tmp_path / "other").mkdir()
    (tmp_path / "other" / "memory.bin").write_bytes(b"\0" * 8)
    found_files = al._game_files(tmp_path, rules())
    assert (found_files.count, found_files.size) == (5, 40)


def test_the_unconditional_types_count_anywhere_even_beside_a_bin_that_does_not(tmp_path: Path) -> None:
    for name in ("a/state.p2s", "b/run.trace", "c/heap.dump", "d/raw-1.bin"):
        (tmp_path / name).parent.mkdir(parents=True, exist_ok=True)
        (tmp_path / name).write_bytes(b"\0")
    assert al._game_files(tmp_path, rules()).count == 3


def test_the_first_paths_are_listed_relative_to_the_scratch_folder(tmp_path: Path) -> None:
    for name in ("z.p2s", "states/b.p2s", "states/a.p2s", "m.trace", "n.trace", "o.trace", "p.trace"):
        (tmp_path / name).parent.mkdir(parents=True, exist_ok=True)
        (tmp_path / name).write_bytes(b"\0")
    found_files = al._game_files(tmp_path, rules())
    assert found_files.count == 7
    assert found_files.paths == ("m.trace", "n.trace", "o.trace", "p.trace", "states/a.p2s")


def test_scratch_folders_are_read_side_by_side_with_the_same_answers(space: Space) -> None:
    (space.root / "scratch" / "track-0").mkdir(parents=True)
    for number in range(9):
        for state in range(number):
            scratch_file(space, f"track-{number}", f"states/{state}.p2s", size=7)
    space.worktree("track-3")
    folders = context(space).scratch
    assert [(folder.name, folder.game_files, folder.game_bytes, folder.state) for folder in folders] == [
        (f"track-{number}", number, number * 7, "active" if number in (0, 3) else "gone") for number in range(9)
    ]
