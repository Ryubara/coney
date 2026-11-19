# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for detached runs (detached_runs.py): `task check --detach` and `ci run --detach` run apart from their caller,
record their outcome however they end, and a killed run reads as ended without a result (issue 1370). No process is
started: `build_cli.spawn_detached` and the job-object calls are stand-ins."""

import argparse
import json
import os
import sys
import time
from pathlib import Path

import pytest

from coney_tools import build_cli, detached_runs, identity, pcsx2_proc, process_lifetime, task_check
from coney_tools.config import ConfigError

KIND = detached_runs.Kind("task check", "coney-tools task check --result", "result.json", "out.txt")


@pytest.fixture(autouse=True)
def no_job_objects(monkeypatch: pytest.MonkeyPatch) -> None:
    """The child's own process guards would bind the test process: they do nothing here."""
    monkeypatch.setattr(process_lifetime, "bind_children", lambda: None)
    monkeypatch.setattr(process_lifetime, "stop_on_signals", lambda: None)


@pytest.fixture
def spawned(monkeypatch: pytest.MonkeyPatch) -> list[list[str]]:
    """The commands `spawn_detached` was asked to start (none is started)."""
    commands: list[list[str]] = []

    def spawn(command: list[str], cwd: Path, output: Path) -> int:
        commands.append(command)
        return 4242

    monkeypatch.setattr(build_cli, "spawn_detached", spawn)
    return commands


def record(folder: Path) -> dict[str, object]:
    data: dict[str, object] = json.loads((folder / KIND.result_file).read_text(encoding="utf-8"))
    return data


def test_a_child_records_its_exit_code_and_the_result_says_it(
    tmp_path: Path, capsys: pytest.CaptureFixture[str]
) -> None:
    (tmp_path / KIND.output_file).write_text("task check: ok\n", encoding="utf-8")
    assert detached_runs.child(KIND, tmp_path / KIND.result_file, "alice", lambda: 0) == 0
    assert record(tmp_path)["state"] == "done" and record(tmp_path)["exit_code"] == 0
    capsys.readouterr()
    assert detached_runs.read(KIND, tmp_path) == 0
    assert "task check: ok" in capsys.readouterr().out


def test_a_child_that_raises_a_config_error_records_exit_2(tmp_path: Path) -> None:
    def refuse() -> int:
        raise ConfigError("the worktree changed while the checks ran")

    assert detached_runs.child(KIND, tmp_path / KIND.result_file, "alice", refuse) == 2
    assert record(tmp_path)["exit_code"] == 2


def test_a_run_killed_before_its_end_reads_as_ended_without_a_result(
    tmp_path: Path, capsys: pytest.CaptureFixture[str]
) -> None:
    # A killed process writes nothing more: its record keeps `running` with a pid that is gone.
    gone = {"state": "running", "agent": "alice", "pid": 2**22 + 7, "process_started": 1.0, "started": 1.0}
    build_cli._write_record(tmp_path / KIND.result_file, gone)
    assert detached_runs.read(KIND, tmp_path) == 1
    assert "ended without a result" in capsys.readouterr().out


def test_a_run_still_running_reads_as_exit_75(tmp_path: Path, capsys: pytest.CaptureFixture[str]) -> None:
    live = {
        "state": "running",
        "agent": "alice",
        "pid": os.getpid(),
        "process_started": process_lifetime.process_started(os.getpid()),
        "started": 1.0,
    }
    build_cli._write_record(tmp_path / KIND.result_file, live)
    assert detached_runs.read(KIND, tmp_path) == detached_runs.EXIT_RUNNING == 75
    assert "still running" in capsys.readouterr().out


def test_no_record_says_so(tmp_path: Path, capsys: pytest.CaptureFixture[str]) -> None:
    assert detached_runs.read(KIND, tmp_path) == 1
    assert "no detached task check recorded" in capsys.readouterr().out


def test_start_records_the_run_and_refuses_a_second_while_it_runs(tmp_path: Path, spawned: list[list[str]]) -> None:
    assert detached_runs.start(KIND, tmp_path, ["child"], tmp_path, "alice") == 0
    # The starter notes the child's pid and when that process started (issue 1390), so liveness is judged by both.
    assert spawned == [["child"]] and record(tmp_path)["state"] == "running" and record(tmp_path)["pid"] == 4242
    assert "process_started" in record(tmp_path)
    live = {**record(tmp_path), "pid": os.getpid(), "process_started": process_lifetime.process_started(os.getpid())}
    build_cli._write_record(tmp_path / KIND.result_file, live)
    with pytest.raises(ConfigError, match="a detached task check of alice is still running"):
        detached_runs.start(KIND, tmp_path, ["child"], tmp_path, "alice")


