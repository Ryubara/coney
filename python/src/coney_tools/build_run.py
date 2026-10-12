# SPDX-License-Identifier: GPL-3.0-or-later
"""Run configure, build and test of a checkout under the build slots, and boil their output down to a short summary
(docs/guides/coney-tools.md#build).

A run has up to two **phases**, each holding its own claim (build_claims.py): configure and build together, then the
test run. A small job (a build whose `ninja -n` shows a few steps, a `ctest -R` that selects a few tests) takes the
single fast-lane slot; everything else takes one of the main slots, and waits in the queue for one. The full output
goes to a log; what is printed is the outcome of each step, the first compile errors and the failed tests.

Everything that touches the machine is behind `Toolchain` (running a command, capturing a short one), so the order of
a run, the lane choice, the claims and the summary are tested with canned output.
"""

from __future__ import annotations

import json
import os
import re
import shutil
import subprocess
import sys
from collections import deque
from collections.abc import Callable, Mapping
from dataclasses import dataclass, field
from pathlib import Path
from typing import Protocol, TextIO

from coney_tools.build_claims import FAST_LANE, MAIN_LANE, BuildClaim, Slots, SlotTimeout, Waiting
from coney_tools.config import ConfigError

#: Exit code of a run that gave up waiting for a slot (EX_TEMPFAIL): do other work and try again later.
EXIT_QUEUED = 75
#: How many compile errors a summary shows, and how long a line may be.
MAX_ERRORS = 8
MAX_LINE = 220
#: How many last lines a failure with no recognised error shows.
TAIL_LINES = 10
#: Seconds a dry run (`ninja -n`, `ctest -N`) may take before the job counts as unmeasured and uses a main slot.
DRY_RUN_SECONDS = 120

# -- reading the output ------------------------------------------------------------------------------------------

_GNU_ERROR = re.compile(r"^\S.*?:\d+(?::\d+)?: (?:fatal )?error: ")
_MSVC_ERROR = re.compile(r"\b(?:fatal )?error [A-Z]{1,5}\d{3,5}:")
_OTHER_ERROR = re.compile(r"^(?:CMake Error|ninja: (?:error|fatal))")
_FAILED_STEP = re.compile(r"^FAILED: (?:\[code=\d+\] )?(\S+)")
_PROGRESS = re.compile(r"^\[(\d+)/(\d+)\] ")
_TEST_LINE = re.compile(r"^\s*\d+/\d+ Test\s+#\d+: (?P<name>\S+)\s+\.+\s*(?P<state>.*?)\s+[\d.]+ sec\s*$")
_TEST_SUMMARY = re.compile(r"(\d+)% tests passed, (\d+) tests? failed out of (\d+)")
_TEST_FAILED_ENTRY = re.compile(r"^\s*\d+ - (?P<name>\S+) \(")


def _cut(line: str) -> str:
    """A line shortened to the width a summary line may have."""
    line = line.rstrip()
    return line if len(line) <= MAX_LINE else line[: MAX_LINE - 3] + "..."


class BuildScan:
    """Reads compiler, linker, CMake and ninja output line by line: the first distinct errors, the steps that
    failed, how far ninja got and the last lines (for a failure the patterns do not recognise)."""

    def __init__(self) -> None:
        self.errors: list[str] = []
        #: How many distinct error lines there were in all, shown or not.
        self.error_count = 0
        self.failed: list[str] = []
        #: The last `[n/m]` ninja printed.
        self.progress: tuple[int, int] | None = None
        self._seen: set[str] = set()
        self._tail: deque[str] = deque(maxlen=TAIL_LINES)
        self._continues = False

    @property
    def tail(self) -> list[str]:
        """The last non-blank lines."""
        return list(self._tail)

    def feed(self, line: str) -> None:
        """Take one line of output."""
        if line.strip():
            self._tail.append(_cut(line))
        if self._continues:
            # A CMake error's message is the indented line after its heading.
            self._continues = False
            if line.startswith((" ", "\t")) and line.strip():
                self._add_error(line)
                return
        progress = _PROGRESS.match(line)
        if progress:
            self.progress = (int(progress.group(1)), int(progress.group(2)))
        failed = _FAILED_STEP.match(line)
        if failed:
            if failed.group(1) not in self.failed:
                self.failed.append(failed.group(1))
            return
        if _GNU_ERROR.search(line) or _MSVC_ERROR.search(line) or _OTHER_ERROR.match(line):
            self._add_error(line)
            self._continues = line.startswith("CMake Error")

    def _add_error(self, line: str) -> None:
        """Count an error line once, and keep the first few."""
        text = _cut(line)
        if text in self._seen:
            return
        if len(self._seen) < 10_000:
            self._seen.add(text)
        self.error_count += 1
        if len(self.errors) < MAX_ERRORS:
            self.errors.append(text)


