# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for coney_tools.build_run: reading MSVC, ninja, CMake and ctest output into a short summary, the job count,
the Visual Studio environment, which lane a job takes (from the dry run's explanation of what is out of date and from
`ctest -N` output), and the order of a run: the lock of the build folder, a claim per phase, released however the phase
ends, a wait that gives up with exit code 75. No compiler, no CMake and no real time: the toolchain is a fake that
returns canned output."""

import io
import os
from collections.abc import Callable, Sequence
from pathlib import Path

import pytest

from coney_tools import build_run
from coney_tools.build_claims import Slots
from coney_tools.build_history import History, step_key
from coney_tools.build_run import (
    BuildScan,
    Context,
    Options,
    TestScan,
    count_ctest_tests,
    count_dirty_steps,
    execute,
    jobs_for,
    parse_environment,
)

NOW = 1_800_000_000.0

# -- build output ------------------------------------------------------------------------------------------------

MSVC_FAILURE = """\
[1/412] Building CXX object src/core/CMakeFiles/core.dir/a.cpp.obj
FAILED: [code=2] src/core/CMakeFiles/core.dir/a.cpp.obj
C:\\\\work\\\\coney\\\\src\\\\core\\\\a.cpp(12,5): error C2065: 'foo': undeclared identifier
C:\\\\work\\\\coney\\\\src\\\\core\\\\a.cpp(40): error C2143: syntax error: missing ';' before '}'
C:\\\\work\\\\coney\\\\src\\\\core\\\\a.cpp(40): error C2143: syntax error: missing ';' before '}'
[2/412] Building CXX object src/core/CMakeFiles/core.dir/b.cpp.obj
FAILED: src/core/CMakeFiles/core.dir/b.cpp.obj src/core/CMakeFiles/core.dir/b.cpp.obj.d
C:\\\\work\\\\coney\\\\src\\\\core\\\\b.cpp(7): fatal error C1083: Cannot open include file: 'x.h'
ninja: build stopped: subcommand failed.
"""


def scan_build(text: str) -> BuildScan:
    scan = BuildScan()
    for line in text.splitlines():
        scan.feed(line)
    return scan


def test_msvc_errors_are_listed_once_and_failed_targets_counted() -> None:
    scan = scan_build(MSVC_FAILURE)
    assert scan.errors == [
        "C:\\\\work\\\\coney\\\\src\\\\core\\\\a.cpp(12,5): error C2065: 'foo': undeclared identifier",
        "C:\\\\work\\\\coney\\\\src\\\\core\\\\a.cpp(40): error C2143: syntax error: missing ';' before '}'",
        "C:\\\\work\\\\coney\\\\src\\\\core\\\\b.cpp(7): fatal error C1083: Cannot open include file: 'x.h'",
    ]
    assert scan.failed == ["src/core/CMakeFiles/core.dir/a.cpp.obj", "src/core/CMakeFiles/core.dir/b.cpp.obj"]
    assert scan.progress == (2, 412)


def test_gcc_and_clang_errors_are_found() -> None:
    scan = scan_build(
        "[3/9] Building CXX object x.o\n"
        "FAILED: x.o\n"
        "/w/src/a.cpp:12:5: error: 'foo' was not declared in this scope\n"
        "/w/src/a.cpp:13:1: warning: unused variable 'x' [-Wunused-variable]\n"
        "In file included from /w/src/b.cpp:1:\n"
        "/w/src/b.h:4:10: fatal error: 'missing.h' file not found\n"
    )
    assert [e.split(": ", 1)[1].split(":")[0] for e in scan.errors] == ["error", "fatal error"]
    assert len(scan.errors) == 2


def test_linker_cmake_and_ninja_errors_are_found() -> None:
    scan = scan_build(
        "a.obj : error LNK2019: unresolved external symbol foo referenced in function main\n"
        "LINK : fatal error LNK1181: cannot open input file 'z.lib'\n"
        "CMake Error at CMakeLists.txt:12 (find_package):\n"
        '  Could not find a package configuration file provided by "SDL3"\n'
        "\n"
        "-- Configuring incomplete, errors occurred!\n"
        "ninja: error: loading 'build.ninja': The system cannot find the file specified.\n"
    )
    assert len(scan.errors) == 5
    assert scan.errors[2] == "CMake Error at CMakeLists.txt:12 (find_package):"
    assert scan.errors[3].strip().startswith("Could not find a package configuration file")
    assert scan.errors[4].startswith("ninja: error: loading")


def test_only_the_first_errors_are_kept_and_long_lines_are_cut() -> None:
    text = "".join(f"/w/a.cpp:{n}:1: error: problem number {n} " + "x" * 400 + "\n" for n in range(1, 40))
    scan = scan_build(text)
    assert len(scan.errors) == build_run.MAX_ERRORS
    assert all(len(e) <= build_run.MAX_LINE for e in scan.errors)
    assert scan.error_count == 39


def test_the_tail_keeps_the_last_lines_for_a_failure_with_no_recognised_error() -> None:
    scan = scan_build("".join(f"line {n}\n" for n in range(100)))
    assert scan.errors == [] and scan.tail[-1] == "line 99" and len(scan.tail) == build_run.TAIL_LINES


# -- ctest output ------------------------------------------------------------------------------------------------

CTEST_FAILURE = """\
Test project C:/work/coney/build/dev
    Start 1: core.math
1/4 Test #1: core.math .........................   Passed    0.01 sec
2/4 Test #2: core.parse ........................***Failed    0.30 sec
3/4 Test #3: smoke.help ........................   Passed    0.10 sec
4/4 Test #4: smoke.play ........................***Timeout  60.00 sec

50% tests passed, 2 tests failed out of 4

Total Test time (real) =  60.50 sec

The following tests FAILED:
\t  2 - core.parse (Failed)
\t  4 - smoke.play (Timeout)
Errors while running CTest
"""


def scan_tests(text: str) -> TestScan:
    scan = TestScan()
    for line in text.splitlines():
        scan.feed(line)
    return scan


def test_ctest_counts_and_failed_names() -> None:
    scan = scan_tests(CTEST_FAILURE)
    assert (scan.total, scan.passed, scan.failed) == (4, 2, ["core.parse", "smoke.play"])


CTEST_LOOKALIKES = """\
Test project C:/work/coney/build/dev
1/5 Test #1: core.math .........................   Passed    0.01 sec
\t  7 - Change character ... (Passed)
\t  8 - LOGO decodes ... (Passed)
2/5 Test #2: frame.copies_upright ..............***Timeout  75.00 sec
3/5 Test #3: core.parse ........................   Passed    0.30 sec
\t  9 - a recorder writes a clip through FFmpeg (Passed)
4/5 Test #4: core.text .........................   Passed    0.10 sec
5/5 Test #5: core.mesh .........................   Passed    0.10 sec

80% tests passed, 1 tests failed out of 5

Total Test time (real) =  75.50 sec

The following tests FAILED:
\t  2 - frame.copies_upright (Timeout)
Errors while running CTest
"""


def test_ctest_failed_list_is_only_what_ctest_listed_under_its_heading() -> None:
    # Passing tests' output (or names) shaped like "N - name (state)" above the summary are not failures.
    scan = scan_tests(CTEST_LOOKALIKES)
    assert (scan.total, scan.passed, scan.failed_count, scan.failed) == (5, 4, 1, ["frame.copies_upright"])


def test_ctest_a_failed_tests_output_shaped_like_a_failure_entry_is_not_counted() -> None:
    scan = scan_tests(
        "1/2 Test #1: a ....***Failed    0.01 sec\n"
        "\t  3 - something else (Failed)\n"
        "\t  4 - and another (Timeout)\n"
        "2/2 Test #2: b ....   Passed    0.01 sec\n\n"
        "50% tests passed, 1 tests failed out of 2\n\n"
        "The following tests FAILED:\n"
        "\t  1 - a (Failed)\n"
        "Errors while running CTest\n"
        "\t  5 - after the list (Failed)\n"
    )
    assert (scan.failed_count, scan.failed) == (1, ["a"])


def test_ctest_failed_list_ends_at_a_blank_line_or_the_errors_line() -> None:
    scan = scan_tests(
        "50% tests passed, 1 tests failed out of 2\n\nThe following tests FAILED:\n"
        "\t  1 - a (Failed)\n\n"
        "some later output\n\t  2 - not listed (Failed)\n"
    )
    assert scan.failed == ["a"]


# The names are those of a real run's list (ctest prints a test's labels after its state); the numbers are made up.
CTEST_LABELLED = """\
50% tests passed, 4 tests failed out of 8

