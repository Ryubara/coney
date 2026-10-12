# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for coney_tools.build_run: reading MSVC, ninja, CMake and ctest output into a short summary, the job count,
the Visual Studio environment, which lane a job takes (from `ninja -n` and `ctest -N` output), and the order of a
run: a claim per phase, released however the phase ends, a wait that gives up with exit code 75. No compiler, no
CMake and no real time: the toolchain is a fake that returns canned output."""

import io
import os
from collections.abc import Callable
from pathlib import Path

import pytest

from coney_tools import build_run
from coney_tools.build_claims import Slots
from coney_tools.build_run import (
    BuildScan,
    Context,
    Options,
    TestScan,
    count_ctest_tests,
    count_ninja_steps,
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


@pytest.mark.parametrize(
    ("output", "steps"),
    [
        ("ninja: no work to do.\n", 0),
        (
            "[1/3] Building CXX object a.obj\n[2/3] Building CXX object b.obj\n[3/3] Linking coney.exe\n",
            3,
        ),
        ("[1/1] Re-running CMake...\n[1/200] Building CXX object a.obj\n", None),
        ("ninja: error: loading 'build.ninja': no such file\n", None),
        ("", None),
    ],
)
def test_ninja_dry_run_steps_are_counted(output: str, steps: int | None) -> None:
    assert count_ninja_steps(output) == steps


@pytest.mark.parametrize(
    ("output", "tests"),
    [("Test project x\n  Test #1: a\n  Test #2: b\n\nTotal Tests: 2\n", 2), ("Total Tests: 0\n", 0), ("garbage", None)],
)
def test_ctest_list_counts_the_selected_tests(output: str, tests: int | None) -> None:
    assert count_ctest_tests(output) == tests


# -- a whole run --------------------------------------------------------------------------------------------------


class FakeToolchain:
    """Canned output for each command, a record of what ran, and a look at the claims while a command runs."""

    def __init__(self, slots: Slots, fast: Slots) -> None:
        self.slots = slots
        self.fast = fast
        self.commands: list[list[str]] = []
        self.output: dict[str, tuple[int, str]] = {
            "ninja": (0, "[1/2] Building CXX object a.obj\n[2/2] Linking CXX executable coney\n"),
            "ctest-list": (0, "Total Tests: 3\n"),
            "configure": (0, "-- Configuring done\n"),
            "build": (0, "[1/2] Building CXX object a.obj\n[2/2] Linking CXX executable coney\n"),
            "test": (0, "1/3 Test #1: a ... Passed 0.1 sec\n100% tests passed, 0 tests failed out of 3\n"),
        }
        self.during: dict[str, tuple[int, int]] = {}
        self.raises: dict[str, BaseException] = {}

    @staticmethod
    def kind(command: list[str]) -> str:
        if command[0] == "ninja":
            return "ninja"
        if command[0] == "ctest":
            return "ctest-list" if "-N" in command else "test"
        return "configure" if "--build" not in command else "build"

    def capture(self, command: list[str], cwd: Path | None) -> tuple[int, str]:
        self.commands.append(command)
        return self.output[self.kind(command)]

    def run(self, command: list[str], on_line: Callable[[str], None]) -> int:
        kind = self.kind(command)
        self.commands.append(command)
        self.during[kind] = (
            sum(1 for r in self.slots.status().slots if r.claim),
            sum(1 for r in self.fast.status().slots if r.claim),
        )
        if kind in self.raises:
            raise self.raises[kind]
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
        self.toolchain = FakeToolchain(self.main, self.fast)
        self.lines: list[str] = []
        self.log = io.StringIO()
        settings: dict[str, object] = {"agent": "alice", "preset": "dev", "checkout": tmp_path / "wt", **options}
        self.options = Options(**settings)  # type: ignore[arg-type]
        self.configured = True
        self.on_sleep: list[Callable[[], None]] = []

    def clock(self) -> float:
        return self.now

    def is_alive(self, pid: int) -> bool:
        return pid in self.alive

    def sleep(self, seconds: float) -> None:
        self.now += seconds
        for action in self.on_sleep:
            action()

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
    big.toolchain.output["ninja"] = (0, "[1/300] Building CXX object a.obj\n[2/300] Building CXX object b.obj\n")
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


def test_a_build_that_would_rerun_cmake_or_cannot_be_counted_takes_a_main_slot(tmp_path: Path) -> None:
    run = Run(tmp_path)
    run.toolchain.output["ninja"] = (0, "[1/1] Re-running CMake...\n[1/3] Building CXX object a.obj\n")
    assert run.go().ok and run.toolchain.during["build"] == (1, 0)


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
    run.toolchain.output["ninja"] = (0, "[1/300] x\n")
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
    run.toolchain.output["ninja"] = (0, "[1/300] x\n")
    occupy(run.main, run, "bob", 101)
    occupy(run.main, run, "carol", 102)
    result = run.go()
    assert result.exit_code == build_run.EXIT_QUEUED == 75 and not result.ok
    text = "\n".join(result.summary)
    assert "still queued, position 1" in text and "try again later" in text
    assert run.main.status().queue == []
    assert run.toolchain.commands and all(run.toolchain.kind(c) == "ninja" for c in run.toolchain.commands)
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
