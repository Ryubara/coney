# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for the `coney-tools build` command line (coney_tools.build_cli): its flags, the precheck, the printed summary
and exit codes, `build status` and `build unclaim`. The toolchain is a fake; the claims are real folders in a
temporary directory."""

import getpass
import hashlib
import json
import os
import re
import subprocess
import sys
from collections.abc import Callable, Sequence
from pathlib import Path
from typing import Any, ClassVar

import pytest

from coney_tools import build_cli, build_record, build_root, build_run, compiler_cache, pcsx2_proc, process_lifetime
from coney_tools.build_claims import FAST_LANE, MAIN_LANE, default_slots, folder_lock
from coney_tools.cli import _build_parser, main
from coney_tools.config import ConfigError
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
            "--full",
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
    assert (args.full, _build_parser().parse_args(["build", "--agent", "a"]).full) == (True, False)
    assert (args.label, args.build_dir, args.env) == ("disc", Path("build/tidy"), ["CC=clang-19", "CXX=clang++-19"])
    defaults = _build_parser().parse_args(["build", "--agent", "a"])
    assert (defaults.preset, defaults.max_wait, defaults.test, defaults.priority) == ("dev", None, False, False)
    assert (defaults.detach, defaults.log_file, defaults.result_file) == (False, None, None)
    assert (defaults.label, defaults.build_dir, defaults.env) == (None, None, [])
    assert (args.budget, defaults.budget) == (30.0, None)  # None: the caps (build_cli.limits)


def test_status_and_unclaim_are_subcommands() -> None:
    parser = _build_parser()
    assert parser.parse_args(["build", "status", "--json"]).build_command == "status"
    release = parser.parse_args(["build", "unclaim", "--agent", "a", "--force", "--slot", "2", "--lane", "fast"])
    assert (release.build_command, release.release_agent, release.force, release.slot, release.lane) == (
        "unclaim",
        "a",
        True,
        2,
        "fast",
    )


def test_dir_is_accepted_after_the_subcommand_and_before_it() -> None:
    parser = _build_parser()
    for name in ("status", "result"):
        after = parser.parse_args(["build", name, "--dir", "/some/worktree"])
        before = parser.parse_args(["build", "--dir", "/some/worktree", name])
        assert after.dir == before.dir == Path("/some/worktree")
    release = parser.parse_args(["build", "unclaim", "--agent", "a", "--dir", "/some/worktree"])
    assert release.dir == Path("/some/worktree")
    # Not given after the subcommand: the value given before it is kept, not reset to None.
    assert parser.parse_args(["build", "--dir", "/w", "result", "--json"]).dir == Path("/w")
    assert parser.parse_args(["build", "result"]).dir is None


def test_result_dir_reads_another_worktrees_record(
    checkout: Path, tmp_path: Path, capsys: pytest.CaptureFixture[str]
) -> None:
    other = tmp_path / "work" / "worktrees" / "coney" / "other"
    other.mkdir(parents=True)
    (other / "coney.local.example.toml").write_text("", encoding="utf-8")
    (other / "coney.local.toml").write_text(
        (checkout / "coney.local.toml").read_text(encoding="utf-8"), encoding="utf-8"
    )
    folder = tmp_path / "scratch" / "other"
    folder.mkdir(parents=True)
    record = {"state": "done", "agent": "bob", "preset": "dev", "log": "y.log", "started": 1.0, "exit_code": 0}
    (folder / "build-result.json").write_text(json.dumps({**record, "summary": ["result: ok"]}), encoding="utf-8")
    assert main(["build", "result", "--dir", str(other)]) == 0
    assert "bob" in capsys.readouterr().out


def test_an_agent_must_name_itself(
    checkout: Path, capsys: pytest.CaptureFixture[str], monkeypatch: pytest.MonkeyPatch
) -> None:
    # Inside an agent tool the id names whose build a slot or log is, so it stays required there. Under pytest the
    # output is captured, as any agent tool captures it, so no terminal is seen either.
    monkeypatch.setenv("CLAUDECODE", "1")
    assert main(["build"]) == 2
    assert "--agent" in capsys.readouterr().err
    monkeypatch.delenv("CLAUDECODE")
    assert main(["build"]) == 2
    assert "--agent" in capsys.readouterr().err


def test_a_person_in_a_terminal_gets_a_default_id(monkeypatch: pytest.MonkeyPatch) -> None:
    terminal = lambda: True  # noqa: E731 -- a one-line stand-in for a terminal
    monkeypatch.setattr(getpass, "getuser", lambda: "Jane Doe")
    assert build_cli.default_agent_id(terminal) == "Jane-Doe"
    monkeypatch.setattr(getpass, "getuser", lambda: "???")
    assert build_cli.default_agent_id(terminal) == "me"

    def no_user() -> str:
        raise OSError("no user name")

    monkeypatch.setattr(getpass, "getuser", no_user)
    assert build_cli.default_agent_id(terminal) == "me"
    # An agent: Claude Code's variable, or any tool that captures the output (not a terminal).
    assert build_cli.default_agent_id(lambda: False) is None
    monkeypatch.setenv("CLAUDECODE", "1")
    assert build_cli.default_agent_id(terminal) is None


def test_an_unknown_preset_is_refused_before_any_slot_is_taken(
    checkout: Path, capsys: pytest.CaptureFixture[str], fake_toolchain: type[FakeToolchain]
) -> None:
    (checkout / "CMakePresets.json").write_text(json.dumps({**PRESETS, "buildPresets": []}), encoding="utf-8")
    assert main(["build", "--agent", "a", "--preset", "asan"]) == 2
    assert "asan" in capsys.readouterr().err
    assert fake_toolchain.commands == []


def test_an_agents_unfiltered_test_run_is_refused_and_names_the_rule(
    checkout: Path, capsys: pytest.CaptureFixture[str], fake_toolchain: type[FakeToolchain]
) -> None:
    # The full suite is 15 to 40 minutes of a main slot: an agent iterating runs the tests it touched, and the full
    # suite is the handover tip's run (`--full`, or `task check`, which runs it itself).
    assert main(["build", "--agent", "alice", "--test"]) == 2
    err = capsys.readouterr().err
    assert "-R" in err and "-L" in err and "--full" in err and "task check" in err
    assert "docs/guides/testing.md" in err
    assert fake_toolchain.commands == []
    snapshot = default_slots(MAIN_LANE, checkout).status()
    assert all(row.claim is None for row in snapshot.slots) and snapshot.queue == []


@pytest.mark.parametrize("filters", [["-R", "Widget"], ["-L", "disc"], ["-R", "Widget", "-L", "disc"]])
def test_a_filtered_test_run_by_an_agent_is_allowed(
    checkout: Path, fake_toolchain: type[FakeToolchain], filters: list[str]
) -> None:
    assert main(["build", "--agent", "alice", "--test", *filters]) == 0
    assert fake_toolchain.commands[-1][0] == "ctest"


def test_full_lets_an_agent_run_the_whole_suite_and_the_record_says_so(
    checkout: Path, tmp_path: Path, fake_toolchain: type[FakeToolchain]
) -> None:
    git_checkout(checkout)
    fake_toolchain.outputs["test"] = (0, PASSING_TESTS)
    assert main(["build", "--agent", "alice", "--test", "--full"]) == 0
    ctest = fake_toolchain.commands[-1]
    assert ctest[0] == "ctest" and "-R" not in ctest and "-L" not in ctest
    [path] = records(tmp_path)
    record = build_record.read(path.parent, path.stem)
    assert "--full" in record["command_line"] and record["filters"] == []


@pytest.mark.parametrize(
    ("flags", "message"),
    [(["--full"], "--full needs --test"), (["--test", "--full", "-R", "Widget"], "--full runs every test")],
)
def test_full_with_no_test_run_or_with_a_filter_is_refused(
    checkout: Path,
    capsys: pytest.CaptureFixture[str],
    fake_toolchain: type[FakeToolchain],
    flags: list[str],
    message: str,
) -> None:
    assert main(["build", "--agent", "alice", *flags]) == 2
    assert message in capsys.readouterr().err and fake_toolchain.commands == []


def test_a_person_in_a_terminal_may_run_the_whole_suite_without_full(
    checkout: Path, fake_toolchain: type[FakeToolchain], monkeypatch: pytest.MonkeyPatch
) -> None:
    # No --agent: the id comes from the user name only for a person at a terminal, and that run is not an agent's.
    monkeypatch.setattr(build_cli, "default_agent_id", lambda: "jane")
    assert main(["build", "--test"]) == 0
    assert fake_toolchain.commands[-1][0] == "ctest"


def test_a_detached_unfiltered_run_is_refused_before_it_starts_and_full_reaches_the_child(
    checkout: Path, capsys: pytest.CaptureFixture[str], spawned: list[tuple[list[str], Path, Path]]
) -> None:
    assert main(["build", "--agent", "alice", "--test", "--detach"]) == 2
    assert "--full" in capsys.readouterr().err and spawned == []
    assert main(["build", "--agent", "alice", "--test", "--full", "--detach"]) == 0
    ((command, _, _),) = spawned
    assert "--test" in command and "--full" in command


def test_a_successful_build_prints_the_log_first_then_a_short_summary(
    checkout: Path, tmp_path: Path, capsys: pytest.CaptureFixture[str], fake_toolchain: type[FakeToolchain]
) -> None:
    fake_toolchain.outputs["build"] = (0, "[1/2] Building a\n[2/2] Linking b\n")
    fake_toolchain.outputs["test"] = (
        0,
        "1/1 Test #1: a ... Passed 0.1 sec\n100% tests passed, 0 tests failed out of 1\n",
    )
    assert main(["build", "--agent", "alice", "--configure", "--test", "--full"]) == 0
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
    assert "tidy" not in data  # the clang-tidy lane went with the local gate


def test_release_frees_the_agents_stale_claim_and_ticket(
    checkout: Path, capsys: pytest.CaptureFixture[str], monkeypatch: pytest.MonkeyPatch
) -> None:
    slots = default_slots(MAIN_LANE, checkout)
    ticket = slots.join("alice", "wt", "dev", False, 6001)
    assert slots.try_claim(ticket) is not None
    slots.join("alice", "wt", "dev", False, 6002)
    monkeypatch.setattr(pcsx2_proc, "pid_alive", lambda pid: False)
    assert main(["build", "unclaim", "--agent", "alice"]) == 0
    out = capsys.readouterr().out
    assert "released main slot 1" in out and "left the main queue" in out
    assert main(["build", "unclaim", "--agent", "alice"]) == 2


def test_release_of_a_running_build_needs_force(checkout: Path, capsys: pytest.CaptureFixture[str]) -> None:
    slots = default_slots(MAIN_LANE, checkout)
    assert slots.try_claim(slots.join("alice", "wt", "dev", False, os.getpid())) is not None
    assert main(["build", "unclaim", "--agent", "alice"]) == 2
    assert "still runs" in capsys.readouterr().err
    assert main(["build", "unclaim", "--agent", "alice", "--force"]) == 0


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


def stub_started(monkeypatch: pytest.MonkeyPatch, started: float | None) -> None:
    """The process a recorded pid names now was started at `started` (None: its start cannot be read)."""
    monkeypatch.setattr(process_lifetime, "process_started", lambda pid: started)


def test_result_of_a_run_whose_pid_now_belongs_to_another_process(
    checkout: Path, tmp_path: Path, capsys: pytest.CaptureFixture[str], monkeypatch: pytest.MonkeyPatch
) -> None:
    write_record(checkout, tmp_path, pid=4242, process_started=1_700_000_000.0)
    stub_started(monkeypatch, 1_700_090_000.0)  # the pid is alive, but its process began a day later
    assert main(["build", "result"]) == 1
    assert "ended without a result" in capsys.readouterr().out


def test_result_of_a_run_whose_pid_is_still_its_own_process(
    checkout: Path, tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    write_record(checkout, tmp_path, pid=4242, process_started=1_700_000_000.0)
    stub_started(monkeypatch, 1_700_000_001.0)  # POSIX reports whole seconds
    assert main(["build", "result"]) == 75


def test_a_start_time_that_cannot_be_read_counts_as_running(
    checkout: Path, tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    write_record(checkout, tmp_path, pid=4242, process_started=1_700_000_000.0)
    stub_started(monkeypatch, None)
    assert main(["build", "result"]) == 75


def test_a_record_without_a_start_time_is_judged_by_the_record_time(
    checkout: Path, tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    # Records written before the start time was kept: the build process began just before the child wrote its record,
    # so a process that began long after the record is a stranger holding a reused pid.
    write_record(checkout, tmp_path, pid=4242, started=1_700_000_000.0)
    stub_started(monkeypatch, 1_700_000_005.0)
    assert main(["build", "result"]) == 75
    stub_started(monkeypatch, 1_700_500_000.0)
    assert main(["build", "result"]) == 1


def test_a_stale_record_with_a_reused_pid_does_not_refuse_a_new_detached_run(
    checkout: Path,
    tmp_path: Path,
    capsys: pytest.CaptureFixture[str],
    fake_toolchain: type[FakeToolchain],
    spawned: list[tuple[list[str], Path, Path]],
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    write_record(checkout, tmp_path, pid=4242, process_started=1_700_000_000.0)
    stub_started(monkeypatch, 1_700_090_000.0)
    assert main(["build", "--agent", "alice", "--detach"]) == 0
    assert len(spawned) == 1


def test_the_detached_child_records_when_its_process_started(
    checkout: Path,
    tmp_path: Path,
    fake_toolchain: type[FakeToolchain],
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    monkeypatch.setattr(build_cli, "_detached_lifetime", lambda: None)
    stub_started(monkeypatch, 1_700_000_123.0)
    folder = tmp_path / "scratch" / "wt"
    folder.mkdir(parents=True)
    log, result = folder / "x.log", folder / "build-result.json"
    log.write_text("", encoding="utf-8")
    main(
        [
            "build",
            "--agent",
            "alice",
            "--configure",
            "--budget",
            "0",
            "--log-file",
            str(log),
            "--result-file",
            str(result),
        ]
    )
    record = json.loads(result.read_text(encoding="utf-8"))
    assert record["process_started"] == 1_700_000_123.0


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
    assert "--lane" not in command  # the default, auto, is not spelt out


def test_lane_main_skips_the_measure_and_reaches_the_detached_child(
    checkout: Path,
    fake_toolchain: type[FakeToolchain],
    spawned: list[tuple[list[str], Path, Path]],
) -> None:
    assert main(["build", "--agent", "alice", "--lane", "main"]) == 0
    assert not any(c[0] == "ninja" and "-n" in c for c in fake_toolchain.commands)  # no dry run: nothing measured
    assert main(["build", "--agent", "alice", "--detach", "--lane", "main"]) == 0
    command = spawned[0][0]
    assert command[command.index("--lane") + 1] == "main"


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


# The Windows creation flags of the detached child: it must not open a console window of its own. The values are the
# Win32 ones, written out here so the test checks the numbers Windows sees, not the module's names for them.


CREATE_NO_WINDOW = 0x08000000
CREATE_NEW_PROCESS_GROUP = 0x00000200
CREATE_BREAKAWAY_FROM_JOB = 0x01000000
DETACHED_PROCESS = 0x00000008  # leaves the venv launcher's child with no console, which Windows Terminal then shows


def test_the_detached_child_on_windows_has_no_window_and_a_group_of_its_own(monkeypatch: pytest.MonkeyPatch) -> None:
    seen: list[int] = []

    class FakePopen:
        pid = 7

        def __init__(self, command: list[str], **kwargs: Any) -> None:
            seen.append(kwargs["creationflags"])
            if kwargs["creationflags"] & CREATE_BREAKAWAY_FROM_JOB:
                raise OSError("the job forbids leaving it")

    monkeypatch.setattr(sys, "platform", "win32")
    monkeypatch.setattr(subprocess, "Popen", FakePopen)
    assert build_cli.spawn_detached(["x"], Path(), Path(os.devnull)) == 7
    # First try leaves the job, then the retry stays in it; both hide the console and keep Ctrl+C away.
    hidden = CREATE_NO_WINDOW | CREATE_NEW_PROCESS_GROUP
    assert seen == [hidden | CREATE_BREAKAWAY_FROM_JOB, hidden]
    assert not any(flags & DETACHED_PROCESS for flags in seen)


def set_game_dir(checkout: Path, game: Path) -> None:
    """Name `game` as the checkout's game_dir in its coney.local.toml."""
    config = checkout / "coney.local.toml"
    config.write_text(config.read_text(encoding="utf-8") + f"game_dir = '{game}'\n", encoding="utf-8")