The following tests FAILED:
\t2009 - the disc's level3 rooftop chase, driven by the pad, reaches the gallery (Failed) disc playthrough story
\t2017 - the disc's level5: the boss fight, won with the pad, ends the mission (Failed) disc scenes story
\t2071 - the disc's story missions 2, 3, 5 and 6 play each checkpoint without a script error (Failed) disc story
\t2602 - coney.frame_copies_upright (SEGFAULT)
Errors while running CTest
"""

LABELLED_NAMES = [
    "the disc's level3 rooftop chase, driven by the pad, reaches the gallery",
    "the disc's level5: the boss fight, won with the pad, ends the mission",
    "the disc's story missions 2, 3, 5 and 6 play each checkpoint without a script error",
    "coney.frame_copies_upright",
]


def test_ctest_failed_entries_with_labels_after_the_state_are_read() -> None:
    scan = scan_tests(CTEST_LABELLED)
    assert (scan.failed, scan.failed_count) == (LABELLED_NAMES, 4)


def test_ctest_labelled_and_unlabelled_entries_mix_and_a_timeout_follows_a_labelled_one() -> None:
    scan = scan_tests(
        "0% tests passed, 4 tests failed out of 4\n\nThe following tests FAILED:\n"
        "\t  1 - plain (Failed)\n"
        "\t  2 - labelled one (Failed) disc story\n"
        "\t  3 - slow one (Timeout)\n"
        "\t  4 - name (with parentheses) (Timeout) disc slow\n"
        "Errors while running CTest\n"
    )
    assert scan.failed == ["plain", "labelled one", "slow one", "name (with parentheses)"]


def test_ctest_a_short_list_is_completed_from_the_failed_lines() -> None:
    # Fewer names listed than ctest counted: the lines marked failed fill in, so the gap shows.
    scan = scan_tests(
        "1/3 Test #1: a ....***Failed    0.01 sec\n2/3 Test #2: b ....***Timeout   0.01 sec\n"
        "3/3 Test #3: c ....   Passed    0.01 sec\n\n"
        "33% tests passed, 2 tests failed out of 3\n\nThe following tests FAILED:\n"
        "\t  1 - a (Failed)\nErrors while running CTest\n"
    )
    assert (scan.failed, scan.failed_count) == (["a", "b"], 2)


def test_ctest_a_long_failed_list_keeps_every_name_for_the_summary_to_cut() -> None:
    entries = "".join(f"\t  {n} - t{n} (Failed)\n" for n in range(1, 13))
    scan = scan_tests(f"0% tests passed, 12 tests failed out of 12\n\nThe following tests FAILED:\n{entries}")
    assert scan.failed == [f"t{n}" for n in range(1, 13)] and scan.failed_count == 12


def test_ctest_all_passed() -> None:
    scan = scan_tests(
        "1/2 Test #1: a ....   Passed    0.01 sec\n2/2 Test #2: b ....   Passed    0.01 sec\n\n"
        "100% tests passed, 0 tests failed out of 2\n"
    )
    assert (scan.total, scan.passed, scan.failed) == (2, 2, [])


def test_ctest_counts_from_the_lines_when_the_summary_is_missing() -> None:
    scan = scan_tests("1/2 Test #1: a ....   Passed    0.01 sec\n2/2 Test #2: b ....***Failed    0.01 sec\n")
    assert (scan.total, scan.passed, scan.failed) == (2, 1, ["b"])


def test_ctest_finding_no_tests_is_noticed() -> None:
    scan = scan_tests("Test project C:/work/coney/build/dev\n\nNo tests were found!!!\n")
    assert scan.total == 0 and scan.none_found


# -- jobs and environment ----------------------------------------------------------------------------------------


@pytest.mark.parametrize(
    ("cpus", "slots", "override", "expected"),
    [
        (16, 2, None, 8),
        (16, 3, None, 5),
        (4, 2, None, 2),
        (1, 2, None, 1),
        (None, 2, None, 1),
        (16, 2, 3, 3),
        (16, 2, 0, 8),
    ],
)
def test_jobs_share_the_cores_between_the_slots(
    cpus: int | None, slots: int, override: int | None, expected: int
) -> None:
    assert jobs_for(slots, cpus, override) == expected


def test_vcvars_environment_text_is_parsed() -> None:
    text = (
        "Path=C:\\\\VS\\\\bin;C:\\\\Windows\n=C:=C:\\\\work\nINCLUDE=C:\\\\VS\\\\include\nnot a variable line\nEMPTY=\n"
    )
    assert parse_environment(text) == {
        "Path": "C:\\\\VS\\\\bin;C:\\\\Windows",
        "INCLUDE": "C:\\\\VS\\\\include",
        "EMPTY": "",
    }


def test_the_visual_studio_environment_is_found_with_vswhere_and_loaded_once(tmp_path: Path) -> None:
    installer = tmp_path / "Microsoft Visual Studio" / "Installer"
    installer.mkdir(parents=True)
    (installer / "vswhere.exe").write_text("", encoding="utf-8")
    vcvars = tmp_path / "VS" / "VC" / "Auxiliary" / "Build" / "vcvars64.bat"
    vcvars.parent.mkdir(parents=True)
    vcvars.write_text("", encoding="utf-8")
    calls: list[list[str]] = []

    def run(command: list[str]) -> str:
        calls.append(command)
        if command[0].endswith("vswhere.exe"):
            return f"{tmp_path / 'VS'}\n"
        return "Path=C:\\\\VS\\\\bin\nINCLUDE=C:\\\\VS\\\\include\n"

    env = build_run.load_msvc_environment(
        {"ProgramFiles(x86)": str(tmp_path), "Path": "C:\\\\Windows"}, run, win32=True
    )
    assert env["INCLUDE"] == "C:\\\\VS\\\\include"
    assert len(calls) == 2 and str(vcvars) in " ".join(calls[1])


def test_the_environment_is_left_alone_off_windows_or_inside_a_developer_shell() -> None:
    base = {"PATH": "/usr/bin"}
    assert build_run.load_msvc_environment(base, lambda c: pytest.fail("no command"), win32=False) == base
    shell = {"PATH": "/x", "VSCMD_VER": "17.8"}
    assert build_run.load_msvc_environment(shell, lambda c: pytest.fail("no command"), win32=True) == shell


def test_a_missing_visual_studio_is_explained(tmp_path: Path) -> None:
    from coney_tools.config import ConfigError

    with pytest.raises(ConfigError, match="Visual Studio"):
        build_run.load_msvc_environment({"ProgramFiles(x86)": str(tmp_path)}, lambda c: "", win32=True)


# -- the lane a job takes ----------------------------------------------------------------------------------------


def explain(*reasons: str, steps: int = 3) -> str:
    """A dry run's output: the `-d explain` lines (stderr, so first) and then the planned steps."""
    lines = [f"ninja explain: {reason}" for reason in reasons]
    lines += [f"[{n}/{steps}] Building CXX object x{n}.obj" for n in range(1, steps + 1)]
    return "\n".join(lines) + "\n"


TOUCHED = "output src/a.cpp.obj.ddi older than most recent input C:/w/src/a.cpp (8134980954457836 vs 8135031095096621)"


