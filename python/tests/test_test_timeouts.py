# SPDX-License-Identifier: GPL-3.0-or-later
"""The engine's tests have no wall-clock limits: a test's timeout is a hang guard, the same for every test, never a
measure of how fast the machine is (docs/guides/testing.md#no-wall-clock-limits).

The game runs in lockstep in test mode, so a run's result never depends on time; only a timeout can. A timeout a few
times a run's usual length failed `coney.plays_level` whenever the machine ran several builds at once. So every
`TIMEOUT` a CMake file gives a test, and every timeout a test's CMake script gives the process it runs, is the root
CMakeLists.txt's `CONEY_TEST_HANG_GUARD`. Nothing here needs a compiler or a disc: it reads the sources."""

import re
from pathlib import Path

import pytest

# Reads the checkout's own files of these classes (python/tests/repo_reads.py).
pytestmark = pytest.mark.reads("engine")

REPO = Path(__file__).resolve().parents[2]
#: The one hang guard, set in the root CMakeLists.txt.
GUARD = "${CONEY_TEST_HANG_GUARD}"
#: A test's TIMEOUT property inside a `set_tests_properties(...)` call; the value is what follows the keyword.
_PROPERTIES = re.compile(r"set_tests_properties\(([^)]*)\)", re.DOTALL)
_TIMEOUT = re.compile(r"\bTIMEOUT\s+(\S+)")
#: A timeout an `execute_process` in a test's CMake script gives the process it runs.
_PROCESS = re.compile(r"execute_process\(([^)]*)\)", re.DOTALL)
#: A quoted CMake argument, escapes included: a `)` or a `TIMEOUT` inside one (a PASS_REGULAR_EXPRESSION) is not code.
_QUOTED = re.compile(r'"(?:[^"\\]|\\.)*"', re.DOTALL)


def _unquoted(text: str) -> str:
    """`text` with every quoted argument emptied, so a call ends at its own `)`, not at one inside a regex."""
    return _QUOTED.sub('""', text)


def _property_timeouts(text: str) -> list[str]:
    """The value of every TIMEOUT a CMake file's `set_tests_properties` calls set."""
    return [value for block in _PROPERTIES.findall(_unquoted(text)) for value in _TIMEOUT.findall(block)]


def _process_timeouts(text: str) -> list[str]:
    """The value of every TIMEOUT a CMake script's `execute_process` calls set."""
    return [value for block in _PROCESS.findall(_unquoted(text)) for value in _TIMEOUT.findall(block)]


def _test_cmake_files() -> list[Path]:
    """The CMake files that register tests: every CMakeLists.txt under src/ and tests/."""
    return sorted([*(REPO / "src").rglob("CMakeLists.txt"), *(REPO / "tests").rglob("CMakeLists.txt")])


def test_the_timeouts_are_read() -> None:
    text = 'set_tests_properties(a PROPERTIES TIMEOUT 60 PASS_REGULAR_EXPRESSION "x")\n'
    text += "set_tests_properties(b c\n    PROPERTIES LABELS disc TIMEOUT ${CONEY_TEST_HANG_GUARD})\n"
    text += "set_tests_properties(d PROPERTIES WILL_FAIL TRUE)\n"
    assert _property_timeouts(text) == ["60", GUARD]
    # A `)` inside a quoted regex does not end the call, so the TIMEOUT after it is still read; a TIMEOUT inside a
    # quoted string is not one.
    regex = r'set_tests_properties(e PROPERTIES PASS_REGULAR_EXPRESSION "f\\(0\\) \"TIMEOUT 5\"" TIMEOUT 60)' + "\n"
    assert _property_timeouts(regex) == ["60"]
    script = 'execute_process(COMMAND "${X}" TIMEOUT 30 RESULT_VARIABLE r)\nexecute_process(COMMAND y)\n'
    assert _process_timeouts(script) == ["30"]
    assert _process_timeouts('execute_process(COMMAND x "a)b" TIMEOUT 30)\n') == ["30"]


def test_the_root_sets_a_generous_hang_guard() -> None:
    text = (REPO / "CMakeLists.txt").read_text(encoding="utf-8")
    match = re.search(r"set\(CONEY_TEST_HANG_GUARD (\d+)\)", text)
    assert match is not None, "the root CMakeLists.txt sets CONEY_TEST_HANG_GUARD"
    # Ten minutes: far past the slowest run of a check on a machine running several builds (about two minutes).
    assert int(match.group(1)) >= 600


def test_every_test_timeout_is_the_hang_guard() -> None:
    wrong = []
    for path in _test_cmake_files():
        for value in _property_timeouts(path.read_text(encoding="utf-8")):
            if value != GUARD:
                wrong.append(f"{path.relative_to(REPO).as_posix()}: TIMEOUT {value}")
    assert wrong == [], f"a test's TIMEOUT is {GUARD}, a hang guard, never a speed limit:\n" + "\n".join(wrong)


def test_every_timeout_in_a_test_script_is_the_hang_guard() -> None:
    # A test's script that runs a process with its own timeout passes the guard down as a variable.
    wrong = []
    for path in sorted((REPO / "tests").rglob("*.cmake")):
        for value in _process_timeouts(path.read_text(encoding="utf-8")):
            if not value.startswith("${"):
                wrong.append(f"{path.relative_to(REPO).as_posix()}: TIMEOUT {value}")
    assert wrong == [], "a test script's process timeout is the hang guard passed in, not a number:\n" + "\n".join(
        wrong
    )
