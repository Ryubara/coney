# SPDX-License-Identifier: GPL-3.0-or-later
"""Live progress for the commands that run for a long time: `task check`, `ci run` and `orig fails-first`
(docs/guides/coney-tools-build.md#check-slots).

A full suite takes half an hour or more, and a run that prints nothing until it ends cannot be told from a hung one.
Three pieces, each used where a run could go quiet:

* `Heartbeat` calls a function every so many seconds from a thread of its own, however quiet the work is;
* `run_streamed` runs a program and passes each line it prints to a log file and the caller at once, with a heartbeat
  line (how long it has run and, for pytest, how far it got) at least every minute;
* `streaming_runner` is `run_streamed` in the shape the checks of `task check` take (`task_check_steps.Runner`).

Nothing here keeps state between runs: a run's log file is written line by line and flushed, so `tail` shows it grow.
"""

from __future__ import annotations

import os
import re
import subprocess
import sys
import threading
import time
from collections.abc import Callable, Sequence
from datetime import datetime
from pathlib import Path
from types import TracebackType

from coney_tools.local_gate import low_priority

#: Seconds between progress lines of a long step.
HEARTBEAT_SECONDS = 60.0
#: The programs a command line can name (`uv run --project python pytest ...`), the first found names its log.
KNOWN_PROGRAMS = ("pytest", "mypy", "ruff", "mkdocs", "pre-commit", "actionlint", "ctest", "cmake")
#: pytest's `[ 45%]` at the end of a line of dots.
PYTEST_PERCENT = re.compile(r"\[\s*(\d+)%\]")
#: Windows: the flag that gives a child no console window of its own (kept here so it exists on every platform).
NO_WINDOW = 0x08000000


class Heartbeat:
    """Calls `beat` every `interval` seconds from a thread of its own until the `with` block ends. The first call comes
    after one interval, not at once, so a quick step prints nothing; leaving the block stops the thread at once (it
    waits on an event, not on a sleep). A `beat` that raises is ignored: a progress line never fails the work."""

    def __init__(self, interval: float, beat: Callable[[], None]) -> None:
        self.interval = interval
        self.beat = beat
        self._stop = threading.Event()
        self._thread = threading.Thread(target=self._loop, name="heartbeat", daemon=True)

    def _loop(self) -> None:
        """Beat until stopped."""
        while not self._stop.wait(self.interval):
            try:
                self.beat()
            except Exception:  # a progress line must never end the run it reports on
                continue

    def __enter__(self) -> Heartbeat:
        self._thread.start()
        return self

    def __exit__(
        self, kind: type[BaseException] | None, error: BaseException | None, trace: TracebackType | None
    ) -> None:
        self._stop.set()
        self._thread.join(timeout=5)


def clock_text(now: datetime | None = None) -> str:
    """The time of day a progress line starts with (`14:03:09`), local time."""
    return (now or datetime.now()).strftime("%H:%M:%S")


def minutes_text(seconds: float) -> str:
    """A duration for a progress line: `45 s`, `12 min`, `1 h 05 min`."""
    if seconds < 90:
        return f"{seconds:.0f} s"
    minutes = int(seconds // 60)
    return f"{minutes} min" if minutes < 60 else f"{minutes // 60} h {minutes % 60:02d} min"


def command_label(command: Sequence[str]) -> str:
    """What to call a command in progress lines and in its log's name: the first well-known program on the command
    line (so `uv run --project python pytest ...` is `pytest`), else its own program name."""
    for word in command:
        if word in KNOWN_PROGRAMS:
            return word
    return Path(command[0]).stem if command else "command"


def stop_tree(process: subprocess.Popen[str]) -> None:
    """Stop a child and the programs it started, by id (never by name)."""
    if process.poll() is not None:
        return
    if sys.platform == "win32":
        subprocess.run(
            ["taskkill", "/T", "/F", "/PID", str(process.pid)],
            capture_output=True,
            check=False,
            creationflags=NO_WINDOW,
        )
    else:
        process.kill()


def run_streamed(
    command: Sequence[str],
    cwd: Path,
    say: Callable[[str], None],
    *,
    label: str | None = None,
    log: Path | None = None,
    append: bool = True,
    interval: float = HEARTBEAT_SECONDS,
    clock: Callable[[], float] = time.monotonic,
) -> tuple[int, str]:
    """Run a command below normal priority (`local_gate.low_priority`) and return its exit code and its output (stdout
    and stderr together), like `local_gate.run_command`, but live: each line goes to the `log` file (written line by
    line and flushed, so a reader sees it grow; `append=False` starts the file again) as it is printed, and `say` is
    called with a progress line every `interval` seconds while the command runs (`pytest: still running, 12 min, 45%
    done`). The child runs with PYTHONUNBUFFERED=1 so a Python program does not hold its output back either. A missing
    program is code 127. An interrupt stops the child and what it started, then goes on up."""
    name = label or command_label(command)
    lowered, options = low_priority(command)
    environment = {**os.environ, "PYTHONUNBUFFERED": "1"}
    if log is not None:
        log.parent.mkdir(parents=True, exist_ok=True)
    try:
        process = subprocess.Popen(  # type: ignore[call-overload]
            lowered,
            cwd=cwd,
            env=environment,
            stdin=subprocess.DEVNULL,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            encoding="utf-8",
            errors="replace",
            **options,
        )
    except OSError as error:
        return 127, f"{command[0]}: {error.strerror or error}"
    started = clock()
    lines: list[str] = []
    percent: list[int] = []  # the last percentage seen, in a list so the heartbeat reads it without a lock

    def beat() -> None:
        done = f", {percent[-1]}% done" if percent else ""
        say(f"  [{clock_text()}] {name}: still running, {minutes_text(clock() - started)}{done}")

    try:
        assert process.stdout is not None
        mode = "a" if append else "w"
        with log.open(mode, encoding="utf-8", buffering=1) if log is not None else _NoFile() as sink:
            sink.write(f"==== {' '.join(command)}\n")
            sink.flush()
            with Heartbeat(interval, beat):
                for line in process.stdout:
                    lines.append(line)
                    sink.write(line)
                    sink.flush()
                    found = PYTEST_PERCENT.findall(line)
                    if found:
                        percent.append(int(found[-1]))
                        del percent[:-1]
                code = process.wait()
            sink.write(f"==== exit {code}\n")
    except BaseException:
        stop_tree(process)
        raise
    return code, "".join(lines)


class _NoFile:
    """A stand-in for a log file that is not wanted: `with` works, writes go nowhere."""

    def __enter__(self) -> _NoFile:
        return self

    def __exit__(self, *_: object) -> None:
        return None

    def write(self, _: str) -> int:
        return 0

    def flush(self) -> None:
        return None


def streaming_runner(
    say: Callable[[str], None], logs: Path | None = None, interval: float = HEARTBEAT_SECONDS
) -> Callable[[Sequence[str], Path], tuple[int, str]]:
    """A runner in the shape of `task_check_steps.Runner` (a command and a folder, giving the exit code and the output)
    that streams: every command logs to `logs/<program>.log` when `logs` is given (one file per program: the first
    command of a program in this runner starts the file again, so a rerun does not grow it for ever, and its later
    commands append) and says a progress line every `interval` seconds."""
    started: set[Path] = set()

    def run(command: Sequence[str], cwd: Path) -> tuple[int, str]:
        label = command_label(command)
        log = logs / f"{label}.log" if logs is not None else None
        fresh = log is not None and log not in started
        if log is not None:
            started.add(log)
        return run_streamed(command, cwd, say, label=label, log=log, append=not fresh, interval=interval)

    return run
