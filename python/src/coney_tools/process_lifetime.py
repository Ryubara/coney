# SPDX-License-Identifier: GPL-3.0-or-later
"""Tie the processes `coney-tools build` starts to its own life, so a build or a test run never outlives the agent that
asked for it (docs/guides/coney-tools.md#build, "Process lifetimes").

An orphaned `ctest` keeps starting the next test after its caller is gone, and killing the tests one by one only lets
it start more. Three guards, each for a way the caller can disappear:

* **The tool is killed.** On Windows the tool puts itself in a job object with JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE
  (`bind_children`): every process it starts, and everything those start, inherits the job, and when the tool ends,
  however it ends, the system closes the job's last handle and ends them all. On Linux and macOS each child starts a
  session of its own (so its whole tree is one process group, stopped with one `killpg`), the tool stops that group on
  SIGTERM and SIGHUP (`stop_on_signals`) as on Ctrl+C, and on Linux the child also gets SIGKILL when the tool dies by
  SIGKILL (PR_SET_PDEATHSIG).
* **The shell that ran the tool is gone** (the agent tool restarted, a background shell was killed) while the tool
  itself lives on. `ParentWatch` watches the ancestors up to that shell and stops the build when one of them ends.
* **An older tool died without these guards** (or a POSIX build lost its group). When the next build takes over the
  dead build's claim, `reap` stops the build programs the dead tool started, found by their parent's pid and their
  start time (which must not be before the claim's, as Windows reuses pids).

Listing processes and stopping them is behind small functions; the choices (`leftovers`, `watched_ancestors`,
`ParentWatch.check`) take plain process lists, so they are tested on made-up ones.
"""

from __future__ import annotations

import contextlib
import ctypes
import os
import re
import signal
import subprocess
import sys
import threading
import time
from collections.abc import Callable, Iterable
from ctypes import wintypes
from dataclasses import dataclass
from functools import lru_cache
from typing import Any

from coney_tools import pcsx2_proc

#: The programs a build starts, whose leftovers a later build may stop (names without `.exe`, lower case).
BUILD_PROGRAMS = frozenset({"cmake", "ctest", "ninja", "coney_tests"})
#: Shells: the watch goes up the ancestors as far as the first of these, the one that ran the tool.
SHELLS = frozenset({"bash", "sh", "zsh", "dash", "fish", "pwsh", "powershell", "cmd"})
#: Windows: the creation flag that starts a console child with no console window of its own. Spelt out so it exists
#: on every platform; the tools that start hidden children share it.
CREATE_NO_WINDOW = 0x08000000
#: How many ancestors the watch looks through for the shell.
MAX_ANCESTORS = 8
#: Seconds two start times may differ and still be the same process (POSIX reports elapsed time in whole seconds).
START_SLACK = 2.0
#: Seconds between looks at the watched ancestors.
WATCH_SECONDS = 2.0


@dataclass(frozen=True)
class Process:
    """One process: its id, its parent's id (which may be gone, or reused), its program and when it started."""

    pid: int
    ppid: int
    name: str
    started: float | None  # epoch seconds; None when it cannot be read


def _program(name: str) -> str:
    """A program's name as compared here: the file name, lower case, without `.exe`."""
    base = re.split(r"[\\/]", name.strip())[-1].lower()
    return base.removesuffix(".exe")


# -- choices, on plain process lists --------------------------------------------------------------------------------


def descendants(processes: Iterable[Process], root: int) -> list[Process]:
    """Every process under `root` (children, their children, ...), parents before children, not `root` itself."""
    by_parent: dict[int, list[Process]] = {}
    for process in processes:
        by_parent.setdefault(process.ppid, []).append(process)
    found: list[Process] = []
    pending = [root]
    seen = {root}
    while pending:
        for child in sorted(by_parent.get(pending.pop(0), []), key=lambda p: p.pid):
            if child.pid not in seen:
                seen.add(child.pid)
                found.append(child)
                pending.append(child.pid)
    return found


def leftovers(processes: Iterable[Process], dead_pid: int, since: float) -> list[Process]:
    """The build programs a dead build tool (`dead_pid`, whose claim was made at `since`) left running: its direct
    children that are build programs and started no earlier than the claim. A process whose start time is unknown is
    never one (it could be anybody's)."""
    return sorted(
        (
            p
            for p in processes
            if p.ppid == dead_pid
            and p.name in BUILD_PROGRAMS
            and p.started is not None
            and p.started >= since - START_SLACK
        ),
        key=lambda p: p.pid,
    )


