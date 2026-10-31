# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for the fix tasks `workspace clean` archives (coney_tools.workspace_clean_fixes): a fix whose parent's pull
request merged, in a made-up scratch folder; nothing touches GitHub (a stand-in answers which branches merged)."""

import shutil
from collections import namedtuple
from datetime import UTC, datetime, timedelta
from pathlib import Path

import pytest
from audit_support import CONVENTIONS
from test_workspace_clean import Space

from coney_tools import pcsx2_proc, stale_processes, task_slots, workspace_clean, workspace_clean_fixes
from coney_tools.task_file import TaskFile
from coney_tools.unit_record import UnitRecord, write_record
from coney_tools.workspace_clean_fixes import ARCHIVE_DIR, DeadFix, archive, dead_fixes

Usage = namedtuple("Usage", "total used free")
REPO = Path(__file__).resolve().parents[2]


def task_file(folder: Path, name: str, kind: str = "fix", parent: str = "work") -> None:
    """A task file as `task create` writes it (only the --- block matters here)."""
    folder.mkdir(parents=True, exist_ok=True)
    lines = [f"task: {name}", "issues: [7]", f"kind: {kind}", "claimant: Ryubara", "claimed_at: '2026-10-23T09:00:00Z'"]
    if parent:
        lines.append(f"parent: {parent}")
    (folder / "TASK.md").write_text("---\n" + "\n".join(lines) + "\n---\n# Task\n", encoding="utf-8")


def unit(folder: Path, name: str, branch: str) -> None:
    """A started unit of `name` on `branch`."""
    when = datetime(2026, 10, 23, 9, 0, 0, tzinfo=UTC)
    record = UnitRecord(name, "feature", (7,), "Ryubara", branch, folder / "tree", "origin/main", "b" * 40, when)
    write_record(folder / "unit.json", record)


NOW = datetime(2026, 10, 30, 12, 0, 0, tzinfo=UTC)
RESERVE = CONVENTIONS.tasks.start.slots.reserve_minutes


def dead(
    scratch: Path, heads: workspace_clean_fixes.MergedHeads, covered: workspace_clean_fixes.Covered | None = None
) -> list[DeadFix]:
    """`dead_fixes` on the committed conventions at `NOW`; every worktree is covered unless `covered` says."""
    if covered is None:
        return dead_fixes(scratch, heads, CONVENTIONS, NOW)
    return dead_fixes(scratch, heads, CONVENTIONS, NOW, covered)


def slot(folder: Path, minutes_ago: float) -> None:
    """The reservation `task slots --admit --reserve` left `minutes_ago` minutes before `NOW`."""
    task = TaskFile(
        task=folder.name,
        issues=(7,),
        kind="fix",
        claimant="Ryubara",
        claimed_at=NOW,
        scope=(),
        terms=(),
        parent="work",
    )
    task_slots.write_reservation(folder, task, "fix", "engineer", NOW - timedelta(minutes=minutes_ago))


def merged(*branches: str) -> workspace_clean_fixes.MergedHeads:
    """A GitHub where the pull requests from `branches` merged."""
    return lambda branch: ["a" * 40] if branch in branches else []


def test_a_fix_whose_parent_branch_merged_is_dead(tmp_path: Path) -> None:
    task_file(tmp_path / "work-fix", "work-fix")
    found = dead(tmp_path, merged("work"))
    assert found == [DeadFix("work-fix", tmp_path / "work-fix", "work", "work")]


def test_the_branch_comes_from_the_parent_s_unit_record(tmp_path: Path) -> None:
    task_file(tmp_path / "work-fix", "work-fix", parent="work-task")
    unit(tmp_path / "work-task", "work-task", branch="the-branch")
    assert dead(tmp_path, merged("work-task")) == []
    assert [fix.branch for fix in dead(tmp_path, merged("the-branch"))] == ["the-branch"]


def test_a_fix_with_a_unit_record_of_its_own_is_dead_too(tmp_path: Path) -> None:
    task_file(tmp_path / "work-fix", "work-fix")
    unit(tmp_path / "work-fix", "work-fix", branch="work")
    assert [fix.task for fix in dead(tmp_path, merged("work"))] == ["work-fix"]


def test_a_fix_whose_parent_has_no_merged_pull_request_or_unknown_to_github_stays(tmp_path: Path) -> None:
    task_file(tmp_path / "work-fix", "work-fix")
    assert dead(tmp_path, merged("other")) == []
    assert dead(tmp_path, lambda branch: None) == []


def test_other_folders_are_not_fixes(tmp_path: Path) -> None:
    task_file(tmp_path / "work", "work", kind="feature", parent="")
    (tmp_path / "scratch-notes").mkdir()
    (tmp_path / "plain-file.txt").write_text("x", encoding="utf-8")
    (tmp_path / "damaged").mkdir()
    (tmp_path / "damaged" / "TASK.md").write_text("---\n: [\n---\n", encoding="utf-8")
    assert dead(tmp_path, merged("work", "damaged", "scratch-notes")) == []


def test_a_folder_already_archived_is_not_looked_at_again(tmp_path: Path) -> None:
    task_file(tmp_path / ARCHIVE_DIR / "work-fix", "work-fix")
    task_file(tmp_path / ARCHIVE_DIR, "archive")
    assert dead(tmp_path, merged("work")) == []


def test_archiving_moves_the_whole_folder_and_never_overwrites(tmp_path: Path) -> None:
    task_file(tmp_path / "work-fix", "work-fix")
    (tmp_path / "work-fix" / "stamps").mkdir()
    (tmp_path / "work-fix" / "stamps" / "0001.jsonl").write_text("{}\n", encoding="utf-8")
    first = dead(tmp_path, merged("work"))[0]
    assert archive(first, tmp_path) is None
    assert not (tmp_path / "work-fix").exists()
    assert (tmp_path / ARCHIVE_DIR / "work-fix" / "stamps" / "0001.jsonl").is_file()
    task_file(tmp_path / "work-fix", "work-fix")
    assert archive(dead(tmp_path, merged("work"))[0], tmp_path) is None
    assert (tmp_path / ARCHIVE_DIR / "work-fix-2" / "TASK.md").is_file()


def test_a_folder_that_cannot_be_moved_says_why(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    task_file(tmp_path / "work-fix", "work-fix")

    def refuse(source: str, target: str) -> None:
        raise PermissionError("in use")

    monkeypatch.setattr(shutil, "move", refuse)
    assert archive(dead(tmp_path, merged("work"))[0], tmp_path) == "in use"
    assert (tmp_path / "work-fix").is_dir()


def test_a_fix_with_a_fresh_slot_is_left_alone_and_an_old_one_is_not(tmp_path: Path) -> None:
    task_file(tmp_path / "work-fix", "work-fix")
    slot(tmp_path / "work-fix", minutes_ago=RESERVE - 1)
    assert dead(tmp_path, merged("work")) == []
    slot(tmp_path / "work-fix", minutes_ago=RESERVE + 1)
    assert [fix.task for fix in dead(tmp_path, merged("work"))] == ["work-fix"]


def test_a_fix_whose_unit_record_names_a_worktree_that_exists_is_left_alone(tmp_path: Path) -> None:
    task_file(tmp_path / "work-fix", "work-fix")
    unit(tmp_path / "work-fix", "work-fix", branch="work")
    (tmp_path / "work-fix" / "tree").mkdir()
    assert dead(tmp_path, merged("work")) == []
    (tmp_path / "work-fix" / "tree").rmdir()
    assert [fix.task for fix in dead(tmp_path, merged("work"))] == ["work-fix"]


def test_a_fix_whose_parent_worktree_holds_commits_the_merged_head_lacks_is_left_alone(tmp_path: Path) -> None:
    task_file(tmp_path / "work-fix", "work-fix")
    unit(tmp_path / "work", "work", branch="work")
    (tmp_path / "work" / "tree").mkdir()
    seen: list[tuple[Path, list[str]]] = []

    def uncovered(worktree: Path, heads: list[str]) -> bool:
        seen.append((worktree, heads))
        return False

    assert dead(tmp_path, merged("work"), uncovered) == []
    assert seen == [(tmp_path / "work" / "tree", ["a" * 40])]
    assert [fix.task for fix in dead(tmp_path, merged("work"), lambda worktree, heads: True)] == ["work-fix"]
    (tmp_path / "work" / "tree").rmdir()
    assert [fix.task for fix in dead(tmp_path, merged("work"), uncovered)] == ["work-fix"]


# --- through `workspace clean` ---------------------------------------------------------------------------------------


@pytest.fixture
def space(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> Space:
    monkeypatch.setattr(pcsx2_proc, "pid_alive", lambda pid: False)
    monkeypatch.setattr(stale_processes, "list_processes", lambda: [])
    monkeypatch.setattr(shutil, "disk_usage", lambda path: Usage(1000 * 1024**3, 0, 500 * 1024**3))
    made = Space(tmp_path)
    # The command reads the file names it archives by from the checkout's conventions.
    (made.checkout / ".github").mkdir()
    shutil.copy(REPO / ".github" / "conventions.yml", made.checkout / ".github" / "conventions.yml")
    return made


def test_a_dry_run_names_the_fix_and_moves_nothing(space: Space, capsys: pytest.CaptureFixture[str]) -> None:
    task_file(space.root / "scratch" / "work-fix", "work-fix")
    assert workspace_clean.run_clean(space.checkout, space.root, False, merged_heads=merged("work")) == 0
    assert "would archive" in capsys.readouterr().out and (space.root / "scratch" / "work-fix").is_dir()


def test_apply_archives_the_fix_and_leaves_a_live_one(space: Space, capsys: pytest.CaptureFixture[str]) -> None:
    scratch = space.root / "scratch"
    task_file(scratch / "work-fix", "work-fix")
    task_file(scratch / "other-fix", "other-fix", parent="other")
    assert workspace_clean.run_clean(space.checkout, space.root, True, merged_heads=merged("work")) == 0
    out = capsys.readouterr().out
    assert "archive" in out and "work-fix" in out and "1 fix task(s) archived" in out
    assert not (scratch / "work-fix").exists() and (scratch / ARCHIVE_DIR / "work-fix" / "TASK.md").is_file()
    assert (scratch / "other-fix" / "TASK.md").is_file()
