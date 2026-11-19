# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for the memory readings `task check`'s floor uses (coney_tools.machine_memory): each platform's output parsed
on any machine, and the real reading on this one."""

from coney_tools import machine_memory

MEMINFO = "MemTotal:       16318412 kB\nMemFree:          812312 kB\nMemAvailable:    8388608 kB\nBuffers: 1 kB\n"
VM_STAT = (
    "Mach Virtual Memory Statistics: (page size of 16384 bytes)\n"
    "Pages free:                               262144.\n"
    "Pages active:                             100000.\n"
    "Pages inactive:                           262144.\n"
    "Pages speculative:                         65536.\n"
)
TASKLIST = (
    '"lua-language-server.exe","4120","Console","1","20,873,216 K"\n'
    '"Code.exe","888","Console","1","2 202 112 K"\n'
    '"System","4","Services","0","144 K"\n'
    "not a row\n"
)
PS = "  20000000 /usr/bin/lua-language-server\n   1024 bash\nbroken\n  3000000 Code Helper\n"


def test_linux_reads_mem_available() -> None:
    assert machine_memory.parse_meminfo(MEMINFO) == 8.0
    assert machine_memory.parse_meminfo("MemTotal: 1 kB\n") is None


def test_macos_adds_free_inactive_and_speculative_pages() -> None:
    assert machine_memory.parse_vm_stat(VM_STAT) == (262144 + 262144 + 65536) * 16384 / 1024**3
    assert machine_memory.parse_vm_stat("nothing") is None
    assert machine_memory.parse_vm_stat("page size of 4096 bytes\nPages free: 1.\n") is None


def test_the_biggest_processes_come_first_on_each_platform() -> None:
    windows = machine_memory.parse_tasklist(TASKLIST)
    assert [name for name, _ in windows] == ["lua-language-server.exe", "Code.exe", "System"]
    assert round(windows[0][1], 1) == 19.9 and round(windows[1][1], 1) == 2.1
    unix = machine_memory.parse_ps(PS)
    assert [name for name, _ in unix] == ["lua-language-server", "Code Helper", "bash"]
    assert machine_memory.describe(windows[:2]) == "lua-language-server.exe 19.9 GB, Code.exe 2.1 GB"
    assert machine_memory.describe([]) == ""


def test_this_machine_says_a_sane_amount_or_nothing() -> None:
    free = machine_memory.available_gb()
    assert free is None or 0 < free < 100_000
    assert len(machine_memory.heaviest(2)) <= 2