#: A dry run of a header change: sixty sources out of date by themselves, more than the fast lane takes.
BIG = explain(
    *[f"output src/f{n}.cpp.obj.ddi older than most recent input C:/w/h.h (1 vs 2)" for n in range(60)], steps=300
)


@pytest.mark.parametrize(
    ("output", "steps"),
    [
        ("ninja: no work to do.\n", 0),
        # One touched source: its scan step is out of date by itself; the 400 compile steps behind the dyndep step
        # are "dirty" only because a dry run assumes every restat step changes its output, which a real run checks.
        (
            explain(
                "loading dyndep file 'src/CXX.dd'",
                "output _deps/x/all of phony edge with no inputs doesn't exist",
                TOUCHED,
                "src/a.cpp.obj.ddi is dirty",
                "src/b.cpp.obj.modmap is dirty",
                "src/b.cpp.obj is dirty",
                steps=410,
            ),
            1,
        ),
        # A header many sources include: each scan step is out of date by itself.
        (
            explain(
                *[f"output src/f{n}.cpp.obj.ddi older than most recent input C:/w/h.h (1 vs 2)" for n in range(60)]
            ),
            60,
        ),
        # The same reason twice, or a missing output, a changed command line or a stale record each count once.
        (explain(TOUCHED, TOUCHED, steps=2), 1),
        (explain("output src/new.cpp.obj doesn't exist", "command line changed for src/a.cpp.obj", steps=2), 2),
        (explain("recorded mtime of src/a.obj older than most recent input C:/w/a.h (1 vs 2)", steps=2), 1),
        # Steps are planned but nothing says why: it cannot be told.
        ("[1/3] Building CXX object a.obj\n[2/3] Building CXX object b.obj\n[3/3] Linking CXX executable x\n", None),
        (explain("src/a.cpp.obj is dirty", steps=2), None),
        ("[1/1] Re-running CMake...\n[1/200] Building CXX object a.obj\n", None),
        ("ninja: error: loading 'build.ninja': no such file\n", None),
        ("", None),
    ],
)
def test_ninja_dry_run_counts_the_steps_that_are_out_of_date_by_themselves(output: str, steps: int | None) -> None:
    assert count_dirty_steps(output) == steps


@pytest.mark.parametrize(
    ("output", "tests"),
    [("Test project x\n  Test #1: a\n  Test #2: b\n\nTotal Tests: 2\n", 2), ("Total Tests: 0\n", 0), ("garbage", None)],
)
def test_ctest_list_counts_the_selected_tests(output: str, tests: int | None) -> None:
    assert count_ctest_tests(output) == tests


# -- a whole run --------------------------------------------------------------------------------------------------


class FakeToolchain:
    """Canned output for each command, a record of what ran, and a look at the claims while a command runs."""

    def __init__(self, slots: Slots, fast: Slots, lock: Slots) -> None:
        self.slots = slots
        self.fast = fast
        self.lock = lock
        self.commands: list[list[str]] = []
        self.output: dict[str, tuple[int, str]] = {
            "refresh": (0, "ninja: no work to do.\n"),
            "ninja": (0, explain(TOUCHED, steps=2)),
            "ctest-list": (0, "Total Tests: 3\n"),
            "configure": (0, "-- Configuring done\n"),
            "build": (0, "[1/2] Building CXX object a.obj\n[2/2] Linking CXX executable coney\n"),
            "test": (0, "1/3 Test #1: a ... Passed 0.1 sec\n100% tests passed, 0 tests failed out of 3\n"),
        }
        self.during: dict[str, tuple[int, int]] = {}
        #: The step the folder's lock records while each command runs.
        self.lock_steps: dict[str, str] = {}
        self.raises: dict[str, BaseException] = {}
        #: Seconds each kind of command takes, on the fake clock (`advance`).
        self.seconds: dict[str, float] = {}
        self.advance: Callable[[float], None] = lambda seconds: None

    @staticmethod
    def kind(command: list[str]) -> str:
        if command[0] == "ninja":
            return "refresh" if command[1:] == ["build.ninja"] else "ninja"
        if command[0] == "ctest":
            return "ctest-list" if "-N" in command else "test"
        return "configure" if "--build" not in command else "build"

    def capture(self, command: list[str], cwd: Path | None, timeout: float | None = None) -> tuple[int, str]:
        self.commands.append(command)
        return self.output[self.kind(command)]

    def dry_run(self, directory: Path, targets: Sequence[str] = ()) -> tuple[int, str]:
        command = ["ninja", "-n", "-d", "explain", *targets]
        self.commands.append(command)
        return self.output["ninja"]

    def run(self, command: list[str], on_line: Callable[[str], None]) -> int:
        kind = self.kind(command)
        self.commands.append(command)
        self.during[kind] = (
            sum(1 for r in self.slots.status().slots if r.claim),
            sum(1 for r in self.fast.status().slots if r.claim),
        )
        held = [r.claim for r in self.lock.status().slots if r.claim]
        self.lock_steps[kind] = held[0].step if held else "(no lock)"
        if kind in self.raises:
            raise self.raises[kind]
        self.advance(self.seconds.get(kind, 0.0))
        code, text = self.output[kind]
        for line in text.splitlines():
            on_line(line)
        return code


class Run:
    """One `execute` call with everything faked."""

    def __init__(self, tmp_path: Path, **options: object) -> None:
        self.now = NOW
        self.alive = {1}
        self.main = Slots(tmp_path / "claims", 2, clock=self.clock, alive=self.is_alive, sleep=self.sleep)
        self.fast = Slots(tmp_path / "claims", 1, clock=self.clock, alive=self.is_alive, sleep=self.sleep, lane="fast")
        self.lock = Slots(tmp_path / "folder-lock", 1, clock=self.clock, alive=self.is_alive, sleep=self.sleep)
        self.toolchain = FakeToolchain(self.main, self.fast, self.lock)
        self.toolchain.advance = self.advance
        #: The duration history the run reads and writes; None: none.
        self.history: History | None = None
        self.lines: list[str] = []
        self.log = io.StringIO()
        settings: dict[str, object] = {"agent": "alice", "preset": "dev", "checkout": tmp_path / "wt", **options}
        self.options = Options(**settings)  # type: ignore[arg-type]
        self.configured = True
        #: Whether the folder is configured once the lock is ours (what the run it waited for left behind); None: as
        #: it was.
        self.configured_now: bool | None = None
        self.on_sleep: list[Callable[[], None]] = []

    def clock(self) -> float:
        return self.now

    def is_alive(self, pid: int) -> bool:
        return pid in self.alive

    def sleep(self, seconds: float) -> None:
        self.now += seconds
        for action in self.on_sleep:
            action()

    def advance(self, seconds: float) -> None:
        self.now += seconds

    def go(self) -> build_run.Result:
        context = Context(
            slots=self.main,
            fast=self.fast,
            toolchain=self.toolchain,
            emit=self.lines.append,
            log=self.log,
            pid=1,
            cpus=16,
            fast_steps=50,
            fast_tests=20,
            configured=self.configured,
            poll=30.0,
            checkout_lock=self.lock,
            configured_now=lambda: self.configured if self.configured_now is None else self.configured_now,
            history=self.history,
        )
        return execute(self.options, context)


def occupy(slots: Slots, world: Run, agent: str, pid: int) -> None:
    """Hold a slot of `slots` for another agent."""
    world.alive.add(pid)
    ticket = slots.join(agent, f"wt-{agent}", "dev", False, pid)
    assert slots.try_claim(ticket) is not None
    slots.leave(ticket)


def test_a_small_incremental_build_takes_the_fast_lane_and_a_big_one_a_main_slot(tmp_path: Path) -> None:
    run = Run(tmp_path)
    result = run.go()
    assert result.ok and result.exit_code == 0
    assert run.toolchain.during["build"] == (0, 1)  # two ninja steps: fast lane
    big = Run(tmp_path / "big")
    big.toolchain.output["ninja"] = (0, BIG)
    assert big.go().ok
    assert big.toolchain.during["build"] == (1, 0)