def watched_ancestors(processes: Iterable[Process], me: int) -> list[Process]:
    """The ancestors to watch: from the parent up to the first shell (the one that ran the tool, through `uv` and
    Python's launchers), at most MAX_ANCESTORS. With no shell in reach, every ancestor that can be found: each is a
    launcher that waits for the tool. (Git Bash on Windows is such a case: MSYS starts a command from a short-lived
    copy of the shell, so the recorded parent of the first native process is already gone, and the chain stops there.)
    The chain also ends at a parent that started after its child: that pid was reused, and the real parent is gone."""
    by_pid = {p.pid: p for p in processes}
    chain: list[Process] = []
    current = by_pid.get(me)
    while current is not None and len(chain) < MAX_ANCESTORS:
        parent = by_pid.get(current.ppid)
        if parent is None or parent.pid == current.pid:
            break
        if parent.started is not None and current.started is not None and parent.started > current.started + 1:
            break
        chain.append(parent)
        if parent.name in SHELLS:
            return chain
        current = parent
    return chain


class ParentWatch:
    """Watches processes (the tool's ancestors) and calls `on_gone` once, from a thread of its own, when one ends."""

    def __init__(
        self,
        watched: list[Process],
        still_there: Callable[[Process], bool],
        on_gone: Callable[[Process], None],
    ) -> None:
        """`still_there` says whether a watched process still runs (the same one: not a reused pid)."""
        self.watched = watched
        self.still_there = still_there
        self.on_gone = on_gone
        self._stop = threading.Event()
        self._thread: threading.Thread | None = None

    def check(self) -> Process | None:
        """The first watched process that has ended, or None."""
        return next((p for p in self.watched if not self.still_there(p)), None)

    def start(self, interval: float = WATCH_SECONDS) -> None:
        """Look every `interval` seconds, until one is gone (then call `on_gone`) or `stop` is called."""
        if not self.watched:
            return

        def loop() -> None:
            while not self._stop.wait(interval):
                gone = self.check()
                if gone is not None:
                    self.on_gone(gone)
                    return

        self._thread = threading.Thread(target=loop, name="parent-watch", daemon=True)
        self._thread.start()

    def stop(self) -> None:
        """End the watch."""
        self._stop.set()


# -- the machine ---------------------------------------------------------------------------------------------------

_FILETIME_EPOCH = 116444736000000000  # 1601-01-01 to 1970-01-01, in 100 ns


@lru_cache(maxsize=1)
def _kernel32() -> Any:
    """kernel32 with the types of the job and process-time calls (the rest come from pcsx2_proc's)."""
    lib = pcsx2_proc._kernel32()
    lib.GetProcessTimes.argtypes = [wintypes.HANDLE, *[ctypes.POINTER(wintypes.FILETIME)] * 4]
    lib.CreateJobObjectW.restype = wintypes.HANDLE
    lib.CreateJobObjectW.argtypes = [wintypes.LPVOID, wintypes.LPCWSTR]
    lib.SetInformationJobObject.argtypes = [wintypes.HANDLE, ctypes.c_int, wintypes.LPVOID, wintypes.DWORD]
    lib.AssignProcessToJobObject.argtypes = [wintypes.HANDLE, wintypes.HANDLE]
    lib.GetCurrentProcess.restype = wintypes.HANDLE
    return lib


def _windows_started(pid: int) -> float | None:
    """When a Windows process started, in epoch seconds; None when it cannot be opened."""
    kernel = _kernel32()
    handle = kernel.OpenProcess(0x1000, False, pid)  # PROCESS_QUERY_LIMITED_INFORMATION
    if not handle:
        return None
    try:
        times = [wintypes.FILETIME() for _ in range(4)]
        if not kernel.GetProcessTimes(handle, *[ctypes.byref(t) for t in times]):
            return None
        ticks = (times[0].dwHighDateTime << 32) | times[0].dwLowDateTime
        return (ticks - _FILETIME_EPOCH) / 1e7
    finally:
        kernel.CloseHandle(handle)


def _elapsed(text: str) -> float | None:
    """`ps`'s elapsed time (`[[dd-]hh:]mm:ss`) in seconds."""
    match = re.fullmatch(r"(?:(\d+)-)?(?:(\d+):)?(\d+):(\d+)", text.strip())
    if not match:
        return None
    days, hours, minutes, seconds = (int(g) if g else 0 for g in match.groups())
    return float(((days * 24 + hours) * 60 + minutes) * 60 + seconds)


