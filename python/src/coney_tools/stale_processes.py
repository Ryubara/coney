# SPDX-License-Identifier: GPL-3.0-or-later
"""The processes `coney-tools workspace clean` stops: agent shells left waiting on a run that died, and test runs whose
caller is gone (docs/guides/workspace.md#cleanup).

Two kinds pile up when an agent tool restarts or a run dies:

* **A stale wait.** A background shell loops `until grep -q done build.log; do sleep 20; done`; the run it watches
  died, the line never comes, and the shell waits for ever, keeping its finished agent listed as running. A shell
  (bash, sh, zsh, pwsh, PowerShell) is stale when it is older than `shell_hours` (3), its command holds such a loop,
  and every file the loop reads has not changed for `idle_hours` (1) or does not exist. A loop that names no file it
  can check (`$LOG`, a relative path) is never judged.
* **An orphaned test run.** A `ctest` or `coney_tests` whose parent process is gone (or whose parent pid now belongs
  to a younger process, a reused pid) has no caller left to read its result; an orphaned `ctest` goes on starting
  tests.

Never stopped: the process running this command and its ancestors (the shell that ran `workspace clean`). Only the
topmost stale process of a tree is listed; stopping it stops everything under it. The choice (`find`) takes a plain
process list, a clock and a way to read a file's age, so it is tested on made-up ones.
"""

from __future__ import annotations

import json
import os
import re
import signal
import subprocess
import sys
import time
from collections.abc import Callable, Iterable
from dataclasses import dataclass
from pathlib import Path

#: Shells an agent tool runs commands in.
SHELLS = frozenset({"bash", "sh", "zsh", "dash", "pwsh", "powershell"})
#: Test programs whose orphans are stopped.
TEST_PROGRAMS = frozenset({"ctest", "coney_tests"})
#: A shell younger than this many hours is never stale.
DEFAULT_SHELL_HOURS = 3.0
#: A file a wait reads must be unchanged this many hours for the wait to be stale.
DEFAULT_IDLE_HOURS = 1.0

#: A wait loop: `until`/`while` up to the end of its body (`done`, or a PowerShell `}`), with a sleep in it.
_LOOP = re.compile(r"\b(?:until|while)\b(?P<loop>.*?)(?:\bdone\b|\}|$)", re.DOTALL | re.IGNORECASE)
_SLEEP = re.compile(r"\b(?:sleep|start-sleep)\b", re.IGNORECASE)
#: An absolute path: `C:\...`, `C:/...` or `/c/...` (MSYS), not part of a longer word or a URL.
_PATH = re.compile(r"(?<![\w:/.\\])(?:[A-Za-z]:[\\/]|/(?!/))[^\s'\"`;|&()<>]+")
#: A variable set to an absolute path (`f=/c/x/build.log`, `$log = "C:\x\build.log"`), quoted or not.
_ASSIGNED = re.compile(r"(?:^|[\s;&('\"])\$?(\w+)[ \t]*=[ \t]*\\?[\"']?((?:[A-Za-z]:[\\/]|/(?!/))[^\s'\"`;|&()<>]+)")
#: A variable read (`$f`, `${f}`).
_VARIABLE = re.compile(r"\$\{?(\w+)")
#: Paths that are devices, not files.
_DEVICES = re.compile(r"^/dev/|^/proc/", re.IGNORECASE)


@dataclass(frozen=True)
class Proc:
    """One process: its id, its parent's id, its program (lower case, no `.exe`), when it started and its command."""

    pid: int
    ppid: int
    name: str
    started: float | None  # epoch seconds
    command: str


@dataclass(frozen=True)
class Stale:
    """A process to stop, and why."""

    process: Proc
    why: str


@dataclass(frozen=True)
class Settings:
    """What `find` judges by: the time now, this process's id, and how many seconds ago a file changed (None when it
    does not exist)."""

    now: float
    me: int
    file_age: Callable[[str], float | None]
    shell_hours: float = DEFAULT_SHELL_HOURS
    idle_hours: float = DEFAULT_IDLE_HOURS


def _hours(seconds: float) -> str:
    """Seconds as hours for a report line."""
    return f"{seconds / 3600:.1f} h"


def _candidates(path: str) -> list[str]:
    """The spellings of a path to try: as written, and an MSYS `/c/x` as `C:/x`."""
    match = re.match(r"^/([A-Za-z])/(.*)$", path)
    return [path, f"{match.group(1).upper()}:/{match.group(2)}"] if match else [path]