def test_the_fast_lane_needs_a_configured_build_and_no_configure_flag(tmp_path: Path) -> None:
    fresh = Run(tmp_path / "fresh")
    fresh.configured = False
    assert fresh.go().ok
    assert fresh.toolchain.during["configure"] == (1, 0)
    assert not any(c[0] == "ninja" for c in fresh.toolchain.commands)  # nothing to dry-run in an unbuilt tree
    forced = Run(tmp_path / "forced", configure=True)
    assert forced.go().ok
    assert forced.toolchain.during["configure"] == (1, 0)


def test_a_build_that_cannot_be_counted_takes_a_main_slot(tmp_path: Path) -> None:
    run = Run(tmp_path)
    run.toolchain.output["ninja"] = (0, "[1/1] Re-running CMake...\n[1/3] Building CXX object a.obj\n")
    assert run.go().ok and run.toolchain.during["build"] == (1, 0)
    failing = Run(tmp_path / "failing")
    failing.toolchain.output["ninja"] = (1, "ninja: error: loading 'build.ninja': no such file\n")
    assert failing.go().ok and failing.toolchain.during["build"] == (1, 0)


def test_the_manifest_is_brought_up_to_date_before_the_dry_run_and_a_failure_of_it_means_a_main_slot(
    tmp_path: Path,
) -> None:
    # CONFIGURE_DEPENDS globs make a plain `ninja -n` stop after "Re-running CMake", so the manifest is refreshed for
    # real (cheap when nothing changed) and the dry run reads a copy of it (the toolchain's dry_run).
    run = Run(tmp_path)
    assert run.go().ok
    assert [run.toolchain.kind(c) for c in run.toolchain.commands] == ["refresh", "ninja", "build"]
    assert run.toolchain.commands[0] == ["ninja", "build.ninja"]
    broken = Run(tmp_path / "broken")
    broken.toolchain.output["refresh"] = (1, "CMake Error at CMakeLists.txt:3 (project):\n  boom\n")
    assert broken.go().ok
    assert [broken.toolchain.kind(c) for c in broken.toolchain.commands] == ["refresh", "build"]
    assert broken.toolchain.during["build"] == (1, 0)  # no measure: a main slot, where the build reports the error


def test_the_measure_is_printed_before_waiting_for_the_slot(tmp_path: Path) -> None:
    run = Run(tmp_path)
    assert run.go().ok
    assert run.lines == ["measured: 1 step out of date, so the fast lane"]
    big = Run(tmp_path / "big")
    big.toolchain.output["ninja"] = (0, BIG)
    assert big.go().ok
    assert big.lines == ["measured: 60 steps out of date, so the main lane"]
    unmeasured = Run(tmp_path / "unmeasured", configure=True)
    assert unmeasured.go().ok and unmeasured.lines == []


def test_the_summary_names_how_many_steps_were_out_of_date(tmp_path: Path) -> None:
    run = Run(tmp_path)
    result = run.go()
    assert "1 step out of date" in result.summary[0] and "fast lane" in result.summary[0]
    big = Run(tmp_path / "big")
    big.toolchain.output["ninja"] = (0, BIG)
    assert "60 steps out of date" in big.go().summary[0]


def test_the_fast_lane_can_be_turned_off(tmp_path: Path) -> None:
    run = Run(tmp_path)
    context = Context(
        slots=run.main,
        fast=run.fast,
        toolchain=run.toolchain,
        emit=run.lines.append,
        log=run.log,
        pid=1,
        cpus=16,
        fast_steps=0,
        fast_tests=0,
        configured=True,
    )
    assert execute(run.options, context).ok
    assert run.toolchain.during["build"] == (1, 0)


def test_configure_build_is_one_claim_and_the_test_run_another(tmp_path: Path) -> None:
    run = Run(tmp_path, test=True)
    run.configured = False
    result = run.go()
    assert result.ok
    kinds = [run.toolchain.kind(c) for c in run.toolchain.commands]
    assert kinds == ["configure", "build", "test"]  # no -R: nothing to count first
    assert run.toolchain.during["configure"] == run.toolchain.during["build"] == (1, 0)
    assert run.toolchain.during["test"] == (1, 0)  # no -R: every test, so a main slot
    assert run.main.status().queue == [] and all(r.claim is None for r in run.main.status().slots)
    assert all(r.claim is None for r in run.fast.status().slots)


def test_a_test_run_of_few_selected_tests_takes_the_fast_lane(tmp_path: Path) -> None:
    run = Run(tmp_path, test=True, regex="core")
    result = run.go()
    assert result.ok
    assert run.toolchain.during["test"] == (0, 1)
    listing = next(c for c in run.toolchain.commands if "-N" in c)
    assert listing[-2:] == ["-R", "core"]


def test_a_test_run_selecting_many_tests_or_all_takes_a_main_slot(tmp_path: Path) -> None:
    many = Run(tmp_path / "many", test=True, regex="smoke")
    many.toolchain.output["ctest-list"] = (0, "Total Tests: 21\n")
    assert many.go().ok and many.toolchain.during["test"] == (1, 0)
    exactly = Run(tmp_path / "exactly", test=True, regex="smoke")
    exactly.toolchain.output["ctest-list"] = (0, "Total Tests: 20\n")
    assert exactly.go().ok and exactly.toolchain.during["test"] == (0, 1)


def test_the_commands_carry_the_preset_the_job_count_and_the_filter(tmp_path: Path) -> None:
    run = Run(tmp_path, test=True, regex="core", preset="ci", configure=True)
    run.go()
    commands = [c for c in run.toolchain.commands if c[0] != "ninja" and "-N" not in c]
    assert commands == [
        ["cmake", "--preset", "ci"],
        ["cmake", "--build", "--preset", "ci", "-j", "8"],
        ["ctest", "--preset", "ci", "-j", "8", "-R", "core"],
    ]


def test_an_explicit_job_count_replaces_the_default(tmp_path: Path) -> None:
    run = Run(tmp_path, jobs=3)
    run.toolchain.output["ninja"] = (0, "[1/90] x\n")
    run.go()
    assert ["cmake", "--build", "--preset", "dev", "-j", "3"] in run.toolchain.commands


def test_a_failed_build_stops_there_summarises_the_errors_and_frees_the_slot(tmp_path: Path) -> None:
    run = Run(tmp_path, test=True)
    run.toolchain.output["build"] = (1, MSVC_FAILURE)
    result = run.go()
    assert not result.ok and result.exit_code == 1
    assert not any(
        run.toolchain.kind(c) == "test" for c in run.toolchain.commands
    )  # tests are not run on a failed build
    text = "\n".join(result.summary)
    assert "build: FAILED (exit 1)" in text and "error C2065" in text and "2 failed" in text
    assert "test: not run" in text
    assert all(r.claim is None for r in run.fast.status().slots + run.main.status().slots)
    assert "FAILED: [code=2]" in run.log.getvalue()  # the log has everything


def test_a_failed_configure_stops_before_the_build(tmp_path: Path) -> None:
    run = Run(tmp_path)
    run.configured = False
    run.toolchain.output["configure"] = (1, "CMake Error at CMakeLists.txt:3 (project):\n  boom\n")
    result = run.go()
    assert result.exit_code == 1
    assert "configure: FAILED (exit 1)" in "\n".join(result.summary)
    assert not any(run.toolchain.kind(c) == "build" for c in run.toolchain.commands)


def test_failed_tests_are_named_with_the_counts(tmp_path: Path) -> None:
    run = Run(tmp_path, test=True)
    run.toolchain.output["test"] = (8, CTEST_FAILURE)
    result = run.go()
    assert result.exit_code == 1
    text = "\n".join(result.summary)
    assert "test: FAILED (exit 8)" in text
    assert "2 passed, 2 failed of 4" in text and "core.parse" in text and "smoke.play" in text


