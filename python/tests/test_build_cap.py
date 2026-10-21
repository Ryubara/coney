# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for coney_tools.build_cap: `coney-tools build` refuses to configure a new build folder while the cap on
worktrees holding builds is reached, and says who holds them. Real git worktrees in a temporary workspace; GitHub is
a stand-in."""

import json
import os
from pathlib import Path

import pytest
from test_workspace_clean import Space, tip
from test_workspace_clean_pressure import claim_slot

from coney_tools import build_cap, build_cli, build_run, pcsx2_proc, workspace_clean
from coney_tools.build_cap import BuildCapError, check
from coney_tools.cli import _build_parser, main
from coney_tools.config import INT_KEYS, load_config

#: Pull requests GitHub reports as merged, by branch, for the test that runs (the stand-in for `gh`).
MERGED: dict[str, list[str]] = {}


@pytest.fixture
def space(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> Space:
    MERGED.clear()
    monkeypatch.setattr(workspace_clean, "gh_merged_heads", lambda checkout: lambda branch: MERGED.get(branch, []))
    monkeypatch.setattr(pcsx2_proc, "pid_alive", lambda pid: pid == os.getpid())
    return Space(tmp_path)


def builds(space: Space, count: int, **kwargs: float) -> list[Path]:
    """`count` worktrees, each with unmerged work and a build folder (named w0, w1, ...)."""
    return [space.worktree(f"w{number}", commits=1, build=2048, **kwargs) for number in range(count)] if count else []


def new_folder(path: Path) -> Path:
    """The `build/dev` folder a run in `path` would configure."""
    return path / "build" / "dev"


def test_the_cap_defaults_to_seven_and_is_set_in_the_local_config(tmp_path: Path) -> None:
    assert INT_KEYS["build_worktree_cap"] == 7
    (tmp_path / "coney.local.example.toml").write_text("", encoding="utf-8")
    (tmp_path / "coney.local.toml").write_text("build_worktree_cap = 3\n", encoding="utf-8")
    assert load_config(tmp_path).numbers["build_worktree_cap"] == 3


def test_a_new_build_folder_is_refused_at_the_cap(space: Space) -> None:
    builds(space, 3)
    mine = space.worktree("mine")
    with pytest.raises(BuildCapError) as refused:
        check(mine, new_folder(mine), 3)
    message = str(refused.value)
    assert "3 worktrees already hold a build folder" in message
    assert "cap is 3" in message
    for name in ("w0", "w1", "w2"):
        assert name in message
    assert "GB" in message
    assert "idle" in message
    assert "workspace clean --apply" in message


def test_the_refusal_says_what_became_of_each_branch(space: Space) -> None:
    merged_by_pr = space.worktree("shipped", commits=1, build=2048)
    MERGED["shipped"] = [tip(merged_by_pr)]
    space.worktree("landed", build=2048)  # no commits of its own: contained in origin/main
    space.worktree("unfinished", commits=1, build=2048)
    busy = space.worktree("working", commits=1, build=2048)
    (busy / ".coney-busy").write_text("someone\n", encoding="utf-8")
    building = space.worktree("compiling", commits=1, build=2048)
    claim_slot(space, building.name)
    mine = space.worktree("mine")
    with pytest.raises(BuildCapError) as refused:
        check(mine, new_folder(mine), 5)
    lines = {line.split()[0]: line for line in str(refused.value).splitlines() if line.startswith("  ")}
    assert "merged" in lines["shipped"]
    assert "workspace clean removes it" in lines["shipped"]
    assert "merged" in lines["landed"]
    assert "unmerged" in lines["unfinished"]
    assert "busy" in lines["working"]
    assert "build slot" in lines["compiling"]


def test_a_run_below_the_cap_is_allowed(space: Space) -> None:
    builds(space, 2)
    mine = space.worktree("mine")
    check(mine, new_folder(mine), 3)


def test_a_folder_that_exists_keeps_building_above_the_cap(space: Space) -> None:
    paths = builds(space, 4)
    check(paths[0], new_folder(paths[0]), 3)  # build/dev exists in the made-up worktree
    assert new_folder(paths[0]).is_dir()


def test_a_worktree_that_already_holds_a_build_adds_none(space: Space) -> None:
    paths = builds(space, 3)
    check(paths[0], paths[0] / "build" / "asan", 3)


def test_the_main_checkout_neither_counts_nor_is_refused(space: Space) -> None:
    (space.checkout / "build" / "release").mkdir(parents=True)
    (space.checkout / "build" / "release" / "coney.exe").write_bytes(b"\0")
    builds(space, 2)
    mine = space.worktree("mine")
    check(mine, new_folder(mine), 3)  # two worktree builds and the main checkout's: still room
    check(space.checkout, space.checkout / "build" / "dev", 1)  # never refused itself


def test_worktrees_without_a_build_do_not_count(space: Space) -> None:
    builds(space, 2)
    space.worktree("plain")
    (space.worktrees / "plain" / "build").mkdir()  # an empty build/ is no build
    mine = space.worktree("mine")
    check(mine, new_folder(mine), 3)


def test_a_cap_of_zero_turns_it_off(space: Space) -> None:
    builds(space, 4)
    mine = space.worktree("mine")
    check(mine, new_folder(mine), 0)


def test_a_folder_outside_a_workspace_is_not_judged(tmp_path: Path) -> None:
    (tmp_path / "coney.local.example.toml").write_text("", encoding="utf-8")
    check(tmp_path, tmp_path / "build" / "dev", 1)


def run_build_args(path: Path, *extra: str) -> object:
    """The parsed command line of `build --agent a --dir path`."""
    return _build_parser().parse_args(["build", "--agent", "a", "--dir", str(path), *extra])


@pytest.fixture
def mine(space: Space, monkeypatch: pytest.MonkeyPatch) -> Path:
    """A worktree that can pass the rest of the precheck, with the cap set to 3 and three builds elsewhere."""
    (space.checkout / "coney.local.toml").write_text("build_worktree_cap = 3\n", encoding="utf-8")
    builds(space, 3)
    path = space.worktree("mine")
    presets = {
        "configurePresets": [{"name": "dev"}],
        "buildPresets": [{"name": "dev"}],
        "testPresets": [{"name": "dev"}],
    }
    (path / "CMakePresets.json").write_text(json.dumps(presets), encoding="utf-8")
    monkeypatch.setattr(build_run, "load_msvc_environment", lambda base, run: dict(base))
    monkeypatch.setattr(build_run, "find_programs", lambda env: None)
    return path


def test_the_build_command_refuses_before_it_waits_for_anything(mine: Path, capsys: pytest.CaptureFixture[str]) -> None:
    code = main(["build", "--agent", "a", "--dir", str(mine), "--preset", "dev"])
    assert code == 2
    error = capsys.readouterr().err
    assert error.startswith("coney-tools: build refused")
    assert "workspace clean --apply" in error
    assert not (mine / "build").exists()


def test_the_build_command_lets_an_existing_folder_build(mine: Path) -> None:
    new_folder(mine).mkdir(parents=True)
    root, _ = build_cli._precheck(run_build_args(mine))  # type: ignore[arg-type]
    assert root == mine


def test_a_custom_build_folder_is_judged_too(mine: Path) -> None:
    with pytest.raises(BuildCapError):
        build_cli._precheck(run_build_args(mine, "--build-dir", "build/tidy"))  # type: ignore[arg-type]


def test_the_cap_text_names_the_cleanup_command() -> None:
    assert build_cap.CLEANUP_COMMAND.endswith("workspace clean --apply")