def _wait_files(command: str) -> list[str] | None:
    """The files the wait loops of a command read, or None when it has no wait loop (a loop that sleeps)."""
    loops = [m.group("loop") for m in _LOOP.finditer(command) if _SLEEP.search(m.group("loop"))]
    if not loops:
        return None
    # A path named in the loop, or held in a variable the loop reads (`f=/c/x/build.log; while ...; grep "$f"`).
    assigned = {m.group(1): m.group(2) for m in _ASSIGNED.finditer(command)}
    paths = [p for loop in loops for p in _PATH.findall(loop)]
    paths += [assigned[name] for loop in loops for name in _VARIABLE.findall(loop) if name in assigned]
    # A command line quotes with `\"`, which leaves a backslash at the end of a path.
    return [p for p in dict.fromkeys(p.rstrip(".,\\") for p in paths) if not _DEVICES.match(p)]


def _stale_wait(process: Proc, settings: Settings) -> str | None:
    """Why a shell is a stale wait, or None."""
    if process.name not in SHELLS or process.started is None:
        return None
    age = settings.now - process.started
    if age < settings.shell_hours * 3600:
        return None
    files = _wait_files(process.command)
    if not files:
        return None
    reasons = []
    for path in files:
        ages = [settings.file_age(spelling) for spelling in _candidates(path)]
        known = [a for a in ages if a is not None]
        if not known:
            reasons.append(f"{path} does not exist")
        elif known[-1] < settings.idle_hours * 3600:
            return None  # one file it reads is still being written
        else:
            reasons.append(f"{path} unchanged for {_hours(known[-1])}")
    return f"a wait loop started {_hours(age)} ago; " + ", ".join(reasons)


def _orphan(process: Proc, by_pid: dict[int, Proc]) -> str | None:
    """Why a test program is an orphan, or None."""
    if process.name not in TEST_PROGRAMS:
        return None
    parent = by_pid.get(process.ppid)
    if parent is None:
        return f"its parent (pid {process.ppid}) is gone"
    if parent.started is not None and process.started is not None and parent.started > process.started + 1:
        return f"its parent (pid {process.ppid}) is gone; the pid now belongs to a younger {parent.name}"
    return None


def _ancestors(pid: int, by_pid: dict[int, Proc]) -> list[int]:
    """The live ancestors of a process, nearest first (a reused parent pid ends the chain)."""
    chain: list[int] = []
    current = by_pid.get(pid)
    while current is not None and len(chain) < 64:
        parent = by_pid.get(current.ppid)
        if parent is None or parent.pid == current.pid or parent.pid in chain:
            break
        if parent.started is not None and current.started is not None and parent.started > current.started + 1:
            break
        chain.append(parent.pid)
        current = parent
    return chain


def find(processes: Iterable[Proc], settings: Settings) -> list[Stale]:
    """The stale waits and orphaned test runs among `processes` (see the module comment), topmost of each tree only,
    never this process or its ancestors."""
    by_pid = {p.pid: p for p in processes}
    protected = {settings.me, *_ancestors(settings.me, by_pid)}
    found: dict[int, Stale] = {}
    for process in sorted(by_pid.values(), key=lambda p: p.pid):
        if process.pid in protected:
            continue
        why = _stale_wait(process, settings) or _orphan(process, by_pid)
        if why is not None:
            found[process.pid] = Stale(process, why)
    return [s for pid, s in found.items() if not any(a in found for a in _ancestors(pid, by_pid))]


def report(stale: list[Stale], apply: bool, kill: Callable[[int], str | None]) -> int:
    """Print the stale processes and, with `apply`, stop each one's tree; the number that could not be stopped."""
    if not stale:
        print("No stale processes.")
        return 0
    print("Stale processes:")
    failures = 0
    for entry in stale:
        verb = "stop" if apply else "would stop"
        process = entry.process
        print(f"  {verb:<13}{process.name} (pid {process.pid}): {entry.why}", flush=True)
        problem = kill(process.pid) if apply else None
        if problem:
            failures += 1
            print(f"    FAILED: {problem}")
    return failures


# -- the machine ---------------------------------------------------------------------------------------------------