def test_passing_tests_report_their_count(tmp_path: Path) -> None:
    run = Run(tmp_path, test=True)
    result = run.go()
    assert result.ok and "3 passed, 0 failed of 3" in "\n".join(result.summary)


def test_a_run_that_selects_no_tests_fails(tmp_path: Path) -> None:
    run = Run(tmp_path, test=True, regex="nothing")
    run.toolchain.output["ctest-list"] = (0, "Total Tests: 0\n")
    result = run.go()
    assert result.exit_code == 1 and "no tests" in "\n".join(result.summary)
    assert not any(run.toolchain.kind(c) == "test" for c in run.toolchain.commands)


def test_the_slot_is_released_when_a_step_raises(tmp_path: Path) -> None:
    run = Run(tmp_path)
    run.toolchain.raises["build"] = KeyboardInterrupt()
    with pytest.raises(KeyboardInterrupt):
        run.go()
    assert all(r.claim is None for r in run.fast.status().slots + run.main.status().slots)
    assert run.main.status().queue == [] and run.fast.status().queue == []


def test_the_claim_records_the_step_while_it_runs(tmp_path: Path) -> None:
    run = Run(tmp_path, test=True, configure=True)
    seen: list[str] = []
    original = run.toolchain.run

    def watching(command: list[str], on_line: Callable[[str], None]) -> int:
        for row in run.main.status().slots + run.fast.status().slots:
            if row.claim:
                seen.append(row.claim.step)
        return original(command, on_line)

    run.toolchain.run = watching  # type: ignore[method-assign]
    run.go()
    assert seen == ["configure", "build", "test"]


def test_a_full_main_lane_makes_a_big_build_wait_while_the_fast_lane_stays_open(tmp_path: Path) -> None:
    run = Run(tmp_path)
    run.toolchain.output["ninja"] = (0, BIG)
    occupy(run.main, run, "bob", 101)
    occupy(run.main, run, "carol", 102)
    freed: list[int] = []

    def free_a_slot() -> None:
        if not freed:
            freed.append(1)
            claim = run.main.status().slots[0].claim
            assert claim is not None
            run.main.release_claim(claim)

    run.on_sleep.append(free_a_slot)
    result = run.go()
    assert result.ok and run.toolchain.during["build"][0] == 2
    assert any("waiting for a build slot" in line and "place 1 of 1" in line for line in run.lines)
    assert "waited" in "\n".join(result.summary)


def test_a_small_build_does_not_wait_behind_full_main_slots(tmp_path: Path) -> None:
    run = Run(tmp_path)
    occupy(run.main, run, "bob", 101)
    occupy(run.main, run, "carol", 102)
    assert run.go().ok
    assert not any("waiting" in line for line in run.lines)


def test_waiting_longer_than_the_cap_exits_75_and_leaves_the_queue(tmp_path: Path) -> None:
    run = Run(tmp_path, max_wait=45.0)
    run.toolchain.output["ninja"] = (0, BIG)
    occupy(run.main, run, "bob", 101)
    occupy(run.main, run, "carol", 102)
    result = run.go()
    assert result.exit_code == build_run.EXIT_QUEUED == 75 and not result.ok
    text = "\n".join(result.summary)
    assert "still queued, position 1" in text and "try again later" in text
    assert run.main.status().queue == []
    assert run.toolchain.commands and all(run.toolchain.kind(c) in ("refresh", "ninja") for c in run.toolchain.commands)
    assert run.now - NOW >= 45 * 60


def test_a_test_phase_that_cannot_get_a_slot_reports_the_finished_build(tmp_path: Path) -> None:
    run = Run(tmp_path, test=True, max_wait=10.0)
    run.toolchain.output["ctest-list"] = (0, "Total Tests: 400\n")

    original = run.toolchain.run

    def building(command: list[str], on_line: Callable[[str], None]) -> int:
        code = original(command, on_line)
        if run.toolchain.kind(command) == "build":
            occupy(run.main, run, "bob", 101)
            occupy(run.main, run, "carol", 102)
        return code

    run.toolchain.run = building  # type: ignore[method-assign]
    result = run.go()
    assert result.exit_code == 75
    text = "\n".join(result.summary)
    assert "build: ok" in text and "test: still queued" in text


def test_the_summary_is_short_and_starts_with_the_outcome_of_each_step(tmp_path: Path) -> None:
    run = Run(tmp_path, test=True)
    result = run.go()
    assert len(result.summary) <= 8
    assert result.summary[0].startswith("build: ok") and result.summary[-1].startswith("result: ok")


# -- the real toolchain, on harmless commands ----------------------------------------------------------------------


def test_the_process_toolchain_streams_lines_and_returns_the_exit_code(tmp_path: Path) -> None:
    import sys

    toolchain = build_run.ProcessToolchain(tmp_path, os.environ)
    lines: list[str] = []
    code = toolchain.run(
        [sys.executable, "-c", "import sys; print('out'); print('err', file=sys.stderr); sys.exit(3)"], lines.append
    )
    assert code == 3 and sorted(lines) == ["err", "out"]


def test_a_stopped_process_toolchain_starts_nothing_more(tmp_path: Path) -> None:
    import sys

    toolchain = build_run.ProcessToolchain(tmp_path, os.environ)
    toolchain.stop()
    lines: list[str] = []
    assert toolchain.run([sys.executable, "-c", "print('ran')"], lines.append) == 130
    assert lines and "ran" not in lines
    assert toolchain.capture([sys.executable, "-c", "print('ran')"], None)[0] == 130


def test_the_process_toolchain_captures_and_reports_a_missing_program(tmp_path: Path) -> None:
    import sys

    toolchain = build_run.ProcessToolchain(tmp_path, os.environ)
    code, output = toolchain.capture([sys.executable, "-c", "print('hi')"], tmp_path)
    assert code == 0 and output.strip() == "hi"
    assert toolchain.capture(["no-such-program-anywhere"], None)[0] == 127
    seen: list[str] = []
    assert toolchain.run(["no-such-program-anywhere"], seen.append) == 127 and seen


def test_configure_only_configures_and_builds_nothing(tmp_path: Path) -> None:
    run = Run(tmp_path, configure_only=True, test=True)
    result = run.go()
    assert result.ok and [run.toolchain.kind(c) for c in run.toolchain.commands] == ["configure"]
    assert result.summary[0].startswith("configure: ok") and result.summary[-1] == "result: ok"
    assert run.toolchain.during["configure"] == (1, 0)  # a configure is a main-slot job


def test_a_program_is_found_on_the_childs_path_not_this_processs(tmp_path: Path) -> None:
    # Windows resolves a bare program name on the parent's PATH, so the Visual Studio environment's cmake must be
    # looked up by hand before it is started.
    tools = tmp_path / "vs"
    tools.mkdir()
    program = tools / ("fake-cmake.exe" if os.name == "nt" else "fake-cmake")
    program.write_bytes(b"")
    program.chmod(0o755)
    toolchain = build_run.ProcessToolchain(tmp_path, {"PATH": str(tools), "PATHEXT": ".EXE"})
    found, *rest = toolchain._resolve(["fake-cmake", "--build"])
    assert Path(found) == program  # Windows paths compare without case; which() may add PATHEXT's spelling
    assert rest == ["--build"]
    assert toolchain._resolve(["not-there", "-x"]) == ["not-there", "-x"]


# -- one run per build folder ---------------------------------------------------------------------------------------


def hold_folder(run: Run, agent: str, pid: int) -> None:
    """Another agent's run holds the lock of the build folder."""
    occupy(run.lock, run, agent, pid)


def test_a_run_holds_the_lock_of_its_folder_from_configure_to_the_last_test(tmp_path: Path) -> None:
    run = Run(tmp_path, test=True, configure=True)
    assert run.go().ok
    assert run.toolchain.lock_steps == {"configure": "configure", "build": "build", "test": "test"}
    assert all(row.claim is None for row in run.lock.status().slots) and run.lock.status().queue == []


