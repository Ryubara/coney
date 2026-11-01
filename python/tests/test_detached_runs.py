# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for detached runs (detached_runs.py): `task check --detach` and `ci run --detach` run apart from their caller,
record their outcome however they end, and a killed run reads as ended without a result (issue 1370). No process is
started: `build_cli.spawn_detached` and the job-object calls are stand-ins."""

import argparse
import json
import os
from pathlib import Path

import pytest

from coney_tools import build_cli, detached_runs, process_lifetime, task_check
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
    assert spawned == [["child"]] and record(tmp_path)["state"] == "running" and record(tmp_path)["pid"] is None
    with pytest.raises(ConfigError, match="a detached task check of alice is still running"):
        detached_runs.start(KIND, tmp_path, ["child"], tmp_path, "alice")  # a record with no pid yet counts as running


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
