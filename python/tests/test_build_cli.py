# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for the `coney-tools build` command line (coney_tools.build_cli): its flags, the precheck, the printed summary
and exit codes, `build status` and `build release`. The toolchain is a fake; the claims are real folders in a
temporary directory."""

import json
import os
from collections.abc import Callable
from pathlib import Path
from typing import ClassVar

import pytest

from coney_tools import build_run, pcsx2_proc
from coney_tools.build_claims import FAST_LANE, MAIN_LANE, default_slots
from coney_tools.cli import _build_parser, main

PRESETS = {
    "configurePresets": [{"name": "base"}, {"name": "dev"}, {"name": "ci"}],
    "buildPresets": [{"name": "dev"}, {"name": "ci"}],
    "testPresets": [{"name": "dev"}, {"name": "ci"}],
}


@pytest.fixture
def checkout(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> Path:
    """A made-up Coney checkout with presets and a local config that puts the claims and scratch in tmp_path."""
    root = tmp_path / "work" / "worktrees" / "coney" / "wt"
    root.mkdir(parents=True)
    (root / "coney.local.example.toml").write_text("", encoding="utf-8")
    (root / "CMakePresets.json").write_text(json.dumps(PRESETS), encoding="utf-8")
    (root / "coney.local.toml").write_text(
        f"build_claims_dir = '{tmp_path / 'claims'}'\nscratch_dir = '{tmp_path / 'scratch'}'\nbuild_slots = 2\n",
        encoding="utf-8",
    )
    monkeypatch.chdir(root)
    monkeypatch.setattr(pcsx2_proc, "pid_alive", lambda pid: True)
    return root


class FakeToolchain:
    """A toolchain that prints canned lines and records what it was asked to run."""

    commands: ClassVar[list[list[str]]] = []
    outputs: ClassVar[dict[str, tuple[int, str]]] = {}

    def __init__(self, checkout: Path, env: object) -> None:
        self.checkout = checkout

    def capture(self, command: list[str], cwd: Path | None) -> tuple[int, str]:
        return 0, "[1/2] a\n[2/2] b\n"

    def run(self, command: list[str], on_line: Callable[[str], None]) -> int:
        FakeToolchain.commands.append(command)
        key = "test" if command[0] == "ctest" else ("build" if "--build" in command else "configure")
        code, text = FakeToolchain.outputs.get(key, (0, ""))
        for line in text.splitlines():
            on_line(line)
        return code


@pytest.fixture
def fake_toolchain(monkeypatch: pytest.MonkeyPatch) -> type[FakeToolchain]:
    FakeToolchain.commands = []
    FakeToolchain.outputs = {}
    monkeypatch.setattr(build_run, "ProcessToolchain", FakeToolchain)
    monkeypatch.setattr(build_run, "load_msvc_environment", lambda base, run: dict(base))
    monkeypatch.setattr(build_run, "find_programs", lambda env: None)
    return FakeToolchain


def test_the_flags_of_a_build_are_parsed() -> None:
    args = _build_parser().parse_args(
        [
            "build",
            "--agent",
            "a",
            "--preset",
            "ci",
            "--dir",
            "x",
            "--configure",
            "--test",
            "-R",
            "core",
            "--priority",
            "--max-wait",
            "5",
            "-j",
            "3",
        ]
    )
    assert (args.agent, args.preset, args.configure, args.test, args.regex, args.priority) == (
        "a",
        "ci",
        True,
        True,
        "core",
        True,
    )
    assert (args.max_wait, args.jobs, args.build_command) == (5.0, 3, None)
    defaults = _build_parser().parse_args(["build", "--agent", "a"])
    assert (defaults.preset, defaults.max_wait, defaults.test, defaults.priority) == ("dev", 45.0, False, False)
    alias = _build_parser().parse_args(["build", "--agent", "a", "--slots-wait-timeout", "7"])
    assert alias.max_wait == 7.0


def test_status_and_release_are_subcommands() -> None:
    parser = _build_parser()
    assert parser.parse_args(["build", "status", "--json"]).build_command == "status"
    release = parser.parse_args(["build", "release", "--agent", "a", "--force", "--slot", "2", "--lane", "fast"])
    assert (release.build_command, release.release_agent, release.force, release.slot, release.lane) == (
        "release",
        "a",
        True,
        2,
        "fast",
    )


def test_a_build_needs_an_agent(checkout: Path, capsys: pytest.CaptureFixture[str]) -> None:
    assert main(["build"]) == 2
    assert "--agent" in capsys.readouterr().err


def test_an_unknown_preset_is_refused_before_any_slot_is_taken(
    checkout: Path, capsys: pytest.CaptureFixture[str], fake_toolchain: type[FakeToolchain]
) -> None:
    (checkout / "CMakePresets.json").write_text(json.dumps({**PRESETS, "buildPresets": []}), encoding="utf-8")
    assert main(["build", "--agent", "a", "--preset", "asan"]) == 2
    assert "asan" in capsys.readouterr().err
    assert fake_toolchain.commands == []


def test_a_successful_build_prints_the_log_first_then_a_short_summary(
    checkout: Path, tmp_path: Path, capsys: pytest.CaptureFixture[str], fake_toolchain: type[FakeToolchain]
) -> None:
    fake_toolchain.outputs["build"] = (0, "[1/2] Building a\n[2/2] Linking b\n")
    fake_toolchain.outputs["test"] = (
        0,
        "1/1 Test #1: a ... Passed 0.1 sec\n100% tests passed, 0 tests failed out of 1\n",
    )
    assert main(["build", "--agent", "alice", "--configure", "--test"]) == 0
    lines = capsys.readouterr().out.splitlines()
    log = tmp_path / "scratch" / "wt" / "build-dev.log"
    assert lines[0] == f"log: {log}"
    assert lines[-1] == "result: ok" and any("1 passed, 0 failed of 1" in line for line in lines)
    assert "[1/2] Building a" in log.read_text(encoding="utf-8")  # the full output is in the log, not on screen
    assert not any("Building a" in line for line in lines)
    assert [c[0] for c in fake_toolchain.commands] == ["cmake", "cmake", "ctest"]
    snapshot = default_slots(MAIN_LANE, checkout).status()
    assert all(row.claim is None for row in snapshot.slots) and snapshot.queue == []


def test_a_failure_exits_1_with_the_first_errors(
    checkout: Path, capsys: pytest.CaptureFixture[str], fake_toolchain: type[FakeToolchain]
) -> None:
    fake_toolchain.outputs["build"] = (1, "C:\\a.cpp(3): error C2065: 'x': undeclared identifier\n")
    assert main(["build", "--agent", "alice", "--configure"]) == 1
    out = capsys.readouterr().out
    assert "build: FAILED (exit 1)" in out and "C2065" in out and "result: FAILED at build" in out


def test_an_interrupt_exits_130_and_frees_the_slot(
    checkout: Path,
    capsys: pytest.CaptureFixture[str],
    fake_toolchain: type[FakeToolchain],
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    def interrupted(self: FakeToolchain, command: list[str], on_line: Callable[[str], None]) -> int:
        raise KeyboardInterrupt

    monkeypatch.setattr(FakeToolchain, "run", interrupted)
    assert main(["build", "--agent", "alice", "--configure"]) == 130
    assert all(row.claim is None for row in default_slots(MAIN_LANE, checkout).status().slots)


def test_a_wait_that_runs_out_exits_75(
    checkout: Path,
    capsys: pytest.CaptureFixture[str],
    fake_toolchain: type[FakeToolchain],
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    monkeypatch.setattr("time.sleep", lambda seconds: None)
    clock = iter(range(10**6))
    monkeypatch.setattr("time.time", lambda: 1_800_000_000.0 + next(clock))  # a second per look
    for number, agent in enumerate(["bob", "carol"]):
        slots = default_slots(MAIN_LANE, checkout)
        ticket = slots.join(agent, "wt", "dev", False, 4000 + number)
        assert slots.try_claim(ticket) is not None
        slots.leave(ticket)
    assert main(["build", "--agent", "alice", "--configure", "--max-wait", "3"]) == 75
    out = capsys.readouterr().out
    assert "still queued, position 1" in out and "try again later" in out
    assert default_slots(MAIN_LANE, checkout).status().queue == []


def test_status_shows_holders_waiters_and_the_fast_lane(checkout: Path, capsys: pytest.CaptureFixture[str]) -> None:
    main_lane, fast = default_slots(MAIN_LANE, checkout), default_slots(FAST_LANE, checkout)
    held = main_lane.join("alice", "wt-a", "dev", False, 5001)
    assert main_lane.try_claim(held) is not None
    main_lane.set_step(main_lane.status().slots[0].claim, "build")  # type: ignore[arg-type]
    main_lane.leave(held)
    main_lane.join("bob", "wt-b", "ci", True, 5002)
    quick = fast.join("carol", "wt-c", "dev", False, 5003)
    assert fast.try_claim(quick) is not None
    assert main(["build", "status"]) == 0
    out = capsys.readouterr().out
    assert "slot 1: alice, wt-a, dev, build" in out and "slot 2: free" in out
    assert "1. bob, wt-b, ci, priority" in out
    assert "fast lane (1 slot)" in out and "slot 1: carol, wt-c, dev, starting" in out


def test_status_as_json(checkout: Path, capsys: pytest.CaptureFixture[str]) -> None:
    slots = default_slots(MAIN_LANE, checkout)
    slots.join("bob", "wt-b", "ci", True, 5002)
    assert main(["build", "status", "--json"]) == 0
    data = json.loads(capsys.readouterr().out)
    assert data["main"]["queue"][0]["agent"] == "bob" and data["main"]["queue"][0]["priority"] is True
    assert len(data["main"]["slots"]) == 2 and len(data["fast"]["slots"]) == 1


def test_release_frees_the_agents_stale_claim_and_ticket(
    checkout: Path, capsys: pytest.CaptureFixture[str], monkeypatch: pytest.MonkeyPatch
) -> None:
    slots = default_slots(MAIN_LANE, checkout)
    ticket = slots.join("alice", "wt", "dev", False, 6001)
    assert slots.try_claim(ticket) is not None
    slots.join("alice", "wt", "dev", False, 6002)
    monkeypatch.setattr(pcsx2_proc, "pid_alive", lambda pid: False)
    assert main(["build", "release", "--agent", "alice"]) == 0
    out = capsys.readouterr().out
    assert "released main slot 1" in out and "left the main queue" in out
    assert main(["build", "release", "--agent", "alice"]) == 2


def test_release_of_a_running_build_needs_force(checkout: Path, capsys: pytest.CaptureFixture[str]) -> None:
    slots = default_slots(MAIN_LANE, checkout)
    assert slots.try_claim(slots.join("alice", "wt", "dev", False, os.getpid())) is not None
    assert main(["build", "release", "--agent", "alice"]) == 2
    assert "still runs" in capsys.readouterr().err
    assert main(["build", "release", "--agent", "alice", "--force"]) == 0