def test_the_lock_is_released_when_the_run_fails_or_is_interrupted(tmp_path: Path) -> None:
    failing = Run(tmp_path / "failing", test=True)
    failing.toolchain.output["build"] = (1, MSVC_FAILURE)
    assert failing.go().exit_code == 1
    assert all(row.claim is None for row in failing.lock.status().slots)
    interrupted = Run(tmp_path / "interrupted")
    interrupted.toolchain.raises["build"] = KeyboardInterrupt()
    with pytest.raises(KeyboardInterrupt):
        interrupted.go()
    assert all(row.claim is None for row in interrupted.lock.status().slots)
    assert interrupted.lock.status().queue == []


def test_a_second_run_in_the_same_folder_waits_for_the_first_and_then_goes(tmp_path: Path) -> None:
    run = Run(tmp_path)
    hold_folder(run, "bob", 101)
    freed: list[int] = []

    def finish_the_other_run() -> None:
        if not freed:
            freed.append(1)
            claim = run.lock.status().slots[0].claim
            assert claim is not None
            run.lock.release_claim(claim)

    run.on_sleep.append(finish_the_other_run)
    result = run.go()
    assert result.ok
    wait = [line for line in run.lines if "build folder" in line]
    assert wait and "bob" in wait[0] and "dev" in wait[0] and "gives up after" in wait[0]
    assert run.toolchain.commands  # it ran, once the folder was free


def test_a_second_run_that_waits_too_long_exits_75_and_runs_nothing(tmp_path: Path) -> None:
    run = Run(tmp_path, max_wait=10.0)
    hold_folder(run, "bob", 101)
    result = run.go()
    assert result.exit_code == build_run.EXIT_QUEUED and not result.ok
    text = "\n".join(result.summary)
    assert "bob" in text and "build folder" in text and "try again later" in text
    assert run.toolchain.commands == []  # not even the dry run: the folder is not ours
    assert run.lock.status().queue == []
    assert all(row.claim is None for row in run.main.status().slots + run.fast.status().slots)


def test_a_run_that_waited_looks_at_the_folder_again_once_the_lock_is_its_own(tmp_path: Path) -> None:
    # The run it waited for configured the folder: this one must not configure it again.
    run = Run(tmp_path, test=False)
    run.configured = False  # as it looked when this run started
    run.configured_now = True  # as it is after the first run
    hold_folder(run, "bob", 101)

    def finish_the_other_run() -> None:
        claim = run.lock.status().slots[0].claim
        if claim is not None:
            run.lock.release_claim(claim)

    run.on_sleep.append(finish_the_other_run)
    assert run.go().ok
    assert [run.toolchain.kind(c) for c in run.toolchain.commands] == ["refresh", "ninja", "build"]  # no configure
    unlocked = Run(tmp_path / "plain")
    unlocked.configured = False
    unlocked.configured_now = True
    assert unlocked.go().ok  # no wait: the same check after taking the lock, so it is not configured either
    assert "configure" not in [unlocked.toolchain.kind(c) for c in unlocked.toolchain.commands]


def test_a_stale_lock_of_a_dead_run_is_taken_over(tmp_path: Path) -> None:
    run = Run(tmp_path)
    hold_folder(run, "bob", 101)
    run.alive.discard(101)
    assert run.go().ok
    assert any("took over" in line for line in run.lines)


def test_a_takeover_reports_the_leftover_processes_it_stopped(tmp_path: Path) -> None:
    run = Run(tmp_path)
    run.lock.reap = lambda claim: [object(), object()]  # the dead run left ctest and a cmake running
    hold_folder(run, "bob", 101)
    run.alive.discard(101)
    assert run.go().ok
    assert any("stopped 2 process trees bob's run left running" in line for line in run.lines)


# -- which tests, which folder -------------------------------------------------------------------------------------


def test_a_label_filter_is_passed_to_ctest_and_to_the_count(tmp_path: Path) -> None:
    run = Run(tmp_path, test=True, label="disc")
    assert run.go().ok
    listing = next(c for c in run.toolchain.commands if "-N" in c)
    assert listing[-2:] == ["-L", "disc"]
    assert ["ctest", "--preset", "dev", "-j", "8", "-L", "disc"] in run.toolchain.commands
    assert run.toolchain.during["test"] == (0, 1)  # three tests: the fast lane


def test_a_regex_and_a_label_together_select_with_both(tmp_path: Path) -> None:
    run = Run(tmp_path, test=True, regex="level80", label="disc")
    run.toolchain.output["ctest-list"] = (0, "Total Tests: 400\n")
    assert run.go().ok
    assert ["ctest", "--preset", "dev", "-j", "8", "-R", "level80", "-L", "disc"] in run.toolchain.commands
    assert run.toolchain.during["test"] == (1, 0)


def test_a_label_that_selects_no_tests_fails_and_says_so(tmp_path: Path) -> None:
    run = Run(tmp_path, test=True, label="nothing")
    run.toolchain.output["ctest-list"] = (0, "Total Tests: 0\n")
    result = run.go()
    assert result.exit_code == 1 and "-L nothing" in "\n".join(result.summary)


def test_a_regex_reaches_ctest_exactly_as_it_was_given(tmp_path: Path) -> None:
    # Test names hold spaces, '+', '.', parentheses and quotes; ctest reads the filter as a CMake regular expression, so
    # the caller escapes with a backslash. Nothing here may rewrite it.
    regex = r"^(L1 \+ R1 with a full rage meter starts rage|coney\.help|<A> draws 256 \+ \(c - 1\): it's here)$"
    run = Run(tmp_path, test=True, regex=regex)
    assert run.go().ok
    listing = next(c for c in run.toolchain.commands if "-N" in c)
    assert listing[-1] == regex and run.toolchain.commands[-1][-2:] == ["-R", regex]


def test_the_process_toolchain_passes_an_argument_through_unchanged(tmp_path: Path) -> None:
    import sys

    regex = r"^(L1 \+ R1 with a full rage meter starts rage|coney\.help|it's \(c - 1\): \"quoted\")$"
    toolchain = build_run.ProcessToolchain(tmp_path, os.environ)
    code, output = toolchain.capture([sys.executable, "-c", "import sys; print(sys.argv[1])", regex], tmp_path)
    assert code == 0 and output.strip() == regex


def test_a_custom_build_folder_changes_the_commands_not_the_presets(tmp_path: Path) -> None:
    folder = tmp_path / "wt" / "build" / "tidy"
    run = Run(tmp_path, test=True, label="disc", build_dir=folder, configure=True)
    assert run.go().ok
    commands = [c for c in run.toolchain.commands if c[0] != "ninja" and "-N" not in c]
    assert commands == [
        ["cmake", "--preset", "dev", "-B", str(folder)],
        ["cmake", "--build", str(folder), "-j", "8"],
        ["ctest", "--test-dir", str(folder), "--output-on-failure", "-j", "8", "-L", "disc"],
    ]
    listing = next(c for c in run.toolchain.commands if "-N" in c)
    assert listing[:4] == ["ctest", "--test-dir", str(folder), "--output-on-failure"]


def test_the_default_build_folder_is_the_presets(tmp_path: Path) -> None:
    options = Options(agent="a", preset="ci", checkout=tmp_path / "wt")
    assert options.folder == tmp_path / "wt" / "build" / "ci"
    assert Options(agent="a", preset="ci", checkout=tmp_path, build_dir=tmp_path / "x").folder == tmp_path / "x"