def test_a_spawn_that_fails_leaves_no_record_that_counts_as_running(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    def fail(command: list[str], cwd: Path, output: Path) -> int:
        raise OSError("no such program")

    monkeypatch.setattr(build_cli, "spawn_detached", fail)
    with pytest.raises(OSError, match="no such program"):
        detached_runs.start(KIND, tmp_path, ["child"], tmp_path, "alice")
    assert not (tmp_path / KIND.result_file).exists()
    # The last run's outcome survives a later spawn that fails.
    done = {"state": "done", "agent": "alice", "pid": 1, "exit_code": 0, "started": 1.0}
    build_cli._write_record(tmp_path / KIND.result_file, done)
    with pytest.raises(OSError, match="no such program"):
        detached_runs.start(KIND, tmp_path, ["child"], tmp_path, "alice")
    assert record(tmp_path) == done


def test_a_child_that_dies_before_its_first_write_does_not_block_a_later_detach(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    gone = 2**22 + 7
    monkeypatch.setattr(pcsx2_proc, "pid_alive", lambda pid: pid != gone)
    monkeypatch.setattr(build_cli, "spawn_detached", lambda command, cwd, output: gone)
    assert detached_runs.start(KIND, tmp_path, ["child"], tmp_path, "alice") == 0
    assert record(tmp_path)["pid"] == gone  # the child wrote nothing: this is all the record has
    assert detached_runs.read(KIND, tmp_path) == 1 and "ended without a result" in capsys.readouterr().out
    monkeypatch.setattr(build_cli, "spawn_detached", lambda command, cwd, output: 4242)
    assert detached_runs.start(KIND, tmp_path, ["child"], tmp_path, "bob") == 0  # accepted, not "still running"
    assert record(tmp_path)["agent"] == "bob"


def test_a_record_whose_starter_never_noted_the_pid_stops_counting_as_running(tmp_path: Path) -> None:
    stale = {
        "state": "running",
        "agent": "alice",
        "pid": None,
        "started": time.time() - 2 * build_cli.NOT_STARTED_SECONDS,
    }
    build_cli._write_record(tmp_path / KIND.result_file, stale)
    assert not build_cli._alive(stale)
    assert build_cli._alive({**stale, "started": time.time()})


def test_the_starter_never_overwrites_the_record_the_child_wrote(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    def quick_child(command: list[str], cwd: Path, output: Path) -> int:
        # A child that finished before its starter got back: its own record is the truth.
        build_cli._write_record(
            tmp_path / KIND.result_file, {"state": "done", "agent": "alice", "pid": 99, "exit_code": 0}
        )
        return 99

    monkeypatch.setattr(build_cli, "spawn_detached", quick_child)
    detached_runs.start(KIND, tmp_path, ["child"], tmp_path, "alice")
    assert record(tmp_path)["state"] == "done"


def test_a_real_child_started_by_spawn_detached_reads_the_identity_handed_on_to_it(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    home = tmp_path / "home"
    monkeypatch.setenv("HOME", str(home))
    monkeypatch.setenv("USERPROFILE", str(home))
    monkeypatch.delenv(identity.TOKEN_VARIABLE, raising=False)
    caller = identity.Identity(
        id="a1", type="tooling", model="claude-sonnet-5-5", verified=True, session="s1", expires=time.time() + 300
    )
    monkeypatch.setattr(identity, "_current", caller)
    # The child asks for its own identity, as every coney-tools command does as its first act.
    script = "from coney_tools import identity; f = identity.current(); print(f.verified, f.id, f.type, f.session)"
    output = tmp_path / "child.out"
    pid = build_cli.spawn_detached([sys.executable, "-c", script], tmp_path, output)
    deadline = time.time() + 120
    while time.time() < deadline and not output.read_text(encoding="utf-8").strip():
        time.sleep(0.1)
    assert output.read_text(encoding="utf-8").split() == ["True", "a1", "tooling", "s1"], f"child {pid}"
    # The file the child read is the handed-on one, capped at the caller's own life.
    (file,) = (home / ".coney" / "identity").glob("*.json")
    assert json.loads(file.read_text(encoding="utf-8"))["expires"] <= caller.expires


def check_args(**changes: object) -> argparse.Namespace:
    """The options of `task check` as the parser leaves them."""
    values: dict[str, object] = {
        "name": None,
        "worktree": None,
        "base": None,
        "agent": None,
        "profile": None,
        "keep_going": False,
        "dry_run": False,
        "max_wait": 240.0,
        "detach": False,
        "result": False,
        "result_file": None,
        **changes,
    }
    return argparse.Namespace(**values)


def test_the_task_check_child_gets_the_options_the_worktree_and_its_result_file(tmp_path: Path) -> None:
    args = check_args(name="1370-x", base="origin/main", agent="alice", keep_going=True, max_wait=90.0)
    command = task_check.child_command(args, tmp_path, tmp_path / "r.json")
    assert command[1:6] == ["-m", "coney_tools.cli", "task", "check", "1370-x"]
    assert command[command.index("--worktree") + 1] == str(tmp_path) and "--keep-going" in command
    assert command[command.index("--agent") + 1] == "alice" and command[command.index("--max-wait") + 1] == "90.0"
    assert command[-2:] == ["--result-file", str(tmp_path / "r.json")] and "--detach" not in command


def test_task_check_detach_goes_alone() -> None:
    with pytest.raises(ConfigError, match="--detach and --result go alone"):
        task_check.run(check_args(detach=True, dry_run=True))


def test_the_task_check_parser_takes_detach_and_result() -> None:
    from coney_tools.cli import _build_parser as build_parser

    parsed = build_parser().parse_args(["task", "check", "--detach"])
    assert parsed.detach and not parsed.result and parsed.result_file is None
    assert build_parser().parse_args(["task", "check", "--result"]).result
