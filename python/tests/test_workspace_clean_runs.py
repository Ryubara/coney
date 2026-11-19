# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests that `workspace clean` never takes the build of a worktree a run is using: a `task check` (its progress file),
a detached build, check or fails-first (their result files), a busy file and a build folder's lock, each on its own,
with no build slot and no activity in the queue (coney_tools.live_runs, workspace_clean; issue 1141)."""

import argparse
import json
import os
import shutil
import time
from collections import namedtuple
from datetime import UTC, datetime
from pathlib import Path

import pytest
from test_workspace_clean import Space
from test_workspace_clean_pressure import claims

from coney_tools import (
    build_claims,
    check_cli,
    live_runs,
    pcsx2_proc,
    process_lifetime,
    stale_processes,
    unit_record,
)
from coney_tools.workspace_clean import BUSY_FILE, run_clean

GIB = 1024**3
Usage = namedtuple("Usage", "total used free")
DEAD_PID = 987_654


@pytest.fixture
def space(tmp_path: Path) -> Space:
    return Space(tmp_path)


@pytest.fixture(autouse=True)
def machine(monkeypatch: pytest.MonkeyPatch) -> None:
    """Only this process is alive, no real process is listed or stopped, and the drive has room."""
    monkeypatch.setattr(pcsx2_proc, "pid_alive", lambda pid: pid == os.getpid())
    monkeypatch.setattr(stale_processes, "list_processes", lambda: [])
    monkeypatch.setattr(stale_processes, "kill_tree", lambda pid: None)
    monkeypatch.setattr(shutil, "disk_usage", lambda path: Usage(1000 * GIB, 0, 500 * GIB))


def scratch(space: Space, name: str) -> Path:
    """The scratch folder of the unit `name` in the made-up workspace."""
    folder = space.root / "scratch" / name
    folder.mkdir(parents=True, exist_ok=True)
    return folder


def progress_file(folder: Path, state: str, pid: int | None = None, age: float = 0.0) -> None:
    """The `check-progress.json` of a `task check` in `state`, last written `age` seconds ago."""
    moment = datetime.fromtimestamp(time.time() - age, UTC).isoformat()
    record = {"schema": 1, "state": state, "pid": os.getpid() if pid is None else pid, "since": moment}
    (folder / "check-progress.json").write_text(json.dumps({**record, "updated": moment}), encoding="utf-8")


def result_file(folder: Path, name: str, state: str, pid: int | None = None, started_apart: float = 0.0) -> None:
    """The result file `name` (`build-result.json`, ...) of a detached run in `state`; `started_apart` seconds is how
    far the process the record names started from when the record says."""
    pid = os.getpid() if pid is None else pid
    actual = process_lifetime.process_started(pid)
    record = {
        "state": state,
        "pid": pid,
        "process_started": None if actual is None else actual - started_apart,
        "started": time.time(),
    }
    (folder / name).write_text(json.dumps(record), encoding="utf-8")


def clean(space: Space) -> int:
    """`clean --apply` with GitHub unreachable."""
    return run_clean(space.checkout, space.root, True, merged_heads=lambda branch: None)


def assert_kept(space: Space, tree: Path, why: str) -> None:
    """The plan calls `tree` busy (naming `why`) and `clean --apply` leaves its build and the worktree alone."""
    assert why in space.entries()[tree.name].busy
    assert clean(space) == 0
    assert (tree / "build" / "dev" / "out.bin").is_file(), "the build was deleted"
    assert tree.is_dir()


def test_a_running_task_checks_progress_file_keeps_its_build_with_no_activity(space: Space) -> None:
    tree = space.worktree("checking", commits=1, build=100)
    progress_file(scratch(space, "checking"), "running")
    assert not (claims(space) / "active").exists()
    assert_kept(space, tree, "task check")


def test_a_check_waiting_for_a_slot_keeps_its_build_too(space: Space) -> None:
    tree = space.worktree("queued", commits=1, build=100)
    progress_file(scratch(space, "queued"), "waiting")
    assert_kept(space, tree, "task check")


@pytest.mark.parametrize(
    ("file", "what"),
    [
        ("build-result.json", "build"),
        ("check-result.json", "check"),
        ("fails-first-result.json", "fails-first"),
    ],
)
def test_a_detached_run_that_is_running_keeps_the_build(space: Space, file: str, what: str) -> None:
    tree = space.worktree("detached", commits=1, build=100)
    result_file(scratch(space, "detached"), file, "running")
    assert_kept(space, tree, what)


def test_a_fix_tasks_run_protects_its_parents_worktree(space: Space) -> None:
    tree = space.worktree("parent", commits=1, build=100)
    folder = scratch(space, "fix-of-parent")
    progress_file(folder, "running")
    record = unit_record.UnitRecord(
        task="fix-of-parent",
        kind="fix",
        issues=(1,),
        claimant="a",
        branch="parent",
        worktree=tree,
        base_ref="origin/main",
        base_commit="abc",
        started=datetime.now(UTC),
        parent="parent",
    )
    unit_record.write_record(folder / "unit.json", record)
    assert_kept(space, tree, "task check")


@pytest.mark.parametrize(
    "write",
    [
        pytest.param(lambda folder: progress_file(folder, "running", pid=DEAD_PID), id="progress of a dead process"),
        pytest.param(lambda folder: progress_file(folder, "running", age=3600), id="progress not written for an hour"),
        pytest.param(lambda folder: progress_file(folder, "passed"), id="progress of a finished check"),
        pytest.param(lambda folder: result_file(folder, "build-result.json", "done"), id="a finished build"),
        pytest.param(
            lambda folder: result_file(folder, "check-result.json", "running", pid=DEAD_PID), id="a killed check"
        ),
        pytest.param(
            # the pid lives, but it is another program's: Windows hands a dead process's pid to a later one
            lambda folder: result_file(folder, "build-result.json", "running", started_apart=3600.0),
            id="a reused pid that started at another time",
        ),
    ],
)
def test_a_run_that_is_over_protects_nothing(space: Space, write: object) -> None:
    tree = space.worktree("done", commits=1, build=100)
    write(scratch(space, "done"))  # type: ignore[operator]
    assert space.entries()["done"].busy == ""
    assert clean(space) == 0
    assert not (tree / "build").exists()


def test_a_run_in_another_worktree_protects_only_that_one(space: Space) -> None:
    mine = space.worktree("mine", commits=1, build=100)
    other = space.worktree("other", commits=1, build=100)
    progress_file(scratch(space, "mine"), "running")
    assert clean(space) == 0
    assert (mine / "build").is_dir()
    assert not (other / "build").exists()


def test_a_busy_file_keeps_the_build(space: Space) -> None:
    tree = space.worktree("busy", commits=1, build=100)
    (tree / BUSY_FILE).write_text("agent\n", encoding="utf-8")
    assert_kept(space, tree, BUSY_FILE)


def test_a_build_folders_lock_keeps_the_build(space: Space) -> None:
    tree = space.worktree("locked", commits=1, build=100)
    held = claims(space) / "folders" / "dev" / "slot-1.claim"
    held.mkdir(parents=True)
    owner = {
        "slot": 1,
        "agent": "a",
        "worktree": "locked",
        "preset": "dev",
        "step": "build",
        "pid": os.getpid(),
        "started": datetime.now(UTC).isoformat(timespec="seconds"),
        "lane": "main",
    }
    (held / "owner.json").write_text(json.dumps(owner), encoding="utf-8")
    assert_kept(space, tree, "build slot")


def test_the_scratch_is_read_again_each_time_it_is_asked(space: Space) -> None:
    folder = scratch(space, "again")
    now = time.time()
    assert live_runs.running(folder.parent, now) == {}
    progress_file(folder, "running")
    assert live_runs.running(folder.parent, now) == {"again": ["task check"]}
    progress_file(folder, "passed")
    assert live_runs.running(folder.parent, now) == {}


def test_a_scratch_folder_with_broken_files_names_nobody(space: Space) -> None:
    folder = scratch(space, "broken")
    (folder / "check-progress.json").write_text("{not json", encoding="utf-8")
    (folder / "build-result.json").write_text("[]", encoding="utf-8")
    (folder / "unit.json").write_text("{}", encoding="utf-8")
    assert live_runs.running(folder.parent, time.time()) == {}
    assert live_runs.running(folder.parent / "nowhere", time.time()) == {}


def test_the_dry_run_names_the_run_that_keeps_the_build(space: Space, capsys: pytest.CaptureFixture[str]) -> None:
    space.worktree("shown", commits=1, build=100)
    progress_file(scratch(space, "shown"), "running")
    assert run_clean(space.checkout, space.root, False, merged_heads=lambda branch: None) == 0
    assert "task check runs in it" in capsys.readouterr().out


def test_ci_run_holds_the_activity_while_it_runs(space: Space, monkeypatch: pytest.MonkeyPatch) -> None:
    tree = space.worktree("1-ci", commits=1)
    seen: list[set[str]] = []

    def run_changed(base: str, only: list[str] | None, skip: list[str] | None) -> int:
        seen.append(build_claims.live_worktrees(claims(space)))
        return 0

    monkeypatch.setattr(check_cli, "run_changed", run_changed)
    monkeypatch.chdir(tree)
    args = argparse.Namespace(command="run", result_file=None, detach=False, base="origin/main", only=None, skip=None)
    assert check_cli.run_check(args) == 0
    assert seen == [{"1-ci"}]
    assert build_claims.live_worktrees(claims(space)) == set()
