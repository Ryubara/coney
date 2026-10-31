# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for stale_processes, the part of `coney-tools workspace clean` that finds agent shells left waiting on a dead
run and orphaned test runs, and stops them with `--apply`.

The choices are tested on made-up process lists, files and clocks; one test lists the real processes.
"""

from __future__ import annotations

import os
import subprocess
import sys
import time
from pathlib import Path

import pytest

from coney_tools import stale_processes
from coney_tools.stale_processes import Proc, Settings

NOW = 1_800_000_000.0
HOUR = 3600.0
LOG = "C:/Repositories/Coney/scratch/x/build.log"
WAIT = f"bash -c 'until grep -q PASSED {LOG}; do sleep 20; done'"


def proc(pid: int, ppid: int, name: str, age_hours: float, command: str = "") -> Proc:
    return Proc(pid, ppid, name, NOW - age_hours * HOUR, command or name)


def find(processes: list[Proc], files: dict[str, float | None] | None = None, me: int = 1) -> list[tuple[int, str]]:
    """Run the finder with file ages (hours since the last change; None for a missing file)."""
    ages = files if files is not None else {LOG: 2.0}

    def file_age(path: str) -> float | None:
        hours = ages.get(path.replace("\\", "/"))
        return None if hours is None else hours * HOUR

    settings = Settings(now=NOW, me=me, file_age=file_age)
    return [(s.process.pid, s.why) for s in stale_processes.find(processes, settings)]


def test_an_old_shell_waiting_on_a_log_unchanged_for_an_hour_is_stale() -> None:
    found = find([proc(10, 1, "bash", 5, WAIT)])
    assert [pid for pid, _ in found] == [10]
    assert "build.log" in found[0][1] and "5.0 h" in found[0][1] and "2.0 h" in found[0][1]


def test_a_young_shell_or_a_live_log_is_left_alone() -> None:
    assert find([proc(10, 1, "bash", 2, WAIT)]) == []  # under three hours old
    assert find([proc(10, 1, "bash", 5, WAIT)], {LOG: 0.5}) == []  # the log changed half an hour ago


def test_a_shell_waiting_on_a_file_that_does_not_exist_is_stale() -> None:
    found = find([proc(10, 1, "bash", 4, WAIT)], {})
    assert [pid for pid, _ in found] == [10] and "does not exist" in found[0][1]


def test_a_shell_with_no_wait_loop_or_no_file_to_judge_is_left_alone() -> None:
    assert find([proc(10, 1, "bash", 9, f"bash -c 'tail -n 5 {LOG}'")]) == []
    assert find([proc(10, 1, "bash", 9, "bash -c 'until grep -q X $LOG; do sleep 20; done'")]) == []
    assert find([proc(10, 1, "bash", 9, "bash")]) == []


def test_msys_and_windows_paths_are_both_read() -> None:
    msys = "bash -c 'until grep -q X /c/Repositories/Coney/scratch/x/build.log; do sleep 20; done'"
    windows = 'pwsh -c "while (-not (Select-String -Quiet C:\\Repositories\\Coney\\scratch\\x\\'
    windows += 'build.log X)) { Start-Sleep 9 }"'
    assert [pid for pid, _ in find([proc(10, 1, "bash", 5, msys)])] == [10]
    assert [pid for pid, _ in find([proc(11, 1, "pwsh", 5, windows)])] == [11]


def test_the_shell_running_this_command_and_its_ancestors_are_never_stale() -> None:
    processes = [proc(10, 1, "bash", 5, WAIT), proc(11, 10, "bash", 5, WAIT), proc(12, 11, "python", 0)]
    assert find(processes, me=12) == []


def test_only_the_topmost_of_a_stale_tree_is_listed() -> None:
    processes = [proc(10, 1, "bash", 5, WAIT), proc(11, 10, "bash", 5, WAIT), proc(12, 11, "sleep", 0)]
    assert [pid for pid, _ in find(processes)] == [10]


def test_a_ctest_or_test_run_whose_parent_is_gone_is_an_orphan() -> None:
    processes = [
        proc(20, 999, "ctest", 1),  # parent gone
        proc(21, 20, "coney_tests", 0.1),  # under the orphan: stopped with it, not listed
        proc(30, 31, "ctest", 1),
        proc(31, 1, "python", 1.5),  # a live parent
        proc(40, 41, "coney_tests", 1),
        proc(41, 1, "bash", 0.5),  # a parent younger than its child: the pid was reused
    ]
    found = find(processes)
    assert [pid for pid, _ in found] == [20, 40]
    assert "pid 999" in found[0][1] and "gone" in found[0][1]


def test_other_programs_with_a_gone_parent_are_not_touched() -> None:
    assert find([proc(20, 999, "cmake", 1), proc(21, 999, "notepad", 9)]) == []


def test_apply_stops_each_stale_tree_and_reports_failures(capsys: pytest.CaptureFixture[str]) -> None:
    stopped: list[int] = []
    stale = [
        stale_processes.Stale(proc(10, 1, "bash", 5, WAIT), "waits"),
        stale_processes.Stale(proc(20, 999, "ctest", 1), "orphan"),
    ]

    def kill(pid: int) -> str | None:
        stopped.append(pid)
        return "access denied" if pid == 20 else None

    failures = stale_processes.report(stale, apply=True, kill=kill)
    out = capsys.readouterr().out
    assert stopped == [10, 20] and failures == 1
    assert "stop" in out and "bash (pid 10)" in out and "FAILED: access denied" in out


def test_a_dry_run_stops_nothing(capsys: pytest.CaptureFixture[str]) -> None:
    stale = [stale_processes.Stale(proc(10, 1, "bash", 5, WAIT), "waits")]
    assert stale_processes.report(stale, apply=False, kill=lambda pid: pytest.fail("killed")) == 0
    assert "would stop" in capsys.readouterr().out


def test_nothing_stale_prints_one_line(capsys: pytest.CaptureFixture[str]) -> None:
    assert stale_processes.report([], apply=True, kill=lambda pid: None) == 0
    assert "no stale" in capsys.readouterr().out.lower()


def test_the_age_of_a_real_file_and_a_missing_one(tmp_path: Path) -> None:
    log = tmp_path / "x.log"
    log.write_text("x", encoding="utf-8")
    old = time.time() - 2 * HOUR
    os.utime(log, (old, old))
    age = stale_processes.file_age(str(log))
    assert age is not None and abs(age - 2 * HOUR) < 60
    assert stale_processes.file_age(str(tmp_path / "missing.log")) is None
    assert stale_processes.file_age(str(tmp_path)) == 0.0  # a folder is never judged unchanged


def test_the_real_process_list_holds_this_process_with_its_command() -> None:
    processes = {p.pid: p for p in stale_processes.list_processes()}
    me = processes[os.getpid()]
    # The command is this process's own (pytest's, or a pytest-xdist worker's `python -c ...`): its last word is in it.
    assert me.ppid == os.getppid() and me.name.startswith("python") and sys.orig_argv[-1] in me.command


def test_a_path_held_in_a_variable_the_loop_reads_is_judged() -> None:
    # How Claude Code's Bash tool shows a background wait in the process list (its command line, quotes escaped).
    command = (
        r'"C:\Program Files\Git\bin\bash.exe" -c "source /c/x/snapshot.sh && eval '
        r"'f=/c/Repositories/Coney/scratch/x/build.log; last=\"\"; while true; do "
        r"l=$(grep -E \"FAILED|passed\" \"$f\" | tail -1); if [ \"$l\" != \"$last\" ]; then echo \"$l\"; "
        r"""last=\"$l\"; fi; sleep 20; done' < /dev/null" """
    )
    found = find([proc(10, 1, "bash", 6, command)])
    assert [pid for pid, _ in found] == [10] and "build.log unchanged for 2.0 h" in found[0][1]
    assert find([proc(10, 1, "bash", 6, command)], {LOG: 0.2}) == []


def test_posix_command_lines_are_read_whole(monkeypatch: pytest.MonkeyPatch) -> None:
    # `ps` cuts its lines at 80 columns when not writing to a terminal unless asked for full width (`-ww`).
    asked: list[list[str]] = []
    long_command = "/usr/bin/bash -c " + "x" * 300

    def fake_run(command: list[str], **_kwargs: object) -> subprocess.CompletedProcess[str]:
        asked.append(command)
        return subprocess.CompletedProcess(command, 0, f"  42     1  01:02:03 {long_command}\n", "")

    monkeypatch.setattr(sys, "platform", "linux")
    monkeypatch.setattr(subprocess, "run", fake_run)
    (found,) = stale_processes.list_processes()
    assert "-ww" in asked[0] and found.command == long_command and found.name == "bash"
