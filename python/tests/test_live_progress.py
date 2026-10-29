# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for live_progress.py: a long command's output reaches its log while it runs, a progress line comes every
interval however quiet the command is, and an interrupt stops the child. The commands are tiny Python programs started
here; no real suite runs."""

import _thread
import subprocess
import sys
import threading
import time
from pathlib import Path

import pytest

from coney_tools.live_progress import Heartbeat, command_label, minutes_text, run_streamed, streaming_runner


def python(code: str) -> list[str]:
    """A command that runs `code` with the interpreter running the tests."""
    return [sys.executable, "-c", code]


def wait_for(condition: object, seconds: float = 20.0) -> bool:
    """Poll `condition` (a callable) until it holds or `seconds` pass."""
    assert callable(condition)
    end = time.monotonic() + seconds
    while time.monotonic() < end:
        if condition():
            return True
        time.sleep(0.02)
    return False


def test_the_log_grows_while_the_command_runs(tmp_path: Path) -> None:
    # The command prints a line, then waits for a file the test writes only after it has seen that line in the log: if
    # the log were written when the command ends, the test would wait for ever (and fail at the wait's limit).
    go = tmp_path / "go"
    log = tmp_path / "logs" / "job.log"
    code = (
        "import pathlib, time\n"
        "print('first line', flush=True)\n"
        f"p = pathlib.Path({str(go)!r})\n"
        "end = time.time() + 20\n"
        "while not p.exists() and time.time() < end: time.sleep(0.02)\n"
        "print('last line', flush=True)\n"
    )
    result: list[tuple[int, str]] = []
    runner = threading.Thread(
        target=lambda: result.append(run_streamed(python(code), tmp_path, lambda _: None, log=log))
    )
    runner.start()
    try:
        assert wait_for(lambda: log.is_file() and "first line" in log.read_text(encoding="utf-8")), (
            "the log did not grow"
        )
        assert not result  # still running: the line came before the end
    finally:
        go.write_text("go", encoding="utf-8")
        runner.join(timeout=30)
    assert result[0][0] == 0 and "last line" in result[0][1]
    text = log.read_text(encoding="utf-8")
    assert "last line" in text and text.rstrip().endswith("==== exit 0")


def test_a_quiet_command_still_says_a_progress_line_every_interval(tmp_path: Path) -> None:
    said: list[str] = []
    code, _ = run_streamed(python("import time; time.sleep(1.5)"), tmp_path, said.append, label="pytest", interval=0.2)
    assert code == 0
    assert len(said) >= 3, said
    assert all("pytest: still running" in line for line in said)


def test_the_progress_line_carries_the_percent_pytest_printed(tmp_path: Path) -> None:
    said: list[str] = []
    code = "import time; print('.' * 10 + ' [ 45%]', flush=True); time.sleep(1.2)"
    run_streamed(python(code), tmp_path, said.append, label="pytest", interval=0.2)
    assert any("45% done" in line for line in said), said


def test_a_failing_command_gives_its_code_and_all_its_output(tmp_path: Path) -> None:
    code, output = run_streamed(
        python("import sys; print('out'); print('err', file=sys.stderr); sys.exit(3)"), tmp_path, lambda _: None
    )
    assert code == 3 and "out" in output and "err" in output


def test_a_missing_program_is_code_127(tmp_path: Path) -> None:
    code, output = run_streamed(["no-such-program-1135"], tmp_path, lambda _: None)
    assert code == 127 and "no-such-program-1135" in output


def test_the_child_prints_without_waiting_for_a_full_buffer(tmp_path: Path) -> None:
    # PYTHONUNBUFFERED is set for the child: a plain print (no flush) reaches the log before the child ends.
    go = tmp_path / "go"
    log = tmp_path / "job.log"
    code = (
        "import pathlib, time\n"
        "print('early')\n"
        f"p = pathlib.Path({str(go)!r})\n"
        "end = time.time() + 20\n"
        "while not p.exists() and time.time() < end: time.sleep(0.02)\n"
    )
    runner = threading.Thread(target=lambda: run_streamed(python(code), tmp_path, lambda _: None, log=log))
    runner.start()
    try:
        assert wait_for(lambda: log.is_file() and "early" in log.read_text(encoding="utf-8"))
    finally:
        go.write_text("go", encoding="utf-8")
        runner.join(timeout=30)


def test_an_interrupt_stops_the_child(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    started: list[object] = []
    real = subprocess.Popen

    def spy(*args: object, **kwargs: object) -> object:
        process = real(*args, **kwargs)  # type: ignore[call-overload]
        started.append(process)
        return process

    monkeypatch.setattr(subprocess, "Popen", spy)
    # The child prints a line every tenth of a second, so the reading loop turns and sees the interrupt.
    child = "import time\nwhile True:\n    print('tick', flush=True)\n    time.sleep(0.1)"
    timer = threading.Timer(0.8, _thread.interrupt_main)
    timer.start()
    try:
        with pytest.raises(KeyboardInterrupt):
            run_streamed(python(child), tmp_path, lambda _: None, interval=5)
    finally:
        timer.cancel()
    process = started[0]
    assert wait_for(lambda: process.poll() is not None, 10)  # type: ignore[attr-defined]


def test_the_streaming_runner_logs_each_program_in_its_own_file(tmp_path: Path) -> None:
    said: list[str] = []
    run = streaming_runner(said.append, tmp_path / "check-logs")
    code, output = run(python("print('hello')"), tmp_path)
    assert code == 0 and "hello" in output
    assert (tmp_path / "check-logs" / "python.log").is_file()


def test_the_heartbeat_beats_until_it_is_left_and_then_stops() -> None:
    beats: list[int] = []
    with Heartbeat(0.05, lambda: beats.append(1)):
        assert wait_for(lambda: len(beats) >= 3)
    seen = len(beats)
    time.sleep(0.3)
    assert len(beats) == seen  # the thread is gone with the block


def test_a_beat_that_raises_does_not_end_the_heartbeat() -> None:
    beats: list[int] = []

    def beat() -> None:
        beats.append(1)
        raise OSError("the console is gone")

    with Heartbeat(0.05, beat):
        assert wait_for(lambda: len(beats) >= 3)


def test_a_command_is_named_for_the_first_known_program_on_its_line() -> None:
    assert command_label(["uv", "run", "--project", "python", "pytest", "python/tests"]) == "pytest"
    assert command_label(["uvx", "pre-commit", "run", "--all-files"]) == "pre-commit"
    assert command_label(["/usr/bin/actionlint"]) == "actionlint"
    assert command_label([]) == "command"


def test_minutes_read_as_seconds_minutes_or_hours() -> None:
    assert minutes_text(45) == "45 s"
    assert minutes_text(12 * 60 + 5) == "12 min"
    assert minutes_text(65 * 60) == "1 h 05 min"


def test_a_rerun_starts_a_programs_log_again_and_its_later_commands_append(tmp_path: Path) -> None:
    logs = tmp_path / "check-logs"
    first = streaming_runner(lambda _: None, logs)
    first(python("print('alpha')"), tmp_path)
    assert "alpha" in (logs / "python.log").read_text(encoding="utf-8")
    first(python("print('beta')"), tmp_path)  # the same program in the same runner: appended
    text = (logs / "python.log").read_text(encoding="utf-8")
    assert "alpha" in text and "beta" in text
    rerun = streaming_runner(lambda _: None, logs)  # a new run of the whole check: the file starts again
    rerun(python("print('gamma')"), tmp_path)
    text = (logs / "python.log").read_text(encoding="utf-8")
    assert "gamma" in text and "alpha" not in text and "beta" not in text
