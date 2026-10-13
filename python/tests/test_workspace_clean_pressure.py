# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for the disk-pressure mode of `workspace clean`: when the drive is under its floor, idle `build/` folders are
deleted, oldest last use first, until the floor plus a margin is free (coney_tools.workspace_clean)."""

import json
import shutil
from collections import namedtuple
from pathlib import Path

import pytest
from conventions_support import changed, write_checkout
from test_workspace_clean import BUSY_FILE, OLD, Space, git

from coney_tools import stale_processes, workspace_clean
from coney_tools.cli import main
from coney_tools.workspace_clean import run_clean

GIB = 1024**3
Usage = namedtuple("Usage", "total used free")
#: Each made-up build folder holds this many bytes and stands for a 1 GB one on the fake drive.
BUILD_BYTES = 1000


@pytest.fixture
def space(tmp_path: Path) -> Space:
    return Space(tmp_path)


@pytest.fixture(autouse=True)
def no_real_processes(monkeypatch: pytest.MonkeyPatch) -> None:
    """`workspace clean --apply` must never look at, or stop, the machine's real processes."""
    monkeypatch.setattr(stale_processes, "list_processes", lambda: [])
    monkeypatch.setattr(stale_processes, "kill_tree", lambda pid: None)


def claims(space: Space) -> Path:
    """The build queue's folder of the made-up workspace, laid out as the build queue writes it."""
    return space.root / "scratch" / "build-claims"


def claim_slot(space: Space, worktree: str) -> None:
    """A slot claim held by a running build of `worktree`."""
    held = claims(space) / "slot-1.claim"
    held.mkdir(parents=True)
    (held / "owner.json").write_text(json.dumps({"slot": 1, "worktree": worktree}), encoding="utf-8")


def queue_ticket(space: Space, worktree: str, lane: str = "queue") -> None:
    """A ticket of `worktree` waiting in a queue folder."""
    waiting = claims(space) / lane
    waiting.mkdir(parents=True, exist_ok=True)
    (waiting / f"0001-{worktree}.json").write_text(json.dumps({"worktree": worktree}), encoding="utf-8")


def drive(monkeypatch: pytest.MonkeyPatch, space: Space, free_gb: float) -> None:
    """A drive that starts with `free_gb` free and gains 1 GB for every `BUILD_BYTES` bytes deleted under the
    worktrees."""

    def total() -> int:
        return sum(workspace_clean.dir_size(tree) for tree in space.worktrees.iterdir())

    start = total()
    monkeypatch.setattr(
        shutil,
        "disk_usage",
        lambda path: Usage(1000 * GIB, 0, int(free_gb * GIB + (start - total()) / BUILD_BYTES * GIB)),
    )


@pytest.fixture(autouse=True)
def roomy_drive(monkeypatch: pytest.MonkeyPatch) -> None:
    """Unless a test says otherwise there is plenty of room: the machine's real free space decides nothing."""
    monkeypatch.setattr(shutil, "disk_usage", lambda path: Usage(1000 * GIB, 0, 500 * GIB))


def ten_builds(space: Space) -> list[Path]:
    """Ten unmerged worktrees, each last used 10, 20, ... 100 hours ago, each with a build folder of BUILD_BYTES."""
    return [space.worktree(f"w{age}", commits=1, build=BUILD_BYTES, age=age) for age in range(10, 101, 10)]


def pressured(space: Space, *, apply: bool = True, cwd: Path | None = None, margin: float = 3) -> int:
    """Run `clean` with the floor at 30 GB, a build age no build here reaches and GitHub unreachable."""
    return run_clean(
        cwd or space.checkout,
        space.root,
        apply,
        500.0,
        merged_heads=lambda branch: None,
        min_gb=30,
        margin_gb=margin,
    )


def kept(trees: list[Path]) -> list[str]:
    """The names of the worktrees whose build folder is still there."""
    return sorted(tree.name for tree in trees if (tree / "build").exists())