def _windows_processes() -> list[Process]:
    """Every Windows process, from a toolhelp snapshot."""
    kernel = _kernel32()
    snapshot = kernel.CreateToolhelp32Snapshot(0x2, 0)  # TH32CS_SNAPPROCESS
    entry = pcsx2_proc._ProcessEntry()
    entry.dwSize = ctypes.sizeof(pcsx2_proc._ProcessEntry)
    kernel.Process32FirstW.argtypes = kernel.Process32NextW.argtypes = [
        wintypes.HANDLE,
        ctypes.POINTER(pcsx2_proc._ProcessEntry),
    ]
    found: list[Process] = []
    try:
        more = kernel.Process32FirstW(snapshot, ctypes.byref(entry))
        while more:
            pid = entry.th32ProcessID
            found.append(Process(pid, entry.th32ParentProcessID, _program(entry.szExeFile), _windows_started(pid)))
            more = kernel.Process32NextW(snapshot, ctypes.byref(entry))
    finally:
        kernel.CloseHandle(snapshot)
    return found


def _posix_processes() -> list[Process]:
    """Every process `ps` shows."""
    done = subprocess.run(
        ["ps", "-A", "-ww", "-o", "pid=,ppid=,etime=,comm="], capture_output=True, text=True, check=False
    )
    now = time.time()
    found: list[Process] = []
    for line in done.stdout.splitlines():
        match = re.match(r"\s*(\d+)\s+(\d+)\s+(\S+)\s+(.*)", line)
        if match:
            elapsed = _elapsed(match.group(3))
            started = None if elapsed is None else now - elapsed
            found.append(Process(int(match.group(1)), int(match.group(2)), _program(match.group(4)), started))
    return found


def list_processes() -> list[Process]:
    """Every process on the machine."""
    return _windows_processes() if sys.platform == "win32" else _posix_processes()


def process_started(pid: int) -> float | None:
    """When a live process started, in epoch seconds (whole seconds on POSIX); None when it is gone or cannot be read.
    A recorded pid plus this time names one process for good, as a pid alone does not: the system hands a dead
    process's pid to a later one."""
    if sys.platform == "win32":
        return _windows_started(pid)
    done = subprocess.run(["ps", "-o", "etime=", "-p", str(pid)], capture_output=True, text=True, check=False)
    elapsed = _elapsed(done.stdout)
    return None if elapsed is None else time.time() - elapsed


def same_process(process: Process) -> bool:
    """Whether `process` still runs: its pid lives and started when it did (so a reused pid does not count)."""
    if not pcsx2_proc.pid_alive(process.pid):
        return False
    if process.started is None:
        return True
    now = process_started(process.pid)
    return now is None or abs(now - process.started) <= START_SLACK


def kill_tree(pid: int) -> None:
    """Stop a process and everything under it, by id (never by name)."""
    if sys.platform == "win32":
        subprocess.run(
            ["taskkill", "/T", "/F", "/PID", str(pid)],
            capture_output=True,
            check=False,
            creationflags=CREATE_NO_WINDOW,
        )
        return
    for process in [*reversed(descendants(list_processes(), pid)), Process(pid, 0, "", None)]:
        with contextlib.suppress(OSError):
            os.kill(process.pid, signal.SIGKILL)


def reap(
    dead_pid: int,
    since: float,
    processes: Callable[[], list[Process]] = list_processes,
    kill: Callable[[int], None] = kill_tree,
    alive: Callable[[int], bool] = pcsx2_proc.pid_alive,
) -> list[Process]:
    """Stop what a dead build tool left running (`leftovers`) and return the roots stopped. Nothing is stopped while
    `dead_pid` lives: a live claimant is never reaped, only a dead one."""
    if alive(dead_pid):
        return []
    roots = leftovers(processes(), dead_pid, since)
    for root in roots:
        kill(root.pid)
    return roots


# -- binding the tool's children to it ------------------------------------------------------------------------------


class _IoCounters(ctypes.Structure):
    """IO_COUNTERS."""

    _fields_ = [(name, ctypes.c_ulonglong) for name in ("r", "w", "o", "rb", "wb", "ob")]


