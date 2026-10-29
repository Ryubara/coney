# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for `workspace clean` and `workspace check-space` with `build_root` set, and for the running checks that
protect a build however idle it looks (coney_tools.workspace_clean, build_root, build_claims.activity)."""

import argparse
import os
import shutil
import time
from collections import namedtuple
from pathlib import Path

import pytest
from test_workspace_clean import OLD, Space, git
from test_workspace_clean_pressure import claim_slot, claims

from coney_tools import (
    build_claims,
    build_root,
    fails_first,
    original_cli,
    pcsx2_proc,
    stale_processes,
    task_check,
    unit,
)
from coney_tools.conventions import load_conventions
from coney_tools.workspace_clean import BUSY_FILE, run_clean, run_disk

GIB = 1024**3
Usage = namedtuple("Usage", "total used free")
REPO = Path(__file__).resolve().parents[2]
DEAD_PID = 987_654


@pytest.fixture
def space(tmp_path: Path) -> Space:
    return Space(tmp_path)


@pytest.fixture(autouse=True)
def machine(monkeypatch: pytest.MonkeyPatch) -> None:
    """Only this process is alive; no real processes are listed or stopped; the drive has room."""
    monkeypatch.setattr(pcsx2_proc, "pid_alive", lambda pid: pid == os.getpid())
    monkeypatch.setattr(stale_processes, "list_processes", lambda: [])
    monkeypatch.setattr(stale_processes, "kill_tree", lambda pid: None)
    monkeypatch.setattr(shutil, "disk_usage", lambda path: Usage(1000 * GIB, 0, 500 * GIB))


def builds(space: Space) -> Path:
    """The build root of the made-up machine, outside the workspace."""
    return space.root / "other-drive" / "coney_builds"


def with_root(space: Space) -> None:
    """Set `build_root` in the main checkout's coney.local.toml."""
    (space.checkout / "coney.local.toml").write_text(f"build_root = '{builds(space).as_posix()}'\n", encoding="utf-8")


def root_build(space: Space, tree: Path, size: int = 1000, age: float = OLD) -> Path:
    """A build of `tree` in the build root holding `size` bytes, everything in it and the worktree last touched `age`
    hours ago; returns the worktree's folder in the root."""
    folder = build_root.tree(tree, create=True)
    (folder / "dev").mkdir(parents=True, exist_ok=True)
    (folder / "dev" / "out.bin").write_bytes(b"\0" * size)
    when = time.time() - age * 3600
    for path in (folder / "dev" / "out.bin", folder / "dev", folder / build_root.MARKER_FILE, folder):
        os.utime(path, (when, when))
    space.touch(tree, age)
    return folder


def clean(space: Space, apply: bool, cwd: Path | None = None, **more: float) -> int:
    """Run `clean` with GitHub unreachable."""
    return run_clean(cwd or space.checkout, space.root, apply, merged_heads=lambda branch: None, **more)  # type: ignore[arg-type]


# --- build_root unset or set: what clean does with the folder there ----------------------------------------------


def test_an_idle_build_in_the_root_is_deleted_and_the_worktree_kept(space: Space) -> None:
    with_root(space)
    tree = space.worktree("work", commits=1)
    folder = root_build(space, tree)
    entry = space.entries()["work"]
    assert entry.delete_build and not entry.remove
    assert clean(space, True) == 0
    assert not folder.exists()
    assert tree.is_dir()


def test_a_recent_build_in_the_root_is_kept(space: Space) -> None:
    with_root(space)
    tree = space.worktree("work", commits=1, age=0.1)
    folder = root_build(space, tree, age=0.1)
    assert not space.entries()["work"].delete_build
    assert clean(space, True) == 0
    assert folder.is_dir()


def test_a_merged_worktree_takes_its_root_build_with_it(space: Space) -> None:
    with_root(space)
    tree = space.worktree("done")
    folder = root_build(space, tree, size=5000)
    entry = space.entries()["done"]
    assert entry.remove
    assert entry.reclaim >= 5000
    assert clean(space, True) == 0
    assert not tree.exists() and not folder.exists()


def test_a_busy_worktree_keeps_its_root_build(space: Space) -> None:
    with_root(space)
    tree = space.worktree("work", commits=1)
    folder = root_build(space, tree)
    (tree / BUSY_FILE).write_text("agent\n", encoding="utf-8")
    assert clean(space, True) == 0
    assert folder.is_dir()


def test_a_live_slot_keeps_its_root_build(space: Space) -> None:
    with_root(space)
    tree = space.worktree("work", commits=1)
    folder = root_build(space, tree)
    claim_slot(space, "work")
    assert clean(space, True) == 0
    assert folder.is_dir()


def test_a_folder_in_the_root_without_a_marker_is_never_deleted(space: Space) -> None:
    with_root(space)
    tree = space.worktree("done")
    folder = root_build(space, tree)
    (folder / build_root.MARKER_FILE).unlink()
    assert clean(space, True) == 0
    assert (folder / "dev" / "out.bin").is_file()


def test_orphans_are_listed_and_left_alone(space: Space, capsys: pytest.CaptureFixture[str]) -> None:
    with_root(space)
    tree = space.worktree("gone", commits=1)
    folder = root_build(space, tree)
    git(space.checkout, "worktree", "remove", "--force", str(tree))
    (builds(space) / "hand-made").mkdir()
    assert clean(space, True) == 0
    out = capsys.readouterr().out
    assert "Orphaned build folders" in out and folder.name in out
    assert "hand-made" in out
    assert folder.is_dir() and (builds(space) / "hand-made").is_dir()


