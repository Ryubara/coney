# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for the `coney-tools build` command line (coney_tools.build_cli): its flags, the precheck, the printed summary
and exit codes, `build status` and `build release`. The toolchain is a fake; the claims are real folders in a
temporary directory."""

import json
import os
import re
import subprocess
import sys
from collections.abc import Callable, Sequence
from pathlib import Path
from typing import Any, ClassVar

import pytest

from coney_tools import build_cli, build_run, compiler_cache, pcsx2_proc
from coney_tools.build_claims import FAST_LANE, MAIN_LANE, default_slots, folder_lock
from coney_tools.cli import _build_parser, main
from coney_tools.process_lifetime import Process

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
    #: The environment the last toolchain was made with.
    env: ClassVar[dict[str, str]] = {}

    def __init__(self, checkout: Path, env: dict[str, str]) -> None:
        self.checkout = checkout
        FakeToolchain.env = dict(env)

    def capture(self, command: list[str], cwd: Path | None, timeout: float | None = None) -> tuple[int, str]:
        FakeToolchain.commands.append(command)
        return 0, "[1/2] a\n[2/2] b\n"

    def dry_run(self, directory: Path, targets: Sequence[str] = ()) -> tuple[int, str]:
        return 0, "ninja explain: output a.obj.ddi older than most recent input a.cpp (1 vs 2)\n[1/2] a\n[2/2] b\n"

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
    FakeToolchain.env = {}
    monkeypatch.setattr(build_run, "ProcessToolchain", FakeToolchain)
    monkeypatch.setattr(build_run, "load_msvc_environment", lambda base, run: dict(base))
    monkeypatch.setattr(build_run, "find_programs", lambda env: None)
    # No compiler cache unless a test gives one: the machine's own ccache must not change what a test sees.
    monkeypatch.setattr(compiler_cache, "find", lambda root: NO_CACHE)
    TIED.clear()
    monkeypatch.setattr(build_cli, "_tie_to_caller", TIED.append)
    return FakeToolchain


#: The compiler cache of a machine without ccache.
NO_CACHE = compiler_cache.CompilerCache(None, Path("cache"), 25, "ccache not found")

#: The toolchains a run tied to its caller (the real tie binds the test process itself, so the tests record it).
TIED: list[object] = []


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
            "-L",
            "disc",
            "--build-dir",
            "build/tidy",
            "--env",
            "CC=clang-19",
            "--env",
            "CXX=clang++-19",
            "--priority",
            "--max-wait",
            "5",
            "--budget",
            "30",
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
    assert (args.label, args.build_dir, args.env) == ("disc", Path("build/tidy"), ["CC=clang-19", "CXX=clang++-19"])
    defaults = _build_parser().parse_args(["build", "--agent", "a"])
    assert (defaults.preset, defaults.max_wait, defaults.test, defaults.priority) == ("dev", None, False, False)
    assert (defaults.detach, defaults.log_file, defaults.result_file) == (False, None, None)
    assert (defaults.label, defaults.build_dir, defaults.env) == (None, None, [])
    alias = _build_parser().parse_args(["build", "--agent", "a", "--slots-wait-timeout", "7"])
    assert alias.max_wait == 7.0
    assert (args.budget, defaults.budget) == (30.0, None)  # None: the caps (build_cli.limits)


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
    log = Path(lines[0].removeprefix("log: "))
    assert lines[0].startswith("log: ")
    assert log.parent == tmp_path / "scratch" / "wt" and re.fullmatch(r"build-dev-alice-\d{8}-\d{6}.*\.log", log.name)
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


def log_of(out: str) -> Path:
    """The log path a run printed first."""
    first = out.splitlines()[0]
    assert first.startswith("log: ")
    return Path(first[len("log: ") :])


def test_every_run_has_a_log_of_its_own(
    checkout: Path, capsys: pytest.CaptureFixture[str], fake_toolchain: type[FakeToolchain]
) -> None:
    fake_toolchain.outputs["build"] = (0, "[1/1] Building one\n")
    assert main(["build", "--agent", "alice", "--configure"]) == 0
    first = log_of(capsys.readouterr().out)
    fake_toolchain.outputs["build"] = (0, "[1/1] Building two\n")
    assert main(["build", "--agent", "alice", "--configure"]) == 0
    second = log_of(capsys.readouterr().out)
    assert first != second  # even in the same second, by the same agent
    assert "Building one" in first.read_text(encoding="utf-8") and "Building two" in second.read_text(encoding="utf-8")
    assert "\x00" not in first.read_text(encoding="utf-8")
    assert main(["build", "--agent", "bob", "--configure"]) == 0
    assert "bob" in log_of(capsys.readouterr().out).name


def test_old_logs_are_pruned_to_the_newest_few(
    checkout: Path, tmp_path: Path, capsys: pytest.CaptureFixture[str], fake_toolchain: type[FakeToolchain]
) -> None:
    folder = tmp_path / "scratch" / "wt"
    folder.mkdir(parents=True)
    for number in range(30):
        old = folder / f"build-dev-alice-20200101-0000{number:02d}.log"
        old.write_text("old", encoding="utf-8")
        os.utime(old, (1_000_000 + number, 1_000_000 + number))
    (folder / "notes.md").write_text("keep", encoding="utf-8")
    assert main(["build", "--agent", "alice", "--configure"]) == 0
    logs = sorted(p.name for p in folder.glob("build-*.log"))
    assert len(logs) == 20 and "build-dev-alice-20200101-000000.log" not in logs
    assert (folder / "notes.md").is_file()


def test_a_second_run_in_the_same_folder_waits_and_then_gives_up(
    checkout: Path,
    capsys: pytest.CaptureFixture[str],
    fake_toolchain: type[FakeToolchain],
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    monkeypatch.setattr("time.sleep", lambda seconds: None)
    clock = iter(range(10**6))
    monkeypatch.setattr("time.time", lambda: 1_800_000_000.0 + next(clock))
    lock = folder_lock(checkout / "build" / "dev", checkout)
    assert lock.try_claim(lock.join("bob", "wt", "dev", False, 4001)) is not None
    assert main(["build", "--agent", "alice", "--configure", "--max-wait", "3"]) == 75
    out = capsys.readouterr().out
    assert "waiting for another run in this build folder" in out and "bob" in out
    assert "result: QUEUED at build folder (exit 75)" in out
    assert fake_toolchain.commands == []  # nothing ran, in this folder or any
    # Another preset is another folder, so it is not held up.
    assert main(["build", "--agent", "alice", "--configure", "--preset", "ci", "--max-wait", "3"]) == 0


def test_a_run_frees_its_folder_when_it_ends(
    checkout: Path, fake_toolchain: type[FakeToolchain], capsys: pytest.CaptureFixture[str]
) -> None:
    assert main(["build", "--agent", "alice", "--configure"]) == 0
    lock = folder_lock(checkout / "build" / "dev", checkout)
    assert all(row.claim is None for row in lock.status().slots) and lock.status().queue == []


def test_a_label_is_passed_to_ctest(
    checkout: Path, capsys: pytest.CaptureFixture[str], fake_toolchain: type[FakeToolchain]
) -> None:
    fake_toolchain.outputs["build"] = (0, "[1/1] x\n")
    assert main(["build", "--agent", "alice", "--configure", "--test", "-L", "disc", "-R", "level80"]) == 0
    ctest = [c for c in fake_toolchain.commands if c[0] == "ctest" and "-N" not in c]
    assert ctest and ctest[0][-4:] == ["-R", "level80", "-L", "disc"]


def test_another_build_folder_is_configured_with_the_given_compilers(
    checkout: Path, capsys: pytest.CaptureFixture[str], fake_toolchain: type[FakeToolchain]
) -> None:
    code = main(
        [
            "build",
            "--agent",
            "alice",
            "--configure-only",
            "--build-dir",
            "build/tidy",
            "--env",
            "CC=clang-19",
            "--env",
            "CXX=clang++-19",
        ]
    )
    assert code == 0
    assert fake_toolchain.commands == [["cmake", "--preset", "dev", "-B", str(checkout / "build" / "tidy")]]
    assert fake_toolchain.env["CC"] == "clang-19" and fake_toolchain.env["CXX"] == "clang++-19"
    assert "-tidy-" in log_of(capsys.readouterr().out).name  # logs of the two folders are told apart


def test_a_build_folder_with_a_cache_is_not_configured_again(
    checkout: Path, capsys: pytest.CaptureFixture[str], fake_toolchain: type[FakeToolchain]
) -> None:
    (checkout / "build" / "tidy").mkdir(parents=True)
    (checkout / "build" / "tidy" / "CMakeCache.txt").write_text("", encoding="utf-8")
    assert main(["build", "--agent", "alice", "--build-dir", "build/tidy"]) == 0
    assert [c for c in fake_toolchain.commands if c[0] == "cmake"] == [
        ["cmake", "--build", str(checkout / "build" / "tidy"), "-j", fake_toolchain.commands[-1][-1]]
    ]


def test_an_env_setting_replaces_a_variable_whatever_its_case(
    checkout: Path,
    capsys: pytest.CaptureFixture[str],
    fake_toolchain: type[FakeToolchain],
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    monkeypatch.setattr(build_run, "load_msvc_environment", lambda base, run: {"PATH": "x", "Cc": "gcc"})
    assert main(["build", "--agent", "a", "--configure", "--env", "CC=clang"]) == 0
    assert fake_toolchain.env == {"PATH": "x", "CC": "clang", "CCACHE_DISABLE": "1"}


@pytest.mark.parametrize("setting", ["CC", "=x", "1BAD=x", "A B=x", ""])
def test_a_malformed_env_setting_is_refused_before_anything_runs(
    checkout: Path, capsys: pytest.CaptureFixture[str], fake_toolchain: type[FakeToolchain], setting: str
) -> None:
    assert main(["build", "--agent", "a", "--env", setting]) == 2
    assert "--env" in capsys.readouterr().err and fake_toolchain.commands == []


def test_a_run_records_its_step_durations_beside_the_claims(
    checkout: Path, tmp_path: Path, fake_toolchain: type[FakeToolchain]
) -> None:
    assert main(["build", "--agent", "alice", "--configure"]) == 0
    durations = json.loads((tmp_path / "claims" / "durations.json").read_text(encoding="utf-8"))
    assert sorted(durations) == ["dev/build/main", "dev/configure/main"]


def test_the_budget_reaches_the_run(
    checkout: Path, monkeypatch: pytest.MonkeyPatch, fake_toolchain: type[FakeToolchain]
) -> None:
    seen: list[float] = []
    real = build_run.execute

    def spy(options: build_run.Options, context: build_run.Context) -> build_run.Result:
        seen.append(options.budget)
        assert context.history is not None
        return real(options, context)

    monkeypatch.setattr(build_run, "execute", spy)
    assert main(["build", "--agent", "alice", "--budget", "20"]) == 0
    assert seen == [20.0]


def test_a_run_ties_its_toolchain_to_the_caller_before_running(
    checkout: Path, fake_toolchain: type[FakeToolchain]
) -> None:
    assert main(["build", "--agent", "a", "--preset", "dev"]) == 0
    assert len(TIED) == 1 and isinstance(TIED[0], FakeToolchain)


def test_when_the_caller_is_gone_the_run_stops_its_child_and_ends(capsys: pytest.CaptureFixture[str]) -> None:
    calls: list[str] = []

    class Toolchain:
        def stop(self) -> None:
            calls.append("stop")

    build_cli._caller_gone(
        Toolchain(),  # type: ignore[arg-type]
        Process(20, 10, "bash", None),
        interrupt=lambda: calls.append("interrupt"),
        exit_later=lambda: calls.append("exit later"),
    )
    assert calls == ["stop", "interrupt", "exit later"]
    assert "bash (pid 20) that started this build has ended" in capsys.readouterr().err


# The caps that keep an agent from waiting past its prompt cache, and the detached run
# (docs/guides/agents.md#build-waits).


def test_a_wait_past_the_cap_is_refused_unless_detached(
    checkout: Path, capsys: pytest.CaptureFixture[str], fake_toolchain: type[FakeToolchain]
) -> None:
    for flags in (["--max-wait", "41"], ["--budget", "46"], ["--budget", "0"]):
        assert main(["build", "--agent", "alice", *flags]) == 2
        err = capsys.readouterr().err
        assert "--detach" in err and "STATUS.md" in err and "prompt cache" in err
    assert fake_toolchain.commands == []
    assert main(["build", "--agent", "alice", "--max-wait", "40", "--budget", "45"]) == 0


def test_the_limits_default_to_the_caps_and_widen_when_detached() -> None:
    parse = _build_parser().parse_args
    assert build_cli.limits(parse(["build", "--agent", "a"])) == (40.0, 45.0)
    assert build_cli.limits(parse(["build", "--agent", "a", "--detach"])) == (build_cli.DETACHED_MAX_WAIT, 0.0)
    assert build_cli.limits(parse(["build", "--agent", "a", "--detach", "--max-wait", "90", "--budget", "100"])) == (
        90.0,
        100.0,
    )


@pytest.fixture
def spawned(monkeypatch: pytest.MonkeyPatch) -> list[tuple[list[str], Path, Path]]:
    """The detached children a run started, recorded instead of started: (command, folder, output file)."""
    calls: list[tuple[list[str], Path, Path]] = []

    def spawn(command: list[str], cwd: Path, output: Path) -> int:
        calls.append((command, cwd, output))
        return 4242

    monkeypatch.setattr(build_cli, "spawn_detached", spawn)
    return calls


def test_detach_starts_the_run_in_the_background_and_returns_at_once(
    checkout: Path,
    tmp_path: Path,
    capsys: pytest.CaptureFixture[str],
    fake_toolchain: type[FakeToolchain],
    spawned: list[tuple[list[str], Path, Path]],
) -> None:
    assert main(["build", "--agent", "alice", "--test", "-L", "disc", "--detach", "--max-wait", "120"]) == 0
    out = capsys.readouterr().out
    assert fake_toolchain.commands == [] and TIED == []  # nothing built here, and nothing tied to this shell
    ((command, cwd, output),) = spawned
    assert cwd == checkout and output.parent == tmp_path / "scratch" / "wt"
    assert "--detach" not in command and command[command.index("build") - 2 :][:2] == ["-m", "coney_tools.cli"]
    assert command[command.index("--max-wait") + 1] == "120.0" and command[command.index("--budget") + 1] == "0.0"
    assert "--test" in command and command[command.index("-L") + 1] == "disc"
    result = Path(command[command.index("--result-file") + 1])
    assert result == tmp_path / "scratch" / "wt" / "build-result.json"
    record = json.loads(result.read_text(encoding="utf-8"))
    assert (record["state"], record["agent"], record["preset"]) == ("running", "alice", "dev")
    assert record["log"] == command[command.index("--log-file") + 1] and Path(record["log"]).is_file()
    assert "4242" in out and "coney-tools build result" in out and "STATUS.md" in out


def test_a_second_detached_run_is_refused_while_one_runs(
    checkout: Path,
    capsys: pytest.CaptureFixture[str],
    fake_toolchain: type[FakeToolchain],
    spawned: list[tuple[list[str], Path, Path]],
) -> None:
    assert main(["build", "--agent", "alice", "--detach"]) == 0
    command = spawned[0][0]
    record = Path(command[command.index("--result-file") + 1])
    data = json.loads(record.read_text(encoding="utf-8"))
    record.write_text(json.dumps({**data, "pid": 4242}), encoding="utf-8")  # the child has started
    capsys.readouterr()
    assert main(["build", "--agent", "alice", "--detach"]) == 2
    assert "still running" in capsys.readouterr().err and len(spawned) == 1


def test_the_detached_child_records_its_result_for_a_successor(
    checkout: Path,
    tmp_path: Path,
    capsys: pytest.CaptureFixture[str],
    fake_toolchain: type[FakeToolchain],
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    lifetimes: list[str] = []
    monkeypatch.setattr(build_cli, "_detached_lifetime", lambda: lifetimes.append("own"))
    fake_toolchain.outputs["build"] = (1, "C:\a.cpp(3): error C2065: 'x': undeclared identifier\n")
    folder = tmp_path / "scratch" / "wt"
    folder.mkdir(parents=True)
    log, result = folder / "build-dev-alice-x.log", folder / "build-result.json"
    log.write_text("", encoding="utf-8")
    argv = ["build", "--agent", "alice", "--configure", "--max-wait", "120", "--budget", "0"]
    assert main([*argv, "--log-file", str(log), "--result-file", str(result)]) == 1
    assert lifetimes == ["own"] and TIED == []  # its own job, but no tie to a caller that has long gone
    record = json.loads(result.read_text(encoding="utf-8"))
    assert (record["state"], record["exit_code"], record["pid"]) == ("done", 1, os.getpid())
    assert any("C2065" in line for line in record["summary"]) and "C2065" in log.read_text(encoding="utf-8")
    capsys.readouterr()
    assert main(["build", "result"]) == 1
    out = capsys.readouterr().out
    assert "alice" in out and "C2065" in out and "exit 1" in out and str(log) in out


def test_a_detached_child_that_fails_its_precheck_still_records_it(
    checkout: Path,
    tmp_path: Path,
    fake_toolchain: type[FakeToolchain],
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    monkeypatch.setattr(build_cli, "_detached_lifetime", lambda: None)
    result = tmp_path / "scratch" / "wt" / "build-result.json"
    log = tmp_path / "scratch" / "wt" / "x.log"
    assert (
        main(["build", "--agent", "alice", "--preset", "asan", "--log-file", str(log), "--result-file", str(result)])
        == 2
    )
    record = json.loads(result.read_text(encoding="utf-8"))
    assert record["state"] == "done" and record["exit_code"] == 2 and "asan" in "\n".join(record["summary"])


def write_record(checkout: Path, tmp_path: Path, **fields: object) -> None:
    """A build-result.json in the checkout's scratch folder."""
    folder = tmp_path / "scratch" / "wt"
    folder.mkdir(parents=True, exist_ok=True)
    record = {"state": "running", "agent": "alice", "preset": "dev", "log": "x.log", "started": 1_800_000_000.0}
    (folder / "build-result.json").write_text(json.dumps({**record, **fields}), encoding="utf-8")