class TestScan:
    """Reads ctest output: how many tests there were, how many passed and which failed."""

    __test__ = False  # not a pytest class, despite the name

    def __init__(self) -> None:
        self._lines_passed = 0
        self._lines_failed: list[str] = []
        self._summary: tuple[int, int] | None = None
        self._listed_failures: list[str] = []
        self.none_found = False

    @property
    def total(self) -> int:
        """The number of tests run (from ctest's summary, else counted from its lines)."""
        if self._summary is not None:
            return self._summary[0]
        return self._lines_passed + len(self._lines_failed)

    @property
    def failed(self) -> list[str]:
        """The names of the failed tests."""
        return self._listed_failures or self._lines_failed

    @property
    def passed(self) -> int:
        """The number of tests that passed."""
        if self._summary is not None:
            return self._summary[0] - self._summary[1]
        return self._lines_passed

    def feed(self, line: str) -> None:
        """Take one line of output."""
        if "No tests were found" in line:
            self.none_found = True
        summary = _TEST_SUMMARY.search(line)
        if summary:
            self._summary = (int(summary.group(3)), int(summary.group(2)))
            return
        listed = _TEST_FAILED_ENTRY.match(line)
        if listed:
            self._listed_failures.append(listed.group("name"))
            return
        test = _TEST_LINE.match(line)
        if test:
            state = test.group("state")
            if state.startswith("***") and "Skipped" not in state:
                self._lines_failed.append(test.group("name"))
            elif state == "Passed" or "Skipped" in state:
                self._lines_passed += 1


def count_ninja_steps(output: str) -> int | None:
    """The number of steps `ninja -n` would run; 0 for no work, None when it cannot be told (an error, no output, or
    ninja would first re-run CMake, which can change the rest)."""
    if "Re-running CMake" in output:
        return None
    if "no work to do" in output:
        return 0
    steps = [int(m.group(2)) for line in output.splitlines() if (m := _PROGRESS.match(line))]
    return steps[-1] if steps else None


def count_ctest_tests(output: str) -> int | None:
    """The number of tests `ctest -N` lists ("Total Tests: N"); None when it printed no such line."""
    found = re.search(r"^Total Tests: (\d+)", output, re.MULTILINE)
    return int(found.group(1)) if found else None


