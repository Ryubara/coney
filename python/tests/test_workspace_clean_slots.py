# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for what `workspace clean` leaves alone because a build needs it: the live slots, read from the build queue
itself each time, and the busy marker that goes stale (coney_tools.workspace_clean, build_claims.live_worktrees)."""

import os
import shutil
import time
from collections import namedtuple
from collections.abc import Iterator
from dataclasses import replace
from pathlib import Path

import pytest
from test_workspace_clean import OLD, Space
from test_workspace_clean_pressure import claim_slot, claims, queue_ticket

from coney_tools import pcsx2_proc, stale_processes, workspace_clean
from coney_tools.build_claims import FAST_LANE, TIDY_LANE, Slots, live_worktrees
from coney_tools.conventions import load_conventions
from coney_tools.workspace_clean import BUSY_FILE, DEFAULT_BUILD_HOURS, run_clean

Usage = namedtuple("Usage", "total used free")
DEAD_PID = 987_654
REPO = Path(__file__).resolve().parents[2]


@pytest.fixture
def space(tmp_path: Path) -> Space:
    return Space(tmp_path)


@pytest.fixture(autouse=True)
def machine(monkeypatch: pytest.MonkeyPatch) -> None:
    """Only this process is alive; no real processes are listed or stopped; the drive has room."""
    monkeypatch.setattr(pcsx2_proc, "pid_alive", lambda pid: pid == os.getpid())
    monkeypatch.setattr(stale_processes, "list_processes", lambda: [])
    monkeypatch.setattr(stale_processes, "kill_tree", lambda pid: None)
    monkeypatch.setattr(shutil, "disk_usage", lambda path: Usage(1000 * 1024**3, 0, 500 * 1024**3))


def test_a_live_slot_keeps_a_merged_worktree(space: Space) -> None:
    space.worktree("done", build=100)
    claim_slot(space, "done")
    entry = space.entries()["done"]
    assert not entry.remove
    assert not entry.delete_build
    assert "build slot" in entry.why
    assert entry.busy


def test_a_live_slot_keeps_an_idle_build_folder(space: Space) -> None:
    space.worktree("work", commits=1, build=100)
    assert space.entries()["work"].delete_build
    claim_slot(space, "work")
    assert not space.entries()["work"].delete_build


def test_a_claim_of_a_dead_build_protects_nothing(space: Space) -> None:
    space.worktree("done", build=100)
    claim_slot(space, "done", pid=DEAD_PID)
    assert space.entries()["done"].remove


def test_a_place_in_the_queue_protects_like_a_slot(space: Space) -> None:
    space.worktree("one", build=100)
    space.worktree("two", build=100)
    queue_ticket(space, "one")
    queue_ticket(space, "two", lane="queue-fast")
    entries = space.entries()
    assert entries["one"].busy
    assert entries["two"].busy


def test_the_slot_list_is_read_from_the_queue_each_time(space: Space) -> None:
    space.worktree("done", build=100)
    live = space.settings().live
    assert "done" not in live()
    claim_slot(space, "done")
    assert "done" in live()
    for owner in claims(space).glob("slot-*.claim/owner.json"):
        owner.unlink()
        owner.parent.rmdir()
    assert "done" not in live()


def test_the_queue_folder_comes_from_the_local_config(space: Space, tmp_path: Path) -> None:
    elsewhere = tmp_path / "elsewhere"
    (space.checkout / "coney.local.toml").write_text(f"build_claims_dir = '{elsewhere}'\n", encoding="utf-8")
    space.worktree("done", build=100)
    queue = Slots(elsewhere, 2)
    queue.try_claim(queue.join("a", "done", "dev", False, os.getpid()))
    assert workspace_clean.live_slots(space.root, space.checkout)() == {"done"}


def test_live_worktrees_covers_every_lane_and_the_folder_locks(tmp_path: Path) -> None:
    folder = tmp_path / "claims"
    main_lane = Slots(folder, 2)
    main_lane.try_claim(main_lane.join("a", "wt-main", "dev", False, os.getpid()))
    main_lane.join("b", "wt-waiting", "dev", False, os.getpid())
    for lane, name in ((FAST_LANE, "wt-fast"), (TIDY_LANE, "wt-tidy")):
        slots = Slots(folder, 1, lane=lane)
        slots.try_claim(slots.join("c", name, "dev", False, os.getpid()))
    lock = Slots(folder / "folders" / "wt-lock-dev", 1)
    lock.try_claim(lock.join("d", "wt-lock", "dev", False, os.getpid()))
    assert live_worktrees(folder) == {"wt-main", "wt-waiting", "wt-fast", "wt-tidy", "wt-lock"}


def test_live_worktrees_ignores_dead_builds_and_a_missing_folder(tmp_path: Path) -> None:
    assert live_worktrees(tmp_path / "none") == set()
    slots = Slots(tmp_path / "claims", 2)
    slots.try_claim(slots.join("a", "wt-dead", "dev", False, DEAD_PID))
    slots.join("b", "wt-dead-ticket", "dev", False, DEAD_PID)
    assert live_worktrees(tmp_path / "claims") == set()


def test_a_build_that_starts_during_a_clean_is_not_deleted(space: Space, monkeypatch: pytest.MonkeyPatch) -> None:
    path = space.worktree("done", build=100)
    # The first look (judging the worktree) finds no build, every later one finds one.
    answers: Iterator[set[str]] = iter([set()])
    monkeypatch.setattr(workspace_clean, "live_slots", lambda workspace, checkout: lambda: next(answers, {"done"}))
    monkeypatch.setattr(workspace_clean, "gh_merged_heads", lambda checkout: lambda branch: None)
    assert run_clean(space.checkout, None, True) == 0
    assert (path / "build").is_dir()


def forgotten(space: Space, name: str, text: str, hours: float) -> Path:
    """A worktree, merged and old, with a busy file holding `text` last written `hours` ago."""
    path = space.worktree(name, build=100)
    marker = path / BUSY_FILE
    marker.write_text(text, encoding="utf-8")
    when = time.time() - hours * 3600
    os.utime(marker, (when, when))
    return path


def test_an_empty_marker_older_than_the_threshold_protects_nothing(space: Space) -> None:
    forgotten(space, "gone", "", OLD)
    assert space.entries()["gone"].remove


def test_a_recent_empty_marker_protects(space: Space) -> None:
    forgotten(space, "working", "", 1)
    assert space.entries()["working"].busy


def test_a_marker_with_an_id_in_it_protects_however_old(space: Space) -> None:
    forgotten(space, "named", "tools-build-cap\n", OLD)
    entry = space.entries()["named"]
    assert entry.busy
    assert not entry.remove


def test_the_marker_threshold_is_the_idle_hours(space: Space) -> None:
    path = forgotten(space, "edge", "", 30)
    tree = workspace_clean.list_worktrees(space.checkout)[1]
    assert tree.path == path
    assert not workspace_clean.assess(tree, space.settings()).busy  # 30 h is past the default 24 h
    assert workspace_clean.assess(tree, replace(space.settings(), marker_hours=48.0)).busy


def test_the_idle_threshold_defaults_to_one_day() -> None:
    assert DEFAULT_BUILD_HOURS == 24.0
    assert load_conventions(REPO).workspace.build_idle_hours == 24


def test_clean_is_a_dry_run_and_says_why_it_keeps_a_busy_one(
    space: Space, capsys: pytest.CaptureFixture[str], monkeypatch: pytest.MonkeyPatch
) -> None:
    monkeypatch.setattr(workspace_clean, "gh_merged_heads", lambda checkout: lambda branch: None)
    removable = space.worktree("done", build=100)
    space.worktree("building", build=100)
    claim_slot(space, "building")
    assert run_clean(space.checkout, None, False) == 0
    out = capsys.readouterr().out
    assert "would remove" in out
    assert "building: holds or waits for a build slot" in out
    assert removable.is_dir()