def test_result_without_a_detached_run(checkout: Path, capsys: pytest.CaptureFixture[str]) -> None:
    assert main(["build", "result"]) == 1
    assert "no detached build" in capsys.readouterr().out


def test_result_of_a_run_still_going_exits_75(
    checkout: Path, tmp_path: Path, capsys: pytest.CaptureFixture[str]
) -> None:
    write_record(checkout, tmp_path, pid=4242)
    assert main(["build", "result"]) == 75
    out = capsys.readouterr().out
    assert "still running" in out and "4242" in out and "x.log" in out


def test_result_of_a_run_that_died_without_a_word(
    checkout: Path, tmp_path: Path, capsys: pytest.CaptureFixture[str], monkeypatch: pytest.MonkeyPatch
) -> None:
    monkeypatch.setattr(pcsx2_proc, "pid_alive", lambda pid: False)
    write_record(checkout, tmp_path, pid=4242)
    assert main(["build", "result"]) == 1
    assert "ended without a result" in capsys.readouterr().out


def test_result_as_json(checkout: Path, tmp_path: Path, capsys: pytest.CaptureFixture[str]) -> None:
    write_record(checkout, tmp_path, state="done", pid=1, exit_code=0, summary=["result: ok"], ended=1_800_000_600.0)
    assert main(["build", "result", "--json"]) == 0
    assert json.loads(capsys.readouterr().out)["summary"] == ["result: ok"]


