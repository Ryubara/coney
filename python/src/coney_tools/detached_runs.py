# SPDX-License-Identifier: GPL-3.0-or-later
"""Long commands that run apart from their caller: `ci run --detach` and `task check --detach`
(docs/guides/coney-tools-repo.md#task-check, docs/guides/coney-tools-ci.md#ci-run).

A long command started by an agent dies with the shell that started it: a `timeout` wrapper or a background tool's
limit kills the shell, and with it an hour of checks, leaving no result and no error (issue 1370: one `task check` ended
after its build had linked, another at 2988 of 3655 tests). A detached run starts the same command in a process of its
own, apart from the calling shell (`build_cli.spawn_detached`, as `build --detach` does), and returns at once. The run
records itself in a result file in the worktree's scratch folder (`running`, its pid and when that process started;
at the end `done` and the exit code), however it ends, and its console output goes to a file beside it. Reading the
result says the run's own exit code once it ended, 75 while it runs, and 1 with `ended without a result` when its
process is gone without writing one (killed).

The record's reading and writing are `build_cli`'s, so the three kinds of detached run judge "still running" the same
way (a live pid that started when the record says).
"""

from __future__ import annotations

import os
import sys
import time
from collections.abc import Callable
from dataclasses import dataclass
from pathlib import Path

from coney_tools import build_cli, process_lifetime
from coney_tools.config import ConfigError

#: The exit code of a result read while the run still runs: "look again later", as a build still queued says.
EXIT_RUNNING = 75


@dataclass(frozen=True)
class Kind:
    """One kind of detached run: what it is called in messages (`check`, `task check`), the command that reads its
    result, and its result and output files in the worktree's scratch folder."""

    what: str
    result_command: str
    result_file: str
    output_file: str

    def files(self, folder: Path) -> tuple[Path, Path]:
        """The result file and the output file in `folder`."""
        return folder / self.result_file, folder / self.output_file


def start(kind: Kind, folder: Path, command: list[str], cwd: Path, agent: str) -> int:
    """Record the run as started, start `command` (which must end up in `child`) apart from this shell and return 0
    at once, saying where to look. Refuses (ConfigError) a second run of the kind in a worktree whose last one still
    runs: they would share one result file."""
    result, output = kind.files(folder)
    previous = build_cli._read_record(result)
    if previous is not None and previous.get("state") == "running" and build_cli._alive(previous):
        raise ConfigError(
            f"a detached {kind.what} of {previous.get('agent')} is still running (pid {previous.get('pid')}): "
            f"`{kind.result_command}` shows how it ends"
        )
    folder.mkdir(parents=True, exist_ok=True)
    record = {"state": "running", "agent": agent, "pid": None, "log": str(output), "started": time.time()}
    build_cli._write_record(result, record)
    pid = build_cli.spawn_detached(command, cwd, output)
    print(f"detached: pid {pid}, output in {output}")
    print(
        f"write STATUS.md and stop, or do other work: `{kind.result_command}` shows the outcome (exit 75 while it runs)"
    )
    return 0


def child(kind: Kind, result: Path, agent: str, body: Callable[[], int]) -> int:
    """The detached process itself: run `body`, its exit code recorded in `result` however it ends (a ConfigError is
    printed and is exit 2, an interrupt 130). A process killed outright writes nothing, and its record keeps
    `running` with a pid that is gone, which `read` reports as ended without a result."""
    record = {
        "state": "running",
        "agent": agent,
        "pid": os.getpid(),
        # When this process started, so a later process given the same pid is not taken for this run (build_cli._alive).
        "process_started": process_lifetime.process_started(os.getpid()),
        "log": str(result.parent / kind.output_file),
        "started": time.time(),
    }
    build_cli._write_record(result, record)
    code = 1
    try:
        process_lifetime.bind_children()
        process_lifetime.stop_on_signals()
        code = body()
        return code
    except ConfigError as error:
        print(f"coney-tools: {error}")
        code = 2
        return code
    except KeyboardInterrupt:
        code = 130
        raise
    finally:
        sys.stdout.flush()
        build_cli._write_record(result, {**record, "state": "done", "exit_code": code, "ended": time.time()})


def read(kind: Kind, folder: Path) -> int:
    """Print the output and outcome of the worktree's last detached run of the kind. Exit: the run's own code once it
    has ended, 75 while it runs, 1 when there is none or it ended without a result."""
    result, output = kind.files(folder)
    record = build_cli._read_record(result)
    if record is None:
        print(f"no detached {kind.what} recorded for this worktree")
        return 1
    try:
        print(output.read_text(encoding="utf-8", errors="replace").rstrip())
    except OSError:
        print(f"(no output in {output} yet)")
    if record.get("state") == "running":
        if build_cli._alive(record):
            print(f"still running (pid {record.get('pid')}): look again later")
            return EXIT_RUNNING
        print(f"ended without a result (pid {record.get('pid')} is gone)")
        return 1
    code = int(record.get("exit_code", 1))
    print(f"exit {code}")
    return code
