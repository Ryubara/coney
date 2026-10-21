# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for process_lifetime: the build tool's children die with it, it stops when the shell that started it is gone,
and a dead build's leftover processes are found and stopped.

The choices (which processes are leftovers, which ancestors to watch, when the watch fires) are tested on made-up
process lists. Two tests then kill a real "build tool" (a small Python script) and check that its grandchild, the
stand-in for a coney_tests.exe under ctest, dies with it.
"""

from __future__ import annotations

import os
import signal
import subprocess
import sys
import textwrap
import time
from pathlib import Path

import pytest

from coney_tools import pcsx2_proc, process_lifetime
from coney_tools.process_lifetime import ParentWatch, Process

T0 = 1_800_000_000.0


def proc(pid: int, ppid: int, name: str, started: float | None = T0) -> Process:
    return Process(pid, ppid, name, started)


# -- leftovers of a dead build -------------------------------------------------------------------------------------


def test_the_leftovers_of_a_dead_build_are_its_build_programs_and_everything_under_them() -> None:
    processes = [
        proc(500, 1, "bash", T0 - 100),
        proc(601, 500, "ctest", T0 + 10),  # the dead tool (pid 600) started ctest...
        proc(602, 601, "coney_tests", T0 + 20),  # ...which runs a test
        proc(603, 601, "coney_tests", T0 + 21),
        proc(610, 600, "ctest", T0 + 30),
        proc(611, 610, "coney_tests", T0 + 31),
        proc(612, 611, "conhost", T0 + 31),
        proc(620, 600, "cmake", T0 + 40),
        proc(621, 620, "ninja", T0 + 41),
        proc(622, 621, "cl", T0 + 42),
    ]
    roots = process_lifetime.leftovers(processes, dead_pid=600, since=T0)
    assert [p.pid for p in roots] == [610, 620]
    assert [p.pid for p in process_lifetime.descendants(processes, 610)] == [611, 612]


def test_a_process_older_than_the_claim_is_not_a_leftover_even_with_the_same_parent_pid() -> None:
    # Windows reuses pids: a ctest whose recorded parent has the dead build's pid but started before the claim belongs
    # to whatever had that pid earlier.
    processes = [proc(610, 600, "ctest", T0 - 3600), proc(611, 600, "ctest", T0 + 5)]
    assert [p.pid for p in process_lifetime.leftovers(processes, 600, T0)] == [611]


def test_only_build_programs_are_leftovers() -> None:
    processes = [proc(610, 600, "notepad", T0 + 5), proc(611, 600, "ninja", T0 + 5), proc(612, 600, "cmd", T0 + 5)]
    assert [p.pid for p in process_lifetime.leftovers(processes, 600, T0)] == [611]


def test_a_process_with_an_unknown_start_time_is_not_a_leftover() -> None:
    assert process_lifetime.leftovers([proc(610, 600, "ctest", None)], 600, T0) == []


def test_reaping_stops_each_leftover_tree_and_never_a_live_claimant() -> None:
    processes = [proc(610, 600, "ctest", T0 + 5), proc(611, 610, "coney_tests", T0 + 6)]
    stopped: list[int] = []
    reaped = process_lifetime.reap(600, T0, processes=lambda: processes, kill=stopped.append, alive=lambda pid: False)
    assert [p.pid for p in reaped] == [610] and stopped == [610]
    stopped.clear()
    assert (
        process_lifetime.reap(600, T0, processes=lambda: processes, kill=stopped.append, alive=lambda pid: True) == []
    )
    assert stopped == []


# -- the shell that started us -------------------------------------------------------------------------------------


def test_the_watch_covers_every_ancestor_up_to_the_shell_that_ran_the_tool() -> None:
    processes = [
        proc(10, 1, "claude", T0 - 500),
        proc(20, 10, "bash", T0 - 50),
        proc(30, 20, "uv", T0 - 5),
        proc(40, 30, "python", T0 - 4),
        proc(50, 40, "python", T0 - 3),
    ]
    assert [p.pid for p in process_lifetime.watched_ancestors(processes, 50)] == [40, 30, 20]


def test_without_a_shell_in_reach_every_ancestor_found_is_watched() -> None:
    # Git Bash: the native chain is python <- python <- uv <- timeout, whose recorded parent (an MSYS copy of bash) is
    # gone already.
    processes = [
        proc(30, 29, "timeout", T0 - 6),
        proc(40, 30, "uv", T0 - 5),
        proc(50, 40, "python", T0 - 4),
        proc(60, 50, "python", T0 - 3),
    ]
    assert [p.pid for p in process_lifetime.watched_ancestors(processes, 60)] == [50, 40, 30]


def test_a_parent_younger_than_its_child_is_a_reused_pid_and_ends_the_chain() -> None:
    processes = [proc(10, 5, "bash", T0 + 100), proc(20, 10, "python", T0)]
    assert process_lifetime.watched_ancestors(processes, 20) == []


def test_the_watch_fires_once_when_a_watched_process_is_gone() -> None:
    there = {20: True, 30: True}
    watched = [proc(30, 20, "uv"), proc(20, 10, "bash")]
    fired: list[int] = []
    watch = ParentWatch(watched, still_there=lambda p: there[p.pid], on_gone=lambda p: fired.append(p.pid))
    assert watch.check() is None
    there[20] = False
    gone = watch.check()
    assert gone is not None and gone.pid == 20
    watch.start(interval=0.01)
    for _ in range(500):
        if fired:
            break
        time.sleep(0.01)
    watch.stop()
    assert fired == [20]


def test_an_empty_watch_never_fires() -> None:
    watch = ParentWatch([], still_there=lambda p: False, on_gone=lambda p: pytest.fail("fired"))
    assert watch.check() is None


def test_the_real_process_list_holds_this_process_and_its_parent() -> None:
    processes = {p.pid: p for p in process_lifetime.list_processes()}
    me = processes[os.getpid()]
    # POSIX `ps -o comm` names a script started by its shebang after the script (`pytest`), not the interpreter.
    assert me.ppid == os.getppid() and me.name.startswith(("python", "pytest")) and me.started is not None
    assert abs(me.started - time.time()) < 7 * 24 * 3600
    assert process_lifetime.same_process(me)


def test_a_process_start_time_is_read_for_a_live_pid_and_not_for_a_dead_one() -> None:
    started = process_lifetime.process_started(os.getpid())
    assert started is not None and 0 < time.time() - started < 7 * 24 * 3600
    # A pid far above any the system hands out.
    assert process_lifetime.process_started(0x7FFFFFF0) is None


# -- a real tool, killed -------------------------------------------------------------------------------------------

#: A stand-in for `coney-tools build`: ties its children to itself, then runs (through the real toolchain) a child
#: that starts a grandchild and prints the grandchild's pid, and waits.
TOOL = textwrap.dedent(
    """
    import os, sys
    from pathlib import Path
    from coney_tools import build_run, process_lifetime
    process_lifetime.bind_children()
    process_lifetime.stop_on_signals()
    child = (
        "import subprocess, sys, time; "
        "g = subprocess.Popen([sys.executable, '-c', 'import time; time.sleep(120)']); "
        "print(g.pid, flush=True); time.sleep(120)"
    )
    toolchain = build_run.ProcessToolchain(Path.cwd(), os.environ)
    toolchain.run([sys.executable, "-c", child], lambda line: print(line, flush=True))
    """
)


def _start_tool(tmp_path: Path) -> tuple[subprocess.Popen[str], int]:
    """Start the stand-in tool; return it and its grandchild's pid."""
    script = tmp_path / "tool.py"
    script.write_text(TOOL, encoding="utf-8")
    tool = subprocess.Popen(
        [sys.executable, str(script)], cwd=tmp_path, stdout=subprocess.PIPE, text=True, stdin=subprocess.DEVNULL
    )
    assert tool.stdout is not None
    line = tool.stdout.readline()
    assert line.strip().isdigit(), line
    return tool, int(line)