# -- --target and the compiler cache ------------------------------------------------------------------------------


def test_targets_reach_the_build_command_and_the_detached_child(
    checkout: Path,
    capsys: pytest.CaptureFixture[str],
    fake_toolchain: type[FakeToolchain],
    spawned: list[tuple[list[str], Path, Path]],
) -> None:
    assert main(["build", "--agent", "alice", "--configure", "--target", "coney", "--target", "coney_tests"]) == 0
    build = next(c for c in fake_toolchain.commands if "--build" in c)
    assert build[build.index("--target") :][:3] == ["--target", "coney", "coney_tests"]
    assert "build coney coney_tests: ok" in capsys.readouterr().out
    assert main(["build", "--agent", "alice", "--detach", "--target", "coney"]) == 0
    command = spawned[0][0]
    assert command[command.index("--target") + 1] == "coney"


@pytest.mark.parametrize("flag", ["--test", "--configure-only"])
def test_a_target_with_tests_or_a_configure_only_run_is_refused(
    checkout: Path, capsys: pytest.CaptureFixture[str], fake_toolchain: type[FakeToolchain], flag: str
) -> None:
    assert main(["build", "--agent", "alice", "--target", "coney", flag]) == 2
    assert "--target with" in capsys.readouterr().err and fake_toolchain.commands == []


