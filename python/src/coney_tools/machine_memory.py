# SPDX-License-Identifier: GPL-3.0-or-later
"""How much memory the machine has free, and who holds it (docs/guides/coney-tools-repo.md#task-check).

`task check` refuses to start below `proof.memory.min_available_gb`: a language server (lua-language-server grew to
19.9 GB, twice) or a runaway editor starved the full suite of memory, and the run died or crawled for an hour with no
verdict. The refusal names the biggest processes so the person knows what to stop. Everything here is best effort and
read-only: a machine that cannot say gives `None` and the check runs, since a fault of the measuring is not a reason to
refuse the work. The parsers are separate functions so that each platform's output can be tested on any machine.
"""

from __future__ import annotations

import ctypes
import re
import subprocess
import sys
from pathlib import Path

GIB = 1024**3
KIB = 1024
#: The biggest processes a refusal names.
HEAVIEST = 3


def parse_meminfo(text: str) -> float | None:
    """The `MemAvailable` of Linux's `/proc/meminfo` in GB, or None when it has none."""
    found = re.search(r"^MemAvailable:\s+(\d+)\s*kB", text, re.MULTILINE)
    return int(found.group(1)) * KIB / GIB if found else None


def parse_vm_stat(text: str) -> float | None:
    """The free, inactive and speculative pages of macOS's `vm_stat` in GB (what the system can give a program without
    swapping), or None when the output is not `vm_stat`'s."""
    size = re.search(r"page size of (\d+) bytes", text)
    if size is None:
        return None
    pages = 0
    for name in ("free", "inactive", "speculative"):
        found = re.search(rf"^Pages {name}:\s+(\d+)\.", text, re.MULTILINE)
        if found is None:
            return None
        pages += int(found.group(1))
    return pages * int(size.group(1)) / GIB


def parse_ps(text: str) -> list[tuple[str, float]]:
    """The processes of `ps -axo rss=,comm=` (resident kilobytes, then the name) as `(name, GB)`, biggest first."""
    found: list[tuple[str, float]] = []
    for line in text.splitlines():
        match = re.match(r"\s*(\d+)\s+(.+?)\s*$", line)
        if match:
            found.append((Path(match.group(2)).name or match.group(2), int(match.group(1)) * KIB / GIB))
    return sorted(found, key=lambda item: item[1], reverse=True)


def parse_tasklist(text: str) -> list[tuple[str, float]]:
    """The processes of `tasklist /FO CSV /NH` (image name, pid, session, session number, `"1,234,567 K"`) as
    `(name, GB)`, biggest first. Only the digits of the memory column count, whatever the language of its unit."""
    found: list[tuple[str, float]] = []
    for line in text.splitlines():
        cells = re.findall(r'"([^"]*)"', line)
        if len(cells) >= 5 and (digits := re.sub(r"\D", "", cells[4])):
            found.append((cells[0], int(digits) * KIB / GIB))
    return sorted(found, key=lambda item: item[1], reverse=True)


def _windows_available() -> float | None:
    """The available physical memory of Windows in GB (`GlobalMemoryStatusEx`)."""

    class Status(ctypes.Structure):
        _fields_ = [
            ("length", ctypes.c_ulong),
            ("load", ctypes.c_ulong),
            ("total", ctypes.c_ulonglong),
            ("available", ctypes.c_ulonglong),
            ("total_page", ctypes.c_ulonglong),
            ("available_page", ctypes.c_ulonglong),
            ("total_virtual", ctypes.c_ulonglong),
            ("available_virtual", ctypes.c_ulonglong),
            ("available_extended", ctypes.c_ulonglong),
        ]

    status = Status()
    status.length = ctypes.sizeof(Status)
    windll = getattr(ctypes, "windll", None)
    if windll is None or not windll.kernel32.GlobalMemoryStatusEx(ctypes.byref(status)):
        return None
    return float(status.available) / GIB


def _run(command: list[str]) -> str | None:
    """The output of a read-only command, or None when it cannot run or fails."""
    try:
        done = subprocess.run(command, capture_output=True, encoding="utf-8", errors="replace", check=False, timeout=30)
    except (OSError, subprocess.SubprocessError):
        return None
    return done.stdout if done.returncode == 0 else None


def available_gb() -> float | None:
    """The memory a program can still get on this machine, in GB, or None when it cannot be read."""
    if sys.platform == "win32":
        return _windows_available()
    if sys.platform == "darwin":
        output = _run(["vm_stat"])
        return parse_vm_stat(output) if output else None
    try:
        return parse_meminfo(Path("/proc/meminfo").read_text(encoding="utf-8"))
    except OSError:
        return None


def heaviest(count: int = HEAVIEST) -> list[tuple[str, float]]:
    """The `count` processes holding the most memory as `(name, GB)`; empty when the machine cannot say."""
    if sys.platform == "win32":
        output = _run(["tasklist", "/FO", "CSV", "/NH"])
        return parse_tasklist(output)[:count] if output else []
    output = _run(["ps", "-axo", "rss=,comm="])
    return parse_ps(output)[:count] if output else []


def describe(processes: list[tuple[str, float]]) -> str:
    """The biggest processes in words (`lua-language-server.exe 19.9 GB, Code.exe 2.1 GB`); empty for none."""
    return ", ".join(f"{name} {size:.1f} GB" for name, size in processes)