def jobs_for(slots: int, cpus: int | None, override: int | None = None) -> int:
    """The parallel jobs of one build: the cores shared between the slots (at least 1), or the override."""
    if override:
        return override
    return max(1, (cpus or 1) // max(slots, 1))


# -- the Visual Studio environment --------------------------------------------------------------------------------


def parse_environment(text: str) -> dict[str, str]:
    """The `NAME=value` lines of `set` output as a dict; lines that are not variables (and cmd's `=C:=...` drive
    entries) are skipped."""
    found = {}
    for line in text.splitlines():
        name, separator, value = line.rstrip("\r").partition("=")
        if separator and name and not name.startswith("="):
            found[name] = value
    return found


def env_get(env: Mapping[str, str], name: str) -> str | None:
    """A variable of `env` by name, ignoring case (Windows names are case-insensitive, a dict is not)."""
    for key, value in env.items():
        if key.upper() == name.upper():
            return value
    return None


def load_msvc_environment(
    base: Mapping[str, str], run: Callable[[list[str]], str | None], win32: bool | None = None
) -> dict[str, str]:
    """`base` with the Visual Studio C++ environment loaded: vswhere (not on PATH; it lives in the installer folder
    under Program Files (x86)) names the newest install with the C++ tools, `vcvars64.bat` runs once and the
    variables it set are captured. Unchanged off Windows, and inside a Developer shell. Raises ConfigError when no
    Visual Studio is found."""
    if not (sys.platform == "win32" if win32 is None else win32) or env_get(base, "VSCMD_VER") is not None:
        return dict(base)
    root = env_get(base, "ProgramFiles(x86)") or "C:/Program Files (x86)"
    vswhere = Path(root) / "Microsoft Visual Studio" / "Installer" / "vswhere.exe"
    hint = "install Visual Studio 2022 17.8+ or its Build Tools with Desktop development with C++ (building.md)"
    if not vswhere.is_file():
        raise ConfigError(f"Visual Studio not found ({vswhere} is missing); {hint}")
    installed = run(
        [
            str(vswhere),
            "-latest",
            "-products",
            "*",
            "-requires",
            "Microsoft.VisualStudio.Component.VC.Tools.x86.x64",
            "-property",
            "installationPath",
        ]
    )
    folder = (installed or "").strip().splitlines()
    vcvars = Path(folder[0].strip()) / "VC" / "Auxiliary" / "Build" / "vcvars64.bat" if folder else None
    if vcvars is None or not vcvars.is_file():
        raise ConfigError(f"Visual Studio with the C++ tools not found; {hint}")
    captured = run(["cmd", "/d", "/s", "/c", f'"{vcvars}" >nul && set'])
    loaded = parse_environment(captured or "")
    if not loaded:
        raise ConfigError(f"{vcvars} printed no environment")
    # Windows names are case-insensitive, but a dict is not: fold both to upper case so Path and PATH do not coexist.
    return {**{k.upper(): v for k, v in base.items()}, **{k.upper(): v for k, v in loaded.items()}}


def run_text(command: list[str], env: Mapping[str, str] | None = None) -> str | None:
    """Run a short command and return its stdout, None when it fails. A `cmd /s /c` command is passed as the one
    string cmd wants, quotes and all (a list would be quoted again by Python)."""
    arguments: str | list[str] = command
    if command[:4] == ["cmd", "/d", "/s", "/c"]:
        arguments = f'cmd /d /s /c "{command[4]}"'
    try:
        done = subprocess.run(
            arguments, capture_output=True, text=True, encoding="utf-8", errors="replace", check=False, env=env
        )
    except OSError:
        return None
    return done.stdout if done.returncode == 0 else None


# -- running commands ---------------------------------------------------------------------------------------------


class Toolchain(Protocol):
    """What a run needs from the machine: run a command streaming its lines, capture a short one."""

    def run(self, command: list[str], on_line: Callable[[str], None]) -> int:
        """Run the command in the checkout; each output line (stdout and stderr together) goes to `on_line`."""
        ...

    def capture(self, command: list[str], cwd: Path | None) -> tuple[int, str]:
        """Run a short command (in `cwd`, else the checkout) and return its exit code and output."""
        ...


class ProcessToolchain:
    """The real toolchain: subprocesses of the checkout, with the Visual Studio environment where needed, started at
    below-normal priority so a build does not starve the keyboard."""

    def __init__(self, checkout: Path, env: Mapping[str, str]) -> None:
        self.checkout = checkout
        self.env = dict(env)

    def _resolve(self, command: list[str]) -> list[str]:
        """The command with its program found on the environment's PATH: Windows looks a bare name up on this
        process's own PATH, not the child's, so cmake from the Visual Studio environment would not be found."""
        found = shutil.which(command[0], path=env_get(self.env, "PATH") or None)
        return [found, *command[1:]] if found else command

    def _start(self, command: list[str], cwd: Path) -> subprocess.Popen[str]:
        """Start a command with merged, line-buffered text output, at below-normal priority."""
        if sys.platform == "win32":
            lower: dict[str, object] = {"creationflags": 0x00004000}  # BELOW_NORMAL_PRIORITY_CLASS
        else:
            lower = {"preexec_fn": lambda: os.nice(5)}
        return subprocess.Popen(  # type: ignore[call-overload,no-any-return]
            self._resolve(command),
            cwd=cwd,
            env=self.env,
            stdin=subprocess.DEVNULL,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            encoding="utf-8",
            errors="replace",
            **lower,
        )

    @staticmethod
    def _stop(process: subprocess.Popen[str]) -> None:
        """Stop a child and its own children (the compilers), by id, never by name."""
        if process.poll() is not None:
            return
        if sys.platform == "win32":
            subprocess.run(["taskkill", "/T", "/F", "/PID", str(process.pid)], capture_output=True, check=False)
        else:
            process.kill()

    def run(self, command: list[str], on_line: Callable[[str], None]) -> int:
        """Run the command in the checkout, streaming its lines; the child tree is stopped if this is interrupted."""
        try:
            process = self._start(command, self.checkout)
        except OSError as error:
            on_line(f"{command[0]}: {error.strerror or error}")
            return 127
        try:
            assert process.stdout is not None
            for line in process.stdout:
                on_line(line.rstrip("\r\n"))
            return process.wait()
        except BaseException:
            self._stop(process)
            raise

    def capture(self, command: list[str], cwd: Path | None) -> tuple[int, str]:
        """Run a short command, with a time limit; a timeout or a missing program is a failure."""
        try:
            done = subprocess.run(
                self._resolve(command),
                cwd=cwd or self.checkout,
                env=self.env,
                capture_output=True,
                text=True,
                encoding="utf-8",
                errors="replace",
                check=False,
                timeout=DRY_RUN_SECONDS,
            )
        except (OSError, subprocess.TimeoutExpired) as error:
            return 127, str(error)
        return done.returncode, done.stdout + done.stderr


# -- a run --------------------------------------------------------------------------------------------------------


@dataclass(frozen=True)
class Options:
    """What the caller asked for."""

    agent: str
    preset: str
    checkout: Path
    configure: bool = False
    #: Configure and stop (no build, no tests): what `check changed` needs for its compile database.
    configure_only: bool = False
    test: bool = False
    regex: str | None = None
    priority: bool = False
    #: Minutes to wait for a slot, per phase, before giving up with exit code 75.
    max_wait: float = 45.0
    #: Parallel jobs; None shares the cores between the slots.
    jobs: int | None = None


@dataclass
class Context:
    """The machine a run happens on."""

    slots: Slots
    fast: Slots
    toolchain: Toolchain
    emit: Callable[[str], None]
    log: TextIO
    pid: int
    cpus: int | None
    #: A build of fewer ninja steps, or a `-R` run of at most this many tests, takes the fast lane (0: never).
    fast_steps: int
    fast_tests: int
    #: Whether the build folder has been configured (has a CMake cache).
    configured: bool
    poll: float = 2.0
    report_every: float = 60.0


@dataclass
class Result:
    """The outcome of a run: whether it succeeded, the exit code, and the short summary to print."""

    ok: bool = True
    exit_code: int = 0
    summary: list[str] = field(default_factory=list)


def _duration(seconds: float) -> str:
    """Seconds as a short human duration."""
    if seconds < 90:
        return f"{seconds:.0f}s"
    return f"{seconds / 60:.1f} min"


def _holders_text(holders: list[BuildClaim], now: float) -> str:
    """Who holds the slots, for a waiting line."""
    return ", ".join(f"{h.agent} ({h.preset}, {h.step}, {h.age(now) / 60:.0f} min)" for h in holders) or "nobody"


class _Phase:
    """One claim's worth of steps: choose the lane, wait for the slot, run the commands, free the slot."""

    def __init__(self, options: Options, context: Context, label: str, lane: str) -> None:
        self.options = options
        self.context = context
        self.label = label
        self.lane_name = lane
        self.slots = context.fast if lane == FAST_LANE else context.slots
        self.waited = 0.0
        self.queued: SlotTimeout | None = None

    def run(self, steps: list[tuple[str, list[str], Callable[[str], None]]]) -> list[tuple[str, int, float]]:
        """Hold a slot and run each (step, command, line handler); returns (step, exit code, seconds) for each step
        that ran, stopping at the first failure. A phase that gives up waiting sets `queued` and runs nothing."""
        context, options = self.context, self.options
        started = self.slots.clock()

        def report(waiting: Waiting) -> None:
            context.emit(
                f"waiting for a build slot ({self.lane_name} lane): place {waiting.place} of {waiting.total}, "
                f"about {waiting.minutes:.0f} min; held by {_holders_text(waiting.holders, self.slots.clock())}; "
                f"gives up after {options.max_wait:.0f} min"
            )

        results: list[tuple[str, int, float]] = []
        try:
            with self.slots.hold(
                options.agent,
                options.checkout.name,
                options.preset,
                options.priority,
                context.pid,
                poll=context.poll,
                report=report,
                report_every=context.report_every,
                timeout=options.max_wait * 60,
            ) as claim:
                self.waited = self.slots.clock() - started
                if self.slots.last_takeover is not None:
                    context.emit(f"took over a stale build claim of {self.slots.last_takeover.agent}")
                for step, command, on_line in steps:
                    claim = self.slots.set_step(claim, step) or claim
                    context.log.write(f"==== {step}: {' '.join(command)}\n")
                    began = self.slots.clock()
                    code = context.toolchain.run(command, on_line)
                    context.log.flush()
                    results.append((step, code, self.slots.clock() - began))
                    if code != 0:
                        break
        except SlotTimeout as timeout:
            self.queued = timeout
        return results

    def lane_text(self) -> str:
        """The lane and any wait, for a summary line."""
        waited = f", waited {_duration(self.waited)}" if self.waited >= 1 else ""
        return f"{self.lane_name} lane{waited}"


def _failure_lines(scan: BuildScan) -> list[str]:
    """The indented lines that explain a failed build step: its errors, or its last lines."""
    lines = [f"  {error}" for error in scan.errors]
    if scan.error_count > len(scan.errors):
        lines.append(f"  ... and {scan.error_count - len(scan.errors)} more errors (see the log)")
    if scan.failed:
        shown = ", ".join(scan.failed[:3]) + (", ..." if len(scan.failed) > 3 else "")
        lines.append(f"  {len(scan.failed)} failed steps: {shown}")
    if not scan.errors:
        lines += [f"  {line}" for line in scan.tail[-5:]]
    return lines


def _queued_line(label: str, timeout: SlotTimeout) -> str:
    """The summary line of a phase that gave up waiting."""
    return f"{label}: still queued, position {timeout.position} of {timeout.total}, try again later ({timeout})"


def _choose_build_lane(options: Options, context: Context) -> str:
    """The lane of the configure and build phase: fast for a configured tree whose dry run shows few steps."""
    if options.configure or options.configure_only or not context.configured or context.fast_steps <= 0:
        return MAIN_LANE
    cwd = options.checkout / "build" / options.preset
    code, output = context.toolchain.capture(["ninja", "-n"], cwd)
    steps = count_ninja_steps(output) if code == 0 else None
    return FAST_LANE if steps is not None and steps < context.fast_steps else MAIN_LANE


def _choose_test_lane(options: Options, context: Context) -> tuple[str, int | None]:
    """The lane of the test phase and how many tests the filter selects (None when unknown). Only a `-R` filter can
    be small; a full run always takes a main slot."""
    if options.regex is None:
        return MAIN_LANE, None
    code, output = context.toolchain.capture(["ctest", "--preset", options.preset, "-N", "-R", options.regex], None)
    tests = count_ctest_tests(output) if code == 0 else None
    small = tests is not None and 0 < tests <= context.fast_tests
    return (FAST_LANE if small else MAIN_LANE), tests


def execute(options: Options, context: Context) -> Result:
    """Run configure and build (one claim), then the tests when asked (another claim). Stops at the first failing
    step; the summary has a line per step reached, the first errors or the failed tests, and a final result line.
    The claims are released however this ends, an interrupt included."""
    result = Result()
    jobs = str(jobs_for(context.slots.slots, context.cpus, options.jobs))
    scan = BuildScan()

    def build_line(line: str) -> None:
        context.log.write(line + "\n")
        scan.feed(line)

    # Phase 1: configure (when needed) and build.
    build_steps: list[tuple[str, list[str], Callable[[str], None]]] = []
    if options.configure or options.configure_only or not context.configured:
        build_steps.append(("configure", ["cmake", "--preset", options.preset], build_line))
    if not options.configure_only:
        build_steps.append(("build", ["cmake", "--build", "--preset", options.preset, "-j", jobs], build_line))
    phase = _Phase(options, context, "build", _choose_build_lane(options, context))
    ran = phase.run(build_steps)
    if phase.queued is not None:
        return _gave_up(result, "build", phase.queued)
    for step, code, seconds in ran:
        if code == 0:
            where = f" ({phase.lane_text()})" if step == "build" or len(ran) == 1 else ""
            result.summary.append(f"{step}: ok in {_duration(seconds)}{where}")
        else:
            result.summary.append(f"{step}: FAILED (exit {code}) in {_duration(seconds)} ({phase.lane_text()})")
            result.summary += _failure_lines(scan)
            if step == "configure":
                result.summary.append("build: not run")
            return _failed(result, step, options.test)
    if not options.test or options.configure_only:
        result.summary.append("result: ok")
        return result

    # Phase 2: the tests, with a claim of their own.
    lane, selected = _choose_test_lane(options, context)
    if selected == 0:
        result.summary.append(f"test: FAILED: no tests match -R {options.regex}")
        return _failed(result, "test", False)
    tests = TestScan()

    def test_line(line: str) -> None:
        context.log.write(line + "\n")
        tests.feed(line)

    command = ["ctest", "--preset", options.preset, "-j", jobs]
    if options.regex is not None:
        command += ["-R", options.regex]
    phase = _Phase(options, context, "test", lane)
    ran = phase.run([("test", command, test_line)])
    if phase.queued is not None:
        return _gave_up(result, "test", phase.queued)
    _, code, seconds = ran[0]
    counts = f"{tests.passed} passed, {len(tests.failed)} failed of {tests.total}"
    if code == 0 and not tests.none_found:
        result.summary.append(f"test: ok in {_duration(seconds)} ({phase.lane_text()}): {counts}")
        result.summary.append("result: ok")
        return result
    reason = "no tests were found" if tests.none_found else f"exit {code}"
    result.summary.append(f"test: FAILED ({reason}) in {_duration(seconds)} ({phase.lane_text()}): {counts}")
    if tests.failed:
        result.summary.append("  failed: " + ", ".join(tests.failed))
    return _failed(result, "test", False)


def _failed(result: Result, step: str, tests_skipped: bool) -> Result:
    """Mark a run failed at `step` (exit 1), noting the tests that never ran."""
    if tests_skipped:
        result.summary.append("test: not run")
    result.summary.append(f"result: FAILED at {step}")
    result.ok = False
    result.exit_code = 1
    return result


def _gave_up(result: Result, step: str, timeout: SlotTimeout) -> Result:
    """Mark a run that stopped waiting for a slot: exit code 75, the place, and the way on."""
    result.summary.append(_queued_line(step, timeout))
    result.summary.append(f"result: QUEUED at {step} (exit {EXIT_QUEUED}); do other work and run the command again")
    result.ok = False
    result.exit_code = EXIT_QUEUED
    return result


# -- the checkout -------------------------------------------------------------------------------------------------


def check_preset(checkout: Path, preset: str) -> None:
    """The precheck of the checkout: CMakePresets.json names the preset for configure, build and test. Raises
    ConfigError otherwise, before anything waits for a slot."""
    file = checkout / "CMakePresets.json"
    try:
        data = json.loads(file.read_text(encoding="utf-8"))
    except (OSError, ValueError) as error:
        raise ConfigError(f"{file}: {error}; --dir must be a Coney checkout") from error
    for kind in ("configurePresets", "buildPresets", "testPresets"):
        names = [p.get("name") for p in data.get(kind, [])]
        if preset not in names:
            known = ", ".join(sorted(str(n) for n in names if n and n != "base"))
            raise ConfigError(f"{file}: no {kind} entry named {preset!r}; known: {known}")


def find_programs(env: Mapping[str, str]) -> None:
    """The precheck of the tools: cmake, ctest and ninja are on the environment's PATH. Raises ConfigError naming
    the ones missing."""
    path = env_get(env, "PATH") or ""
    missing = [name for name in ("cmake", "ctest", "ninja") if shutil.which(name, path=path) is None]
    if missing:
        raise ConfigError(f"not found on PATH: {', '.join(missing)}; see docs/guides/building.md")