def disc_line(out: str) -> str:
    """The summary line about the disc tests of a run's output."""
    return next(line for line in out.splitlines() if line.startswith("disc tests:"))


def test_the_configured_disc_reaches_the_tests_as_coney_disc(
    checkout: Path, tmp_path: Path, capsys: pytest.CaptureFixture[str], fake_toolchain: type[FakeToolchain]
) -> None:
    game = tmp_path / "game"
    game.mkdir()
    set_game_dir(checkout, game)
    assert main(["build", "--agent", "alice", "--configure", "--test", "--full"]) == 0
    assert fake_toolchain.env["CONEY_DISC"] == str(game)
    line = disc_line(capsys.readouterr().out)
    assert "ran" in line and "game_dir" in line and "SKIPPED" not in line


def test_the_one_iso_in_the_game_folder_is_the_disc(
    checkout: Path, tmp_path: Path, capsys: pytest.CaptureFixture[str], fake_toolchain: type[FakeToolchain]
) -> None:
    game = tmp_path / "game"
    game.mkdir()
    (game / "warriors.iso").write_bytes(b"")
    set_game_dir(checkout, game)
    assert main(["build", "--agent", "alice", "--configure", "--test", "--full"]) == 0
    assert fake_toolchain.env["CONEY_DISC"] == str(game / "warriors.iso")