def test_the_dry_run_reads_a_copy_of_the_manifest_and_removes_it(tmp_path: Path) -> None:
    (tmp_path / "build.ninja").write_text("rule x\n", encoding="utf-8")
    toolchain = build_run.ProcessToolchain(tmp_path, os.environ)
    seen: list[tuple[list[str], bool]] = []

    def capture(command: list[str], cwd: Path | None, timeout: float | None = None) -> tuple[int, str]:
        copy = tmp_path / command[command.index("-f") + 1]
        seen.append((command, copy.is_file() and copy.read_text(encoding="utf-8") == "rule x\n"))
        return 0, "ok"

    toolchain.capture = capture  # type: ignore[method-assign]
    assert toolchain.dry_run(tmp_path) == (0, "ok")
    command, copied = seen[0]
    assert copied and command[:5] == ["ninja", "-n", "-d", "explain", "-f"]
    assert command[5] != "build.ninja"  # a copy: ninja then has no rule to re-run CMake, which stops a dry run
    assert sorted(p.name for p in tmp_path.iterdir()) == ["build.ninja"]


def test_a_missing_manifest_is_a_failed_dry_run(tmp_path: Path) -> None:
    assert build_run.ProcessToolchain(tmp_path, os.environ).dry_run(tmp_path)[0] == 127


# -- ctest output as the Windows ctest prints it: names with spaces, commas and apostrophes --------------------------

REAL_CTEST = """\
1769/2276 Test #1775: the disc's level80: d-pad right with a flash plays 665 ....   Passed   12.34 sec
          Start 1777: coney.help
1770/2276 Test #1776: the disc's level80: Warriors following over a fence climb it (issue #236) ..***Failed   10.00 sec
2276/2276 Test #1780: the disc's level95 plays each checkpoint, player 1 walking ........   Passed  554.46 sec

99% tests passed, 1 tests failed out of 2276

Total Test time (real) = 879.08 sec

The following tests FAILED:
\t1776 - the disc's level80: Warriors following over a fence climb it (issue #236) (Failed)
Errors while running CTest
"""


def test_ctest_names_with_spaces_and_a_failed_count_that_agrees_with_the_summary() -> None:
    scan = scan_tests(REAL_CTEST)
    assert (scan.total, scan.passed, scan.failed_count) == (2276, 2275, 1)
    assert scan.failed == ["the disc's level80: Warriors following over a fence climb it (issue #236)"]


def test_the_failed_count_comes_from_the_summary_even_when_no_name_is_found() -> None:
    scan = scan_tests("99% tests passed, 3 tests failed out of 400\n")
    assert (scan.total, scan.passed, scan.failed_count, scan.failed) == (400, 397, 3, [])


def test_ctest_failure_states_other_than_failed_are_failures_too() -> None:
    scan = scan_tests(
        "1/3 Test #1: slow one ..........***Timeout  60.00 sec\n"
        "2/3 Test #2: not run ..........***Not Run (Disabled)   0.00 sec\n"
        "3/3 Test #3: skipped one ......***Skipped   0.01 sec\n"
    )
    assert scan.failed == ["slow one", "not run"] and scan.failed_count == 2


def test_a_failed_test_run_names_the_failing_tests_and_the_counts_agree(tmp_path: Path) -> None:
    run = Run(tmp_path, test=True)
    run.toolchain.output["test"] = (8, REAL_CTEST)
    result = run.go()
    text = "\n".join(result.summary)
    assert "2275 passed, 1 failed of 2276" in text and "0 failed" not in text
    assert "  failed: the disc's level80: Warriors following over a fence climb it (issue #236)" in result.summary
    assert result.summary[-1].startswith("result: FAILED at test") and "level80" in result.summary[-1]


def test_many_failed_tests_list_the_first_few_and_count_the_rest(tmp_path: Path) -> None:
    lines = "".join(f"{n}/60 Test #{n}: broken thing {n} ....***Failed   0.10 sec\n" for n in range(1, 31))
    lines += "50% tests passed, 30 tests failed out of 60\n\nThe following tests FAILED:\n"
    lines += "".join(f"\t{n} - broken thing {n} (Failed)\n" for n in range(1, 31))
    run = Run(tmp_path, test=True)
    run.toolchain.output["test"] = (8, lines)
    result = run.go()
    failed = next(line for line in result.summary if line.startswith("  failed:"))
    assert "broken thing 1," in failed and "broken thing 8" in failed and "broken thing 9" not in failed
    assert "and 22 more (see the log)" in failed
    assert "30 failed of 60" in "\n".join(result.summary)


# -- a run stopped from outside ----------------------------------------------------------------------------------


def test_a_build_that_exits_non_zero_with_no_failing_step_was_stopped_from_outside(tmp_path: Path) -> None:
    run = Run(tmp_path, test=True)
    run.toolchain.output["build"] = (2, "[1/9] Building CXX object a.obj\n[2/9] Building CXX object b.obj\n")
    result = run.go()
    text = "\n".join(result.summary)
    assert "build: FAILED (exit 2)" in text and "stopped from outside (no failing step found)" in text
    assert "[2/9] Building CXX object b.obj" in text  # where it stopped
    assert result.summary[-1] == "result: FAILED at build (stopped from outside: no failing step found)"


def test_a_build_with_a_compile_error_is_not_called_stopped_from_outside(tmp_path: Path) -> None:
    run = Run(tmp_path)
    run.toolchain.output["build"] = (1, MSVC_FAILURE)
    result = run.go()
    assert "stopped from outside" not in "\n".join(result.summary)
    assert result.summary[-1] == "result: FAILED at build"


def test_a_build_with_a_failed_step_and_no_error_line_names_the_step_not_outside(tmp_path: Path) -> None:
    run = Run(tmp_path)
    run.toolchain.output["build"] = (
        1,
        "[1/2] x\nFAILED: tests/coney_tests.exe\nsomething odd\nninja: build stopped: subcommand failed.\n",
    )
    result = run.go()
    text = "\n".join(result.summary)
    assert "stopped from outside" not in text and "tests/coney_tests.exe" in text


def test_a_ctest_killed_from_outside_says_so(tmp_path: Path) -> None:
    run = Run(tmp_path, test=True)
    run.toolchain.output["test"] = (1, "1/9 Test #1: a ... Passed 0.1 sec\n2/9 Test #2: b ... Passed 0.1 sec\n")
    result = run.go()
    text = "\n".join(result.summary)
    assert "test: FAILED (exit 1)" in text and "stopped from outside (no failing test found)" in text
    assert result.summary[-1] == "result: FAILED at test (stopped from outside: no failing test found)"


def test_a_ctest_that_found_no_tests_is_not_called_stopped_from_outside(tmp_path: Path) -> None:
    run = Run(tmp_path, test=True)
    run.toolchain.output["test"] = (1, "No tests were found!!!\n")
    assert "stopped from outside" not in "\n".join(run.go().summary)


# -- the time budget ----------------------------------------------------------------------------------------------

MINUTE = 60.0


def full_main_lane(run: Run) -> None:
    """A big build (main lane) with both main slots held by others for good."""
    run.toolchain.output["ninja"] = (0, BIG)
    occupy(run.main, run, "bob", 101)
    occupy(run.main, run, "carol", 102)


def history_of(tmp_path: Path, **minutes: list[float]) -> History:
    """A duration history with the given minutes per key (`dev_build_main=[30, 30]` is `dev/build/main`)."""
    history = History(tmp_path / "claims" / "durations.json")
    for name, values in minutes.items():
        for value in values:
            history.record(step_key(*name.split("_")), value * MINUTE)
    return history


def test_a_queued_build_leaves_once_the_wait_plus_its_median_would_pass_the_budget(tmp_path: Path) -> None:
    run = Run(tmp_path, budget=55.0, max_wait=45.0)
    full_main_lane(run)
    run.history = history_of(tmp_path, dev_build_main=[28, 30, 30, 40])
    result = run.go()
    assert result.exit_code == 75
    # 30 min of build left room for about 25 min of waiting, not the 45 of --max-wait.
    assert 24 * MINUTE < run.now - NOW <= 25 * MINUTE  # the queue still counts a busy slot as 1 more min
    assert run.main.status().queue == []
    text = "\n".join(result.summary)
    assert "build: still queued" in text
    assert "the build would take about 30 min (median of 4 runs)" in text
    assert "the budget is 55 min" in text and "min of the call so far" in text