def test_the_build_gets_the_cache_environment_and_reports_its_hits(
    checkout: Path,
    tmp_path: Path,
    capsys: pytest.CaptureFixture[str],
    fake_toolchain: type[FakeToolchain],
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    program = tmp_path / "ccache.exe"
    program.write_text("", encoding="utf-8")
    cache = compiler_cache.CompilerCache(program, tmp_path / "cache", 25)
    monkeypatch.setattr(compiler_cache, "find", lambda root: cache)
    captured: list[list[str]] = []

    def capture(
        self: FakeToolchain, command: list[str], cwd: Path | None, timeout: float | None = None
    ) -> tuple[int, str]:
        captured.append(command)
        if command[0] == str(program):
            return 0, "direct_cache_hit\t9\ncache_miss\t1\n"
        return 0, "[1/2] a\n[2/2] b\n"

    def run(self: FakeToolchain, command: list[str], on_line: Callable[[str], None]) -> int:
        FakeToolchain.commands.append(command)
        Path(FakeToolchain.env["CCACHE_STATSLOG"]).write_text("# ccache\n", encoding="utf-8")  # ccache writes it
        return 0

    monkeypatch.setattr(FakeToolchain, "capture", capture)
    monkeypatch.setattr(FakeToolchain, "run", run)
    assert main(["build", "--agent", "alice", "--configure"]) == 0
    env = FakeToolchain.env
    assert env["CMAKE_CXX_COMPILER_LAUNCHER"] == program.as_posix() and env["CCACHE_BASEDIR"] == str(checkout)
    stats_log = Path(env["CCACHE_STATSLOG"])
    assert stats_log.suffix == ".ccache-stats" and stats_log.parent == tmp_path / "scratch" / "wt"
    assert [str(program), "--print-log-stats"] in captured and not stats_log.exists()  # read, then removed
    assert "compiler cache: 9 hits, 1 misses (90.0 %)" in capsys.readouterr().out


def test_the_cache_line_says_why_it_is_off_or_not_used(tmp_path: Path) -> None:
    toolchain = FakeToolchain(tmp_path, {})
    off = compiler_cache.CompilerCache(None, tmp_path, 25, "ccache not found (x)")
    assert (
        build_cli.cache_line(off, tmp_path, tmp_path / "s", toolchain) == "compiler cache: off (ccache not found (x))"
    )
    zero = compiler_cache.CompilerCache(tmp_path / "ccache", tmp_path, 0)
    assert (
        build_cli.cache_line(zero, tmp_path, tmp_path / "s", toolchain) == "compiler cache: off (ccache_size_gb is 0)"
    )
    on = compiler_cache.CompilerCache(tmp_path / "ccache", tmp_path, 25)
    folder = tmp_path / "dev"
    folder.mkdir()
    (folder / "CMakeCache.txt").write_text("CMAKE_BUILD_TYPE:STRING=Debug\n", encoding="utf-8")
    line = build_cli.cache_line(on, folder, tmp_path / "s", toolchain)
    assert line is not None and "not used" in line and "delete the folder" in line
    (folder / "CMakeCache.txt").write_text("CMAKE_CXX_COMPILER_LAUNCHER:STRING=ccache\n", encoding="utf-8")
    assert build_cli.cache_line(on, folder, tmp_path / "s", toolchain) is None  # nothing compiled


def test_the_cache_line_says_when_ccache_cannot_give_the_hits(tmp_path: Path) -> None:
    # An older ccache (before 4.10) has no --print-log-stats: the build still used the cache, so say so.
    on = compiler_cache.CompilerCache(tmp_path / "ccache", tmp_path, 25)
    stats_log = tmp_path / "run.ccache-stats"
    stats_log.write_text("cache_miss\n", encoding="utf-8")
    line = build_cli.cache_line(on, tmp_path, stats_log, FakeToolchain(tmp_path, {}))
    assert line is not None and "hits unavailable" in line and compiler_cache.MIN_VERSION in line
    assert not stats_log.exists()


def test_the_stats_log_is_removed_however_the_run_ends(tmp_path: Path) -> None:
    stats_log = tmp_path / "run.ccache-stats"
    stats_log.write_text("x", encoding="utf-8")
    with pytest.raises(RuntimeError), build_cli._removed_after(stats_log):
        raise RuntimeError("configure failed")
    assert not stats_log.exists()


# The Windows creation flags of the detached child: it must not open a console window of its own.


DETACHED_PROCESS = 0x00000008  # the flag that leaves a child with no console, which Windows Terminal then replaces


def test_the_detached_child_on_windows_has_no_window_and_a_group_of_its_own(monkeypatch: pytest.MonkeyPatch) -> None:
    seen: list[int] = []

    class FakePopen:
        pid = 7

        def __init__(self, command: list[str], **kwargs: Any) -> None:
            seen.append(kwargs["creationflags"])
            if kwargs["creationflags"] & build_cli.CREATE_BREAKAWAY_FROM_JOB:
                raise OSError("the job forbids leaving it")

    monkeypatch.setattr(sys, "platform", "win32")
    monkeypatch.setattr(subprocess, "Popen", FakePopen)
    assert build_cli.spawn_detached(["x"], Path(), Path(os.devnull)) == 7
    # First try leaves the job, then the retry stays in it; both hide the console and keep Ctrl+C away.
    assert len(seen) == 2 and seen[0] == seen[1] | build_cli.CREATE_BREAKAWAY_FROM_JOB
    for flags in seen:
        assert flags & build_cli.CREATE_NO_WINDOW
        assert flags & build_cli.CREATE_NEW_PROCESS_GROUP
        assert not flags & DETACHED_PROCESS  # DETACHED_PROCESS makes Windows Terminal show a new window