def test_a_disc_in_the_environment_is_kept_over_game_dir(
    checkout: Path,
    tmp_path: Path,
    capsys: pytest.CaptureFixture[str],
    fake_toolchain: type[FakeToolchain],
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    game = tmp_path / "game"
    game.mkdir()
    set_game_dir(checkout, game)
    monkeypatch.setenv("CONEY_DISC", "somewhere/else.iso")
    assert main(["build", "--agent", "alice", "--configure", "--test", "--full"]) == 0
    assert fake_toolchain.env["CONEY_DISC"] == "somewhere/else.iso"
    line = disc_line(capsys.readouterr().out)
    assert "ran" in line and "environment" in line


def test_an_env_setting_of_the_disc_is_kept_over_game_dir(
    checkout: Path, tmp_path: Path, capsys: pytest.CaptureFixture[str], fake_toolchain: type[FakeToolchain]
) -> None:
    game = tmp_path / "game"
    game.mkdir()
    set_game_dir(checkout, game)
    assert main(["build", "--agent", "alice", "--configure", "--test", "--full", "--env", "CONEY_DISC=mine.iso"]) == 0
    assert fake_toolchain.env["CONEY_DISC"] == "mine.iso"
    assert "environment" in disc_line(capsys.readouterr().out)


def test_an_empty_disc_in_the_environment_counts_as_not_set(
    checkout: Path,
    tmp_path: Path,
    capsys: pytest.CaptureFixture[str],
    fake_toolchain: type[FakeToolchain],
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    game = tmp_path / "game"
    game.mkdir()
    set_game_dir(checkout, game)
    monkeypatch.setenv("CONEY_DISC", "")  # the tests read an empty value as not set
    assert main(["build", "--agent", "alice", "--configure", "--test", "--full"]) == 0
    assert fake_toolchain.env["CONEY_DISC"] == str(game)


def test_without_a_disc_the_summary_says_the_disc_tests_were_skipped(
    checkout: Path, capsys: pytest.CaptureFixture[str], fake_toolchain: type[FakeToolchain]
) -> None:
    assert main(["build", "--agent", "alice", "--configure", "--test", "--full"]) == 0
    assert "CONEY_DISC" not in fake_toolchain.env
    line = disc_line(capsys.readouterr().out)
    assert "SKIPPED" in line and "game_dir" in line and "CONEY_DISC" in line


def test_a_game_dir_that_does_not_exist_is_not_passed_on(
    checkout: Path, tmp_path: Path, capsys: pytest.CaptureFixture[str], fake_toolchain: type[FakeToolchain]
) -> None:
    set_game_dir(checkout, tmp_path / "no-such-folder")
    assert main(["build", "--agent", "alice", "--configure", "--test", "--full"]) == 0
    assert "CONEY_DISC" not in fake_toolchain.env
    line = disc_line(capsys.readouterr().out)
    assert "SKIPPED" in line and "does not exist" in line


def test_a_checkout_without_a_local_config_runs_with_the_disc_tests_skipped(
    checkout: Path, capsys: pytest.CaptureFixture[str], fake_toolchain: type[FakeToolchain]
) -> None:
    (checkout / "coney.local.toml").unlink()
    assert main(["build", "--agent", "alice", "--configure", "--test", "--full"]) == 0
    assert "SKIPPED" in disc_line(capsys.readouterr().out)


def test_the_disc_is_set_for_the_configure_too_and_a_build_without_tests_says_nothing_of_it(
    checkout: Path, tmp_path: Path, capsys: pytest.CaptureFixture[str], fake_toolchain: type[FakeToolchain]
) -> None:
    game = tmp_path / "game"
    game.mkdir()
    set_game_dir(checkout, game)
    # CMake registers the extra `coney --disc` smoke tests when CONEY_DISC is in the environment of the configure.
    assert main(["build", "--agent", "alice", "--configure-only"]) == 0
    assert fake_toolchain.env["CONEY_DISC"] == str(game)
    assert "disc tests:" not in capsys.readouterr().out


# -- the build record ---------------------------------------------------------------------------------------------


def git_checkout(checkout: Path) -> Path:
    """Make the made-up checkout a git repository on `main` with everything committed."""
    for command in (
        ["init", "-q", "-b", "main"],
        ["config", "core.autocrlf", "false"],
        ["add", "-A"],
        ["-c", "user.name=T", "-c", "user.email=t@example.org", "commit", "-q", "-m", "base"],
    ):
        subprocess.run(["git", "-C", str(checkout), *command], check=True, capture_output=True)
    return checkout


def records(tmp_path: Path) -> list[Path]:
    """The build records of the task `main`."""
    return sorted((tmp_path / "scratch" / "main" / "build-runs").glob("*.json"))


PASSING_TESTS = "1/1 Test #1: a ... Passed 0.1 sec\n100% tests passed, 0 tests failed out of 1\n"


def test_a_run_that_built_and_tested_leaves_a_build_record(
    checkout: Path, tmp_path: Path, capsys: pytest.CaptureFixture[str], fake_toolchain: type[FakeToolchain]
) -> None:
    git_checkout(checkout)
    fake_toolchain.outputs["test"] = (0, PASSING_TESTS)
    assert main(["build", "--agent", "alice", "--preset", "ci", "--configure", "--test", "--full"]) == 0
    log = Path(capsys.readouterr().out.splitlines()[0].removeprefix("log: "))
    [path] = records(tmp_path)
    record = build_record.read(path.parent, path.stem)
    head = subprocess.run(
        ["git", "-C", str(checkout), "rev-parse", "HEAD"], capture_output=True, text=True, check=True
    ).stdout.strip()
    assert (record["tip"], record["dirty"], record["preset"], record["filters"]) == (head, False, "ci", [])
    assert (record["exit_code"], record["ran"], record["task"], record["agent"]) == (
        0,
        ["configure", "build", "test"],
        "main",
        "alice",
    )
    assert record["tests"] == {"total": 1, "passed": 1, "failed": 0, "skipped": 0, "skipped_disc": 0}
    assert record["log_sha256"] == hashlib.sha256(log.read_bytes()).hexdigest() and record["log"] == str(log)
    assert build_record.problems(record, tip=head, preset="ci") == []


def test_a_filtered_run_and_a_failed_run_are_recorded_as_such(
    checkout: Path, tmp_path: Path, fake_toolchain: type[FakeToolchain]
) -> None:
    git_checkout(checkout)
    fake_toolchain.outputs["test"] = (0, PASSING_TESTS)
    assert main(["build", "--agent", "alice", "--test", "-R", "Widget"]) == 0
    fake_toolchain.outputs["build"] = (1, "C:\a.cpp(3): error C2065: 'x': undeclared identifier\n")
    assert main(["build", "--agent", "alice", "--configure"]) == 1
    found = [build_record.read(p.parent, p.stem) for p in records(tmp_path)]
    assert sorted((r["exit_code"], r["filters"]) for r in found) == [(0, ["-R", "Widget"]), (1, [])]


def test_a_dirty_worktree_is_recorded_as_dirty(
    checkout: Path, tmp_path: Path, fake_toolchain: type[FakeToolchain]
) -> None:
    git_checkout(checkout)
    (checkout / "notes.txt").write_text("not committed", encoding="utf-8")
    assert main(["build", "--agent", "alice", "--configure"]) == 0
    [path] = records(tmp_path)
    assert build_record.read(path.parent, path.stem)["dirty"] is True


def test_a_checkout_that_is_not_a_git_repository_builds_without_a_record(
    checkout: Path, tmp_path: Path, fake_toolchain: type[FakeToolchain]
) -> None:
    assert main(["build", "--agent", "alice", "--configure"]) == 0
    assert records(tmp_path) == []


def test_the_task_environment_variable_names_the_folder_of_the_record(
    checkout: Path, tmp_path: Path, monkeypatch: pytest.MonkeyPatch, fake_toolchain: type[FakeToolchain]
) -> None:
    git_checkout(checkout)
    monkeypatch.setenv("CONEY_TASK", "757-fix-widget")
    assert main(["build", "--agent", "alice", "--configure"]) == 0
    assert len(list((tmp_path / "scratch" / "757-fix-widget" / "build-runs").glob("*.json"))) == 1
    assert records(tmp_path) == []


def test_the_build_for_a_check_runs_the_whole_suite_and_returns_its_record(
    checkout: Path, tmp_path: Path, fake_toolchain: type[FakeToolchain]
) -> None:
    git_checkout(checkout)
    fake_toolchain.outputs["test"] = (0, PASSING_TESTS)
    result = build_cli.build_and_test(checkout, "alice", "ci", task="757-fix-widget")
    assert result.exit_code == 0 and result.record is not None
    assert result.record.parent == tmp_path / "scratch" / "757-fix-widget" / "build-runs"
    record = build_record.read(result.record.parent, result.record.stem)
    assert (record["task"], record["preset"], record["filters"], record["ran"][-1]) == (
        "757-fix-widget",
        "ci",
        [],
        "test",
    )
    assert [c[0] for c in fake_toolchain.commands].count("ctest") == 1
    assert TIED  # the run is tied to its caller like any other


def test_the_build_for_a_check_refuses_an_unknown_preset_with_a_config_error(
    checkout: Path, fake_toolchain: type[FakeToolchain]
) -> None:
    with pytest.raises(ConfigError, match="asan"):
        build_cli.build_and_test(checkout, "alice", "asan")


def test_the_record_names_the_commit_and_state_from_before_the_run(
    checkout: Path, tmp_path: Path, monkeypatch: pytest.MonkeyPatch, fake_toolchain: type[FakeToolchain]
) -> None:
    git_checkout(checkout)
    before = subprocess.run(
        ["git", "-C", str(checkout), "rev-parse", "HEAD"], capture_output=True, text=True, check=True
    ).stdout.strip()
    execute = build_run.execute

    def commit_during_the_run(options: build_run.Options, context: build_run.Context) -> build_run.Result:
        (checkout / "late.txt").write_text("made while building", encoding="utf-8")
        for command in (["add", "-A"], ["-c", "user.name=T", "-c", "user.email=t@e.org", "commit", "-q", "-m", "late"]):
            subprocess.run(["git", "-C", str(checkout), *command], check=True, capture_output=True)
        return execute(options, context)

    monkeypatch.setattr(build_run, "execute", commit_during_the_run)
    assert main(["build", "--agent", "alice", "--configure"]) == 0
    [path] = records(tmp_path)
    record = build_record.read(path.parent, path.stem)
    assert record["tip"] == before and record["dirty"] is False
    assert "tool_revision" in record


def test_only_the_newest_records_are_kept(checkout: Path, tmp_path: Path, fake_toolchain: type[FakeToolchain]) -> None:
    git_checkout(checkout)
    folder = tmp_path / "scratch" / "main" / "build-runs"
    folder.mkdir(parents=True)
    for number in range(build_cli.KEEP_LOGS + 5):
        old = folder / f"{number:064x}.json"
        old.write_text("{}", encoding="utf-8")
        os.utime(old, (1_000_000 + number, 1_000_000 + number))
    other = folder / "notes.txt"
    other.write_text("not a record", encoding="utf-8")
    assert main(["build", "--agent", "alice", "--configure"]) == 0
    assert len(records(tmp_path)) == build_cli.KEEP_LOGS and other.exists()
    assert not (folder / f"{0:064x}.json").exists() and (folder / f"{build_cli.KEEP_LOGS + 4:064x}.json").exists()


def test_pruning_after_a_build_keeps_the_record_a_stamp_covers(
    checkout: Path, tmp_path: Path, fake_toolchain: type[FakeToolchain]
) -> None:
    git_checkout(checkout)
    folder = tmp_path / "scratch" / "main" / "build-runs"
    folder.mkdir(parents=True)
    for number in range(build_cli.KEEP_LOGS + 5):
        old = folder / f"{number:064x}.json"
        old.write_text("{}", encoding="utf-8")
        os.utime(old, (1_000_000 + number, 1_000_000 + number))
    stamps = tmp_path / "scratch" / "other-task" / "stamps"
    stamps.mkdir(parents=True)
    covers = '{"covers":{"build_record_sha256":"' + f"{0:064x}" + '"}}\n'
    (stamps / "0001.jsonl").write_text(covers, encoding="utf-8")
    assert main(["build", "--agent", "alice", "--configure"]) == 0
    assert (folder / f"{0:064x}.json").exists() and not (folder / f"{1:064x}.json").exists()


# --- build_root (coney.local.toml): the build trees on another drive ----------------------------------------------


def set_build_root(checkout: Path, builds: Path) -> None:
    """Add `build_root` to the checkout's local config."""
    config = checkout / "coney.local.toml"
    config.write_text(config.read_text(encoding="utf-8") + f"build_root = '{builds.as_posix()}'\n", encoding="utf-8")


def test_without_a_build_root_the_preset_folder_is_the_presets_own(
    checkout: Path, capsys: pytest.CaptureFixture[str], fake_toolchain: type[FakeToolchain]
) -> None:
    assert main(["build", "--agent", "alice", "--configure-only"]) == 0
    assert fake_toolchain.commands == [["cmake", "--preset", "dev"]]
    assert main(["build", "--agent", "alice", "--test", "--full"]) == 0
    assert ["cmake", "--build", "--preset", "dev"] in [c[:4] for c in fake_toolchain.commands]
    assert fake_toolchain.commands[-1][:3] == ["ctest", "--preset", "dev"]
    assert not (checkout / ".coney-build-id").exists()


def test_with_a_build_root_every_step_uses_the_folder_there(
    checkout: Path, tmp_path: Path, capsys: pytest.CaptureFixture[str], fake_toolchain: type[FakeToolchain]
) -> None:
    builds = tmp_path / "builds"
    set_build_root(checkout, builds)
    assert main(["build", "--agent", "alice", "--test", "--full"]) == 0
    ident = build_root.read_id(checkout)
    assert ident is not None
    folder = builds / ident / "dev"
    assert fake_toolchain.commands[0] == ["cmake", "--preset", "dev", "-B", str(folder)]
    assert ["cmake", "--build", str(folder)] in [c[:3] for c in fake_toolchain.commands]
    ctest = [c for c in fake_toolchain.commands if c[0] == "ctest"]
    assert ctest and all(c[:3] == ["ctest", "--test-dir", str(folder)] for c in ctest)
    assert build_root.owner(builds / ident) == checkout.resolve()
    assert not (checkout / "build").exists()


def test_with_a_build_root_a_custom_folder_under_build_moves_too(
    checkout: Path, tmp_path: Path, capsys: pytest.CaptureFixture[str], fake_toolchain: type[FakeToolchain]
) -> None:
    builds = tmp_path / "builds"
    set_build_root(checkout, builds)
    assert main(["build", "--agent", "alice", "--configure-only", "--build-dir", "build/tidy"]) == 0
    ident = build_root.read_id(checkout)
    assert fake_toolchain.commands == [["cmake", "--preset", "dev", "-B", str(builds / str(ident) / "tidy")]]


def test_a_folder_outside_the_preset_gets_the_test_presets_environment(
    checkout: Path, tmp_path: Path, capsys: pytest.CaptureFixture[str], fake_toolchain: type[FakeToolchain]
) -> None:
    presets = json.loads(json.dumps(PRESETS))
    presets["testPresets"][0]["environment"] = {"LSAN_OPTIONS": "suppressions=${sourceDir}/tests/lsan.supp"}
    (checkout / "CMakePresets.json").write_text(json.dumps(presets), encoding="utf-8")
    set_build_root(checkout, tmp_path / "builds")
    assert main(["build", "--agent", "alice", "--test", "--full"]) == 0
    assert fake_toolchain.env["LSAN_OPTIONS"] == f"suppressions={checkout.as_posix()}/tests/lsan.supp"