def test_without_a_history_the_typical_durations_are_the_estimate(tmp_path: Path) -> None:
    run = Run(tmp_path, budget=55.0, max_wait=45.0)
    full_main_lane(run)
    result = run.go()
    assert result.exit_code == 75
    assert 39 * MINUTE < run.now - NOW <= 40 * MINUTE  # typical build of 15 min, 1 min left in a busy slot
    assert "about 15 min (typical, no history)" in "\n".join(result.summary)


def test_a_budget_of_zero_leaves_only_the_wait_cap(tmp_path: Path) -> None:
    run = Run(tmp_path, budget=0.0, max_wait=45.0)
    full_main_lane(run)
    result = run.go()
    assert result.exit_code == 75
    assert run.now - NOW >= 45 * MINUTE
    assert "after 45 minutes" in "\n".join(result.summary)


def test_a_step_that_already_runs_is_never_stopped_by_the_budget(tmp_path: Path) -> None:
    run = Run(tmp_path, budget=55.0, test=True, regex="a")
    run.toolchain.seconds = {"build": 70 * MINUTE, "test": 10 * MINUTE}
    result = run.go()
    assert result.ok and result.summary[-1] == "result: ok"
    assert [run.toolchain.kind(c) for c in run.toolchain.commands if run.toolchain.kind(c) in ("build", "test")] == [
        "build",
        "test",
    ]


def test_the_test_phase_counts_the_whole_call_against_the_budget(tmp_path: Path) -> None:
    run = Run(tmp_path, budget=55.0, max_wait=45.0, test=True)
    run.toolchain.seconds = {"build": 40 * MINUTE}
    original = run.toolchain.run

    def building(command: list[str], on_line: Callable[[str], None]) -> int:
        code = original(command, on_line)
        if run.toolchain.kind(command) == "build":
            occupy(run.main, run, "bob", 101)
            occupy(run.main, run, "carol", 102)
        return code

    run.toolchain.run = building  # type: ignore[method-assign]
    result = run.go()
    assert result.exit_code == 75
    # 40 min of build, then the full test run (typical 10 min) leaves after about 5 min of waiting.
    assert 44 * MINUTE < run.now - NOW <= 45 * MINUTE
    text = "\n".join(result.summary)
    assert "build: ok" in text and "test: still queued" in text and "the tests would take about 10 min" in text


def test_the_wait_for_the_build_folder_counts_against_the_budget(tmp_path: Path) -> None:
    run = Run(tmp_path, budget=20.0, max_wait=45.0)
    hold_folder(run, "bob", 101)
    result = run.go()
    assert result.exit_code == 75
    assert 19 * MINUTE < run.now - NOW <= 20 * MINUTE
    assert "the budget is 20 min" in "\n".join(result.summary)
    assert run.toolchain.commands == []


def test_finished_steps_are_recorded_per_preset_step_and_size(tmp_path: Path) -> None:
    run = Run(tmp_path, test=True, regex="a")
    run.configured = False
    run.history = History(tmp_path / "claims" / "durations.json")
    run.toolchain.seconds = {"configure": 2 * MINUTE, "build": 20 * MINUTE, "test": MINUTE}
    assert run.go().ok
    assert run.history.expected("dev/configure/main") == (120.0, 1)
    assert run.history.expected("dev/build/main") == (1200.0, 1)
    assert run.history.expected("dev/test/filtered") == (60.0, 1)

    again = Run(tmp_path, test=True)
    again.history = run.history
    again.toolchain.seconds = {"build": 30.0, "test": 5 * MINUTE}
    assert again.go().ok
    assert again.history.expected("dev/build/fast") == (30.0, 1)  # a small incremental build: the fast lane
    assert again.history.expected("dev/test/full") == (300.0, 1)


def test_a_failed_step_is_not_recorded(tmp_path: Path) -> None:
    run = Run(tmp_path, test=True)
    run.history = History(tmp_path / "claims" / "durations.json")
    run.toolchain.output["build"] = (1, "FAILED: a.obj\n/w/a.cpp:1:1: error: no\n")
    run.toolchain.seconds = {"build": 5 * MINUTE}
    assert not run.go().ok
    assert run.history.expected("dev/build/fast") is None


def test_the_waiting_line_names_the_budget(tmp_path: Path) -> None:
    run = Run(tmp_path, budget=55.0)
    full_main_lane(run)
    run.go()
    assert any("waiting for a build slot" in line and "55 min budget" in line for line in run.lines)


def test_a_long_queue_is_left_at_once_when_its_estimate_cannot_fit_the_budget(tmp_path: Path) -> None:
    # Two slots held and four builds ahead: the queue's own estimate is well past the budget, so the run leaves at its
    # first look instead of sitting through most of the budget (an agent idle that long loses its prompt cache).
    run = Run(tmp_path, budget=45.0, max_wait=40.0)
    full_main_lane(run)
    for n, agent in enumerate(("dan", "erin", "frank", "gina")):
        run.main.join(agent, agent, "dev", False, 200 + n)
        run.alive.add(200 + n)
    run.now += 1  # they joined first
    result = run.go()
    assert result.exit_code == 75
    assert run.now - NOW < MINUTE
    text = "\n".join(result.summary)
    assert "the queue ahead is about" in text and "the budget is 45 min" in text
    assert "--detach" in text and "STATUS.md" in text and "coney-tools build result" in text


def test_a_short_queue_estimate_waits_as_before(tmp_path: Path) -> None:
    run = Run(tmp_path, budget=55.0, max_wait=45.0)
    full_main_lane(run)
    run.history = history_of(tmp_path, dev_build_main=[10])
    freed: list[int] = []

    def free_a_slot() -> None:
        if run.now - NOW >= 5 * MINUTE and not freed:
            freed.append(1)
            claim = run.main.status().slots[0].claim
            assert claim is not None
            run.main.release_claim(claim)

    run.on_sleep.append(free_a_slot)
    assert run.go().ok


# -- --target and the compiler cache ------------------------------------------------------------------------------


def test_targets_are_built_and_measured_alone_and_kept_apart_in_the_history(tmp_path: Path) -> None:
    run = Run(tmp_path, targets=("coney",))
    run.history = History(tmp_path / "claims" / "durations.json")
    result = run.go()
    assert result.ok and result.summary[0].startswith("build coney: ok")
    assert ["ninja", "-n", "-d", "explain", "coney"] in run.toolchain.commands  # the fast lane's measure
    build = next(c for c in run.toolchain.commands if "--build" in c)
    assert build[build.index("--target") :][:2] == ["--target", "coney"]
    assert run.history.expected(step_key("dev", "build", "fast:coney")) is not None
    assert run.history.expected(step_key("dev", "build", "fast")) is None


def test_a_cached_build_has_its_own_history_and_reports_the_cache(tmp_path: Path) -> None:
    run = Run(tmp_path)
    run.history = History(tmp_path / "claims" / "durations.json")

    def go() -> build_run.Result:
        context = Context(
            slots=run.main,
            fast=run.fast,
            toolchain=run.toolchain,
            emit=run.lines.append,
            log=run.log,
            pid=1,
            cpus=16,
            fast_steps=50,
            fast_tests=20,
            configured=True,
            history=run.history,
            cached=True,
            cache_report=lambda: "compiler cache: 2 hits, 0 misses (100.0 %)",
        )
        return execute(run.options, context)

    result = go()
    assert result.ok and result.summary[1] == "compiler cache: 2 hits, 0 misses (100.0 %)"
    assert run.history.expected(step_key("dev", "build", "fast+ccache")) is not None
    run.toolchain.output["build"] = (1, "FAILED: a.obj\n/w/a.cpp:1:1: error: boom\n")
    failed = go()
    assert not failed.ok and failed.summary[1] == "compiler cache: 2 hits, 0 misses (100.0 %)"