class _BasicLimits(ctypes.Structure):
    """JOBOBJECT_BASIC_LIMIT_INFORMATION."""

    _fields_ = (
        ("PerProcessUserTimeLimit", ctypes.c_longlong),
        ("PerJobUserTimeLimit", ctypes.c_longlong),
        ("LimitFlags", wintypes.DWORD),
        ("MinimumWorkingSetSize", ctypes.c_size_t),
        ("MaximumWorkingSetSize", ctypes.c_size_t),
        ("ActiveProcessLimit", wintypes.DWORD),
        ("Affinity", ctypes.c_size_t),
        ("PriorityClass", wintypes.DWORD),
        ("SchedulingClass", wintypes.DWORD),
    )


class _ExtendedLimits(ctypes.Structure):
    """JOBOBJECT_EXTENDED_LIMIT_INFORMATION."""

    _fields_ = (
        ("BasicLimitInformation", _BasicLimits),
        ("IoInfo", _IoCounters),
        ("ProcessMemoryLimit", ctypes.c_size_t),
        ("JobMemoryLimit", ctypes.c_size_t),
        ("PeakProcessMemoryUsed", ctypes.c_size_t),
        ("PeakJobMemoryUsed", ctypes.c_size_t),
    )


_JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE = 0x2000
_JOB_OBJECT_EXTENDED_LIMIT_INFORMATION = 9
#: The job's handle, kept open for the life of the process: closing it (the process ending) ends the job's processes.
_job: int | None = None


def bind_children() -> bool:
    """Windows: put this process in a new job that ends all its processes when its last handle closes, so every child
    started from now on (and their children) ends when this process does, even when it is killed. Returns whether the
    job is in place (also when it already was); elsewhere it does nothing and returns False (the children's own
    session and `stop_on_signals` do the work there)."""
    global _job
    if sys.platform != "win32":
        return False
    if _job is not None:
        return True
    kernel = _kernel32()
    job = kernel.CreateJobObjectW(None, None)
    if not job:
        return False
    limits = _ExtendedLimits()
    limits.BasicLimitInformation.LimitFlags = _JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE
    if not kernel.SetInformationJobObject(
        job, _JOB_OBJECT_EXTENDED_LIMIT_INFORMATION, ctypes.byref(limits), ctypes.sizeof(limits)
    ) or not kernel.AssignProcessToJobObject(job, kernel.GetCurrentProcess()):
        kernel.CloseHandle(job)
        return False
    _job = job
    return True


def _raise_exit(signum: int, _frame: object) -> None:
    """A signal handler: leave through the normal exit path, which stops the running child's group."""
    raise SystemExit(128 + signum)


def stop_on_signals() -> None:
    """POSIX: turn SIGTERM and SIGHUP into an ordinary exit, so the tool stops its children's process group on the way
    out (a child in a session of its own no longer gets the terminal's hang-up). Nothing on Windows (the job does
    it)."""
    if sys.platform == "win32":
        return
    for number in (signal.SIGTERM, signal.SIGHUP):
        signal.signal(number, _raise_exit)


@lru_cache(maxsize=1)
def _prctl() -> Callable[..., int] | None:
    """Linux's prctl, loaded before any fork (a child must not load libraries between fork and exec)."""
    if not sys.platform.startswith("linux"):
        return None
    try:
        prctl: Callable[..., int] = ctypes.CDLL(None, use_errno=True).prctl
        return prctl
    except (OSError, AttributeError):
        return None


def child_options(nice: int = 0) -> dict[str, object]:
    """The `subprocess.Popen` arguments that tie a child to this process on Linux and macOS: a session of its own (its
    tree is one process group, stopped with `stop_group`), on Linux SIGKILL when this process dies, and `nice`. On
    Windows nothing: the job from `bind_children` covers every child."""
    if sys.platform == "win32":
        return {}
    prctl = _prctl()

    def before_exec() -> None:
        if prctl is not None:
            prctl(1, signal.SIGKILL)  # PR_SET_PDEATHSIG
        if nice:
            os.nice(nice)

    return {"start_new_session": True, "preexec_fn": before_exec}


def stop_group(pid: int) -> None:
    """POSIX: stop a child started with `child_options` and everything in its process group."""
    if sys.platform == "win32":
        return
    try:
        os.killpg(pid, signal.SIGKILL)
    except OSError:
        with contextlib.suppress(OSError):
            os.kill(pid, signal.SIGKILL)


def watch_parent(on_gone: Callable[[Process], None]) -> ParentWatch:
    """Start watching this process's ancestors up to the shell that ran it (`watched_ancestors`)."""
    watch = ParentWatch(watched_ancestors(list_processes(), os.getpid()), same_process, on_gone)
    watch.start()
    return watch