def test_pressure_deletes_the_oldest_builds_first_until_the_margin_is_reached(
    space: Space, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    trees = ten_builds(space)
    drive(monkeypatch, space, 25)
    assert pressured(space) == 0
    # 25 GB free, a 30 GB floor and a 3 GB margin: eight builds of 1 GB make 33 GB, so the two newest stay.
    assert kept(trees) == ["w10", "w20"]
    out = capsys.readouterr().out
    assert "Disk pressure" in out
    deleted = out.split("Disk pressure")[1]
    assert deleted.index("w100") < deleted.index("w90") < deleted.index("w30")
    assert "free 25.0 GB -> 26.0 GB" in out
    assert "free 32.0 GB -> 33.0 GB" in out
    assert "Free space: 25.0 GB -> 33.0 GB" in out


def test_pressure_deletes_only_build_folders(space: Space, monkeypatch: pytest.MonkeyPatch) -> None:
    trees = ten_builds(space)
    (trees[0] / "a.txt").write_text("changed\n", encoding="utf-8")  # uncommitted
    (trees[1] / "notes.txt").write_text("mine\n", encoding="utf-8")  # untracked
    drive(monkeypatch, space, 0)
    assert pressured(space) == 0
    assert kept(trees) == []  # every build went and the drive is still under the floor
    for tree in trees:
        assert (tree / f"{tree.name}-0.txt").is_file()  # the unmerged commit's file
        assert git(space.checkout, "branch", "--list", tree.name)
    assert (trees[0] / "a.txt").read_text(encoding="utf-8") == "changed\n"
    assert (trees[1] / "notes.txt").is_file()


def test_pressure_leaves_busy_queued_and_current_worktrees_alone(space: Space, monkeypatch: pytest.MonkeyPatch) -> None:
    trees = ten_builds(space)
    by_name = {tree.name: tree for tree in trees}
    (by_name["w100"] / BUSY_FILE).write_text("", encoding="utf-8")
    claim_slot(space, "w90")
    queue_ticket(space, "w80")
    queue_ticket(space, "w70", lane="queue-fast")
    drive(monkeypatch, space, 0)
    assert pressured(space, cwd=by_name["w60"], margin=1000) == 0
    assert kept(trees) == sorted(["w100", "w90", "w80", "w70", "w60"])


def test_pressure_skips_a_build_that_git_does_not_ignore(space: Space, monkeypatch: pytest.MonkeyPatch) -> None:
    path = space.worktree("odd", commits=1, build=BUILD_BYTES)
    (path / ".gitignore").write_text("", encoding="utf-8")  # no longer ignores build/
    git(path, "add", "-A")
    git(path, "commit", "-q", "-m", "unignore")
    space.touch(path, OLD)
    drive(monkeypatch, space, 0)
    assert pressured(space) == 0
    assert (path / "build" / "dev" / "out.bin").is_file()


def test_nothing_is_deleted_while_the_drive_is_not_under_the_floor(
    space: Space, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    trees = ten_builds(space)
    drive(monkeypatch, space, 30)  # exactly the floor is not under it
    assert pressured(space) == 0
    assert len(kept(trees)) == 10
    assert "Disk pressure" not in capsys.readouterr().out


def test_an_absent_queue_folder_is_no_obstacle(space: Space, monkeypatch: pytest.MonkeyPatch) -> None:
    trees = ten_builds(space)
    assert not claims(space).exists()
    drive(monkeypatch, space, 29)
    assert pressured(space) == 0
    assert len(kept(trees)) == 6  # 29 + 4 = 33


def test_a_dry_run_reports_what_pressure_would_delete_and_deletes_nothing(
    space: Space, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    trees = ten_builds(space)
    # A dry run cannot read the space a deletion frees, so it adds up the real sizes: 1500 bytes short of the floor
    # takes two builds of 1000 bytes.
    monkeypatch.setattr(shutil, "disk_usage", lambda path: Usage(1000 * GIB, 0, 10 * GIB))
    code = run_clean(
        space.checkout,
        space.root,
        False,
        500.0,
        merged_heads=lambda branch: None,
        min_gb=10 + 1500 / GIB,
        margin_gb=0,
    )
    assert code == 0
    assert len(kept(trees)) == 10
    out = capsys.readouterr().out
    assert "Disk pressure" in out
    deleted = out.split("Disk pressure")[1]
    assert "would delete" in deleted and "w100" in deleted and "w90" in deleted
    assert "w80" not in deleted


def test_the_report_says_so_when_the_drive_stays_under_the_floor(
    space: Space, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    ten_builds(space)
    drive(monkeypatch, space, 5)
    assert pressured(space) == 0
    assert "still under the 30 GB floor" in capsys.readouterr().out


def test_the_normal_pass_runs_first_and_pressure_only_finishes_the_job(
    space: Space, monkeypatch: pytest.MonkeyPatch
) -> None:
    trees = ten_builds(space)
    drive(monkeypatch, space, 23)
    # Builds idle for 45 hours or more (w50 to w100, six) go in the normal pass: 29 GB, under the floor of 30, so
    # pressure deletes the next oldest, w40, w30 and w20, to reach the 32 GB of floor plus a margin of 2.
    code = run_clean(space.checkout, space.root, True, 45.0, merged_heads=lambda branch: None, min_gb=30, margin_gb=2)
    assert code == 0
    assert kept(trees) == ["w10"]


def test_the_floor_and_margin_default_to_the_conventions(space: Space, monkeypatch: pytest.MonkeyPatch) -> None:
    write_checkout(space.checkout, changed(("workspace.disk_min_free_gb", 20), ("workspace.disk_margin_gb", 1)))
    trees = ten_builds(space)
    drive(monkeypatch, space, 19)
    assert run_clean(space.checkout, space.root, True, 500.0, merged_heads=lambda branch: None) == 0
    assert len(kept(trees)) == 8  # 19 + 2 = 21: the floor plus the margin


def test_the_build_age_defaults_to_the_conventions_then_to_four_hours(space: Space) -> None:
    assert workspace_clean.DEFAULT_BUILD_HOURS == 4.0
    three = space.worktree("three", commits=1, build=100, age=3)
    six = space.worktree("six", commits=1, build=100, age=6)
    assert run_clean(space.checkout, space.root, True, merged_heads=lambda branch: None) == 0
    assert (three / "build").exists() and not (six / "build").exists()
    write_checkout(space.checkout, changed(("workspace.build_idle_hours", 2)))
    assert run_clean(space.checkout, space.root, True, merged_heads=lambda branch: None) == 0
    assert not (three / "build").exists()


def test_the_pressure_options_reach_clean_from_the_command_line(space: Space, monkeypatch: pytest.MonkeyPatch) -> None:
    trees = ten_builds(space)
    drive(monkeypatch, space, 25)
    monkeypatch.setattr(workspace_clean, "gh_merged_heads", lambda checkout: lambda branch: None)
    monkeypatch.chdir(space.checkout)
    assert main(["workspace", "clean", "--apply", "--build-hours", "500", "--min-gb", "30", "--margin-gb", "1"]) == 0
    assert len(kept(trees)) == 4  # 25 + 6 = 31