def _gone_soon(pid: int, seconds: float = 15.0) -> bool:
    """Whether `pid` ends within `seconds`."""
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        if not pcsx2_proc.pid_alive(pid):
            return True
        time.sleep(0.1)
    return False


@pytest.mark.skipif(sys.platform != "win32", reason="the job object is Windows'")
def test_killing_the_tool_kills_its_grandchild_on_windows(tmp_path: Path) -> None:
    tool, grandchild = _start_tool(tmp_path)
    try:
        tool.kill()  # TerminateProcess: no cleanup runs in the tool, only the job closes
        tool.wait(10)
        assert _gone_soon(grandchild)
    finally:
        if pcsx2_proc.pid_alive(grandchild):
            pcsx2_proc.terminate(grandchild, 1)


@pytest.mark.skipif(sys.platform == "win32", reason="signals are POSIX'")
def test_terminating_the_tool_kills_its_child_group_on_posix(tmp_path: Path) -> None:
    tool, grandchild = _start_tool(tmp_path)
    try:
        tool.send_signal(signal.SIGTERM)
        tool.wait(10)
        assert _gone_soon(grandchild)
    finally:
        if sys.platform != "win32" and pcsx2_proc.pid_alive(grandchild):
            os.kill(grandchild, signal.SIGKILL)


#: The stand-in tool tied to its caller the way `coney-tools build` is (build_cli._tie_to_caller).
TIED_TOOL = TOOL.replace("process_lifetime.bind_children()\nprocess_lifetime.stop_on_signals()\n", "").replace(
    "toolchain.run(", "from coney_tools import build_cli\nbuild_cli._tie_to_caller(toolchain)\ntoolchain.run("
)
#: Its caller: starts the tool and waits for it, as a shell would.
CALLER = "import subprocess, sys; subprocess.Popen([sys.executable, sys.argv[1]]).wait()"


def test_when_the_caller_of_the_tool_dies_the_tool_and_its_grandchild_end(tmp_path: Path) -> None:
    script = tmp_path / "tool.py"
    script.write_text(TIED_TOOL, encoding="utf-8")
    caller = subprocess.Popen(
        [sys.executable, "-c", CALLER, str(script)],
        cwd=tmp_path,
        stdout=subprocess.PIPE,
        text=True,
        stdin=subprocess.DEVNULL,
    )
    assert caller.stdout is not None
    line = caller.stdout.readline()
    assert line.strip().isdigit(), line
    grandchild = int(line)
    try:
        caller.kill()  # the caller only: nothing reaches the tool but the watch
        caller.wait(10)
        assert _gone_soon(grandchild, 30.0)
    finally:
        if pcsx2_proc.pid_alive(grandchild):
            process_lifetime.kill_tree(grandchild)