def test_pressure_on_the_build_roots_drive_deletes_root_builds_oldest_first(
    space: Space, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    with_root(space)
    folders = {age: root_build(space, space.worktree(f"w{age}", commits=1), age=age) for age in (10, 20, 30)}
    root = builds(space)

    def usage(path: str | Path) -> Usage:
        # The build root's drive is short by the size of two builds (1000 bytes stand for 1 GB); the workspace's has
        # room.
        if Path(path).resolve().is_relative_to(root.resolve()):
            left = sum(1 for folder in folders.values() if folder.exists())
            return Usage(1000 * GIB, 0, int((29 - left) * GIB + 3 * GIB))
        return Usage(1000 * GIB, 0, 500 * GIB)

    monkeypatch.setattr(shutil, "disk_usage", usage)
    monkeypatch.setattr(build_root, "same_drive", lambda one, two: False)
    assert clean(space, True, build_hours=500.0, min_gb=30, margin_gb=1) == 0
    # 29 GB free with three builds; deleting the 30 h and the 20 h ones makes 31 GB, the floor plus the margin.
    assert not folders[30].exists() and not folders[20].exists()
    assert folders[10].is_dir()
    out = capsys.readouterr().out
    assert f"Free space on the build root ({root.resolve()})" in out


def test_check_space_measures_the_build_roots_drive_too(
    space: Space, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    with_root(space)
    root = builds(space)
    root.mkdir(parents=True)

    def usage(path: str | Path) -> Usage:
        low = Path(path).resolve().is_relative_to(root.resolve())
        return Usage(1000 * GIB, 0, (10 if low else 500) * GIB)

    monkeypatch.setattr(shutil, "disk_usage", usage)
    monkeypatch.setattr(build_root, "same_drive", lambda one, two: False)
    assert run_disk(space.checkout, space.root, 30) == 1
    out = capsys.readouterr().out
    assert "workspace drive: 500.0 GB" in out
    assert f"build root ({root.resolve()}): 10.0 GB" in out


def test_check_space_without_a_build_root_measures_one_drive(
    space: Space, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    assert run_disk(space.checkout, space.root, 30) == 0
    assert "build root" not in capsys.readouterr().out


# --- the idle test: a running check, fails-first or build protects its build ---------------------------------------


def pressured(space: Space, monkeypatch: pytest.MonkeyPatch) -> int:
    """Run `clean --apply` with the drive under the floor, whatever is deleted."""
    monkeypatch.setattr(shutil, "disk_usage", lambda path: Usage(1000 * GIB, 0, 1 * GIB))
    return clean(space, True, build_hours=500.0, min_gb=30, margin_gb=1)


def test_a_running_task_check_keeps_its_build_under_disk_pressure(
    space: Space, monkeypatch: pytest.MonkeyPatch
) -> None:
    tree = space.worktree("checking", commits=1, build=100)
    with build_claims.activity(claims(space), "checking", "agent", "task check"):
        assert "checking" in build_claims.live_worktrees(claims(space))
        assert "task check" in space.entries()["checking"].busy
        assert pressured(space, monkeypatch) == 0
        assert (tree / "build").is_dir()
    assert "checking" not in build_claims.live_worktrees(claims(space))
    assert pressured(space, monkeypatch) == 0
    assert not (tree / "build").exists()


def test_a_running_check_keeps_an_idle_build_and_a_merged_worktree(space: Space) -> None:
    merged = space.worktree("merged", build=100)
    idle = space.worktree("idle", commits=1, build=100)
    with (
        build_claims.activity(claims(space), "merged", "a", "orig fails-first"),
        build_claims.activity(claims(space), "idle", "a", "task check"),
    ):
        entries = space.entries()
        assert not entries["merged"].remove and not entries["idle"].delete_build
        assert clean(space, True) == 0
    assert (merged / "build").is_dir() and (idle / "build").is_dir()


def test_the_activity_of_a_dead_process_protects_nothing(space: Space) -> None:
    space.worktree("done", build=100)
    with build_claims.activity(claims(space), "done", "a", "task check", pid=DEAD_PID):
        assert "done" not in build_claims.live_worktrees(claims(space))
        assert space.entries()["done"].remove


def test_task_check_holds_the_activity_while_it_runs(space: Space, monkeypatch: pytest.MonkeyPatch) -> None:
    tree = space.worktree("1-check", commits=1)
    seen: list[set[str]] = []

    def run_check(request: object, env: object) -> int:
        seen.append(live(space))
        return 0

    monkeypatch.setattr(task_check, "run_check", run_check)
    args = argparse.Namespace(
        name=None, worktree=tree, base=None, agent="a", profile=None, keep_going=False, dry_run=False, max_wait=1.0
    )
    assert task_check.run(args) == 0
    assert seen == [{"1-check"}]
    assert live(space) == set()


def test_fails_first_holds_the_activity_while_it_runs(space: Space, monkeypatch: pytest.MonkeyPatch) -> None:
    tree = space.worktree("1-fix", commits=1)
    seen: list[set[str]] = []

    def run(*_: object, **__: object) -> list[object]:
        seen.append(live(space))
        return []

    monkeypatch.setattr(fails_first, "run", run)
    args = argparse.Namespace(none=None, test=["t"], test_commit="abc", fix_commit=None, agent="a", preset="dev")
    conventions = load_conventions(REPO)
    folder = unit.Unit("1-fix", space.root / "scratch" / "1-fix")
    who = {"agent": {"id": "a"}}
    assert original_cli._fails_first(args, tree, conventions, folder, who) == 0
    assert seen == [{"1-fix"}]
    assert live(space) == set()


def live(space: Space) -> set[str]:
    """The worktrees the build queue of the made-up workspace calls live now."""
    return build_claims.live_worktrees(claims(space))