def file_age(path: str) -> float | None:
    """Seconds since a file last changed; None when it does not exist; 0 for a folder (never judged unchanged)."""
    try:
        target = Path(path)
        if target.is_dir():
            return 0.0
        return max(0.0, time.time() - target.stat().st_mtime)
    except (OSError, ValueError):
        return None


#: PowerShell that prints every process as JSON, with its command line and start time in Unix milliseconds.
_CIM_LIST = (
    "Get-CimInstance Win32_Process | ForEach-Object { [pscustomobject]@{ pid = $_.ProcessId; "
    "ppid = $_.ParentProcessId; name = $_.Name; command = $_.CommandLine; started = $(if ($_.CreationDate) "
    "{ ([DateTimeOffset]$_.CreationDate).ToUnixTimeMilliseconds() } else { $null }) } } | ConvertTo-Json -Compress"
)


def _program(name: str) -> str:
    """A program's name as compared here: the file name, lower case, without `.exe` (and a login shell's `-`)."""
    return re.split(r"[\\/]", name.strip())[-1].lower().removesuffix(".exe").lstrip("-")


def _elapsed(text: str) -> float | None:
    """`ps`'s elapsed time (`[[dd-]hh:]mm:ss`) in seconds."""
    match = re.fullmatch(r"(?:(\d+)-)?(?:(\d+):)?(\d+):(\d+)", text.strip())
    if not match:
        return None
    days, hours, minutes, seconds = (int(g) if g else 0 for g in match.groups())
    return float(((days * 24 + hours) * 60 + minutes) * 60 + seconds)


def list_processes() -> list[Proc]:
    """Every process on the machine with its command line (Windows through CIM, elsewhere through `ps`); an empty list
    when they cannot be read."""
    if sys.platform == "win32":
        try:
            done = subprocess.run(
                ["powershell", "-NoProfile", "-NonInteractive", "-Command", _CIM_LIST],
                capture_output=True,
                text=True,
                encoding="utf-8",
                errors="replace",
                check=False,
                timeout=120,
                creationflags=0x08000000,  # CREATE_NO_WINDOW
            )
            rows = json.loads(done.stdout or "[]")
        except (OSError, subprocess.TimeoutExpired, ValueError):
            return []
        rows = rows if isinstance(rows, list) else [rows]
        return [
            Proc(
                int(row["pid"]),
                int(row["ppid"] or 0),
                _program(row["name"] or ""),
                row["started"] / 1000 if row.get("started") is not None else None,
                row.get("command") or "",
            )
            for row in rows
        ]
    try:
        done = subprocess.run(
            ["ps", "-A", "-o", "pid=,ppid=,etime=,args="], capture_output=True, text=True, check=False, timeout=60
        )
    except (OSError, subprocess.TimeoutExpired):
        return []
    now = time.time()
    found: list[Proc] = []
    for line in done.stdout.splitlines():
        match = re.match(r"\s*(\d+)\s+(\d+)\s+(\S+)\s+(.*)", line)
        if match:
            elapsed = _elapsed(match.group(3))
            command = match.group(4)
            name = _program(command.split()[0]) if command.split() else ""
            started = None if elapsed is None else now - elapsed
            found.append(Proc(int(match.group(1)), int(match.group(2)), name, started, command))
    return found


def kill_tree(pid: int) -> str | None:
    """Stop a process and everything under it, by id (never by name); what went wrong, or None."""
    if sys.platform == "win32":
        done = subprocess.run(
            ["taskkill", "/T", "/F", "/PID", str(pid)],
            capture_output=True,
            text=True,
            check=False,
            creationflags=0x08000000,  # CREATE_NO_WINDOW
        )
        return None if done.returncode == 0 else (done.stderr or done.stdout).strip() or f"exit {done.returncode}"
    by_pid = {p.pid: p for p in list_processes()}
    under = [p for p in by_pid if pid in _ancestors(p, by_pid)]
    problem = None
    for target in [*under, pid]:
        try:
            os.kill(target, signal.SIGKILL)
        except ProcessLookupError:
            pass
        except OSError as error:
            problem = problem or str(error)
    return problem


def run(apply: bool, shell_hours: float = DEFAULT_SHELL_HOURS, idle_hours: float = DEFAULT_IDLE_HOURS) -> int:
    """`workspace clean`'s process part: list (and with `apply` stop) what is stale; the number of failures."""
    settings = Settings(time.time(), os.getpid(), file_age, shell_hours, idle_hours)
    return report(find(list_processes(), settings), apply, kill_tree)
