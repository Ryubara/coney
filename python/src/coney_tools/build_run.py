# SPDX-License-Identifier: GPL-3.0-or-later
"""Run configure, build and test of a checkout under the build slots, and boil their output down to a short summary
(docs/guides/coney-tools.md#build).

A run first takes the **lock of its build folder** (build_claims.folder_lock): one run per folder at a time, from
configure to the last test, so two runs never link the same executable or write the same files. Then it has up to two
**phases**, each holding its own claim (build_claims.py): configure and build together, then the test run. A small job
(a build with a few out-of-date steps, a `ctest -R` or `-L` that selects a few tests) takes the single fast-lane slot;
everything else takes one of the main slots, and waits in the queue for one. The full output goes to a log; what is
printed is the outcome of each step, the first compile errors and the failed tests.

A run has a **time budget** for the whole call (`--budget`, 45 minutes by default and at most unless the run is
detached, build_cli.py: an agent's prompt cache lasts an hour, and a call that outlasts it pays to write the cache
again). A wait for a slot or for the build folder ends early, with exit code 75, as soon as the time the call has taken
plus what the step it waits for usually takes (the median of its recent runs, build_history.py, else a typical figure)
would pass the budget, or that plus the queue's own estimate of the wait still ahead. A step that has started always
runs to its end: the budget only decides whether to keep waiting.

Everything that touches the machine is behind `Toolchain` (running a command, capturing a short one), so the order of
a run, the lane choice, the claims and the summary are tested with canned output.

A filtered test run is small by its **count and its time**: one disc playthrough can hold a slot for half an hour, so a
run whose selected tests' recorded times (test_costs.py) add up to more than the limit takes a main slot whatever its
count. `--lane main` sends any run to a main slot.
"""

from __future__ import annotations

import json
import os
import re
import shutil
import subprocess
import sys
from collections import deque
from collections.abc import Callable, Mapping, Sequence
from dataclasses import dataclass, field
from pathlib import Path
from typing import Protocol, TextIO

from coney_tools import process_lifetime
from coney_tools.build_claims import (
    FAST_LANE,
    FAST_MINUTES,
    MAIN_LANE,
    TYPICAL_MINUTES,
    BuildClaim,
    Slots,
    SlotTimeout,
    Waiting,
)
from coney_tools.build_history import History, step_key
from coney_tools.config import ConfigError
from coney_tools.test_costs import COST_DATA, TestCosts, parse_test_names

#: Exit code of a run that gave up waiting for a slot (EX_TEMPFAIL): do other work and try again later.
EXIT_QUEUED = 75
#: What a run that gave up waiting tells its caller to do: never wait on in the same call, which would outlast the
#: agent's prompt cache (docs/guides/agents.md#build-waits).
QUEUED_ADVICE = (
    "do other work and run the command again, or start it with --detach, write STATUS.md and stop: "
    "`coney-tools build result` shows the outcome to whoever continues"
)

#: How many compile errors a summary shows, and how long a line may be.
MAX_ERRORS = 8
#: How many failed tests a summary names.
MAX_FAILED_TESTS = 8
MAX_LINE = 220
#: How many last lines a failure with no recognised error shows.
TAIL_LINES = 10
#: Seconds a dry run (`ninja -n -d explain`, `ctest -N`) may take before the job counts as unmeasured (a main slot).
DRY_RUN_SECONDS = 120
#: Seconds the refresh of the build manifest (`ninja build.ninja`, which may re-run CMake) may take before the job
#: counts as unmeasured.
REFRESH_SECONDS = 900

# -- reading the output ------------------------------------------------------------------------------------------

_GNU_ERROR = re.compile(r"^\S.*?:\d+(?::\d+)?: (?:fatal )?error: ")
_MSVC_ERROR = re.compile(r"\b(?:fatal )?error [A-Z]{1,5}\d{3,5}:")
_OTHER_ERROR = re.compile(r"^(?:CMake Error|ninja: (?:error|fatal))")
_FAILED_STEP = re.compile(r"^FAILED: (?:\[code=\d+\] )?(\S+)")
_PROGRESS = re.compile(r"^\[(\d+)/(\d+)\] ")
_EXPLAIN = re.compile(r"^ninja explain: (.*)$")
# A test's name holds spaces, commas and quotes: it runs up to the dots that pad it to the state column (ctest glues the
# state to them when it is a failure: `.....***Failed`).
_TEST_LINE = re.compile(r"^\s*\d+/\d+ Test\s+#\d+: (?P<name>.+) \.{2,}\s*(?P<state>.*?)\s+[\d.]+ sec\s*$")
_TEST_SUMMARY = re.compile(r"(\d+)% tests passed, (\d+) tests? failed out of (\d+)")
_TEST_FAILED_HEADING = "The following tests FAILED:"
# An entry is `N - name (state)`, then the test's labels when it has any (`(Failed) disc story`). The name is lazy and
# anchored on the first `(state)` that only labels (no parentheses) follow, so a name may hold parentheses.
_TEST_FAILED_ENTRY = re.compile(r"^\s*\d+ - (?P<name>.+?) \([^()]*\)(?: [^()]*)?\s*$")


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
    """Reads ctest output: how many tests there were, how many passed, which failed and how many skipped (ctest counts
    a skipped test as passed, and the disc tests skip without the disc)."""

    __test__ = False  # not a pytest class, despite the name

    def __init__(self) -> None:
        self._lines_passed = 0
        self._lines_failed: list[str] = []
        #: The tests whose line says they were skipped.
        self.skipped = 0
        self._summary: tuple[int, int] | None = None
        self._listed_failures: list[str] = []
        # True while the lines are the entries under ctest's "The following tests FAILED:" heading.
        self._in_failed_list = False
        self.none_found = False

    @property
    def total(self) -> int:
        """The number of tests run (from ctest's summary, else counted from its lines)."""
        if self._summary is not None:
            return self._summary[0]
        return self._lines_passed + len(self._lines_failed)

    @property
    def failed(self) -> list[str]:
        """The names of the failed tests: ctest's own list when it printed one, else the lines marked `***Failed` or
        `***Timeout` (a run killed before ctest could print its list). When the list holds fewer names than ctest
        counted, the lines marked failed that it lacks follow it, so the gap shows."""
        names = list(self._listed_failures)
        if len(names) < self.failed_count:
            names += [name for name in self._lines_failed if name not in names]
        return names

    @property
    def failed_count(self) -> int:
        """How many tests failed: ctest's own count when it printed its summary (the names it listed may be fewer
        than that, or none), else the failed lines seen."""
        if self._summary is not None:
            return self._summary[1]
        return len(self._lines_failed)

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
        if line.strip() == _TEST_FAILED_HEADING:
            # A test's own output can look like an entry (`7 - name (Passed)`), and with --output-on-failure even hold
            # the heading, so only the lines under the heading are failures; ctest prints the real heading last, so a
            # later heading replaces an earlier list.
            self._in_failed_list = True
            self._listed_failures = []
            return
        if self._in_failed_list:
            if not line.strip() or line.startswith("Errors while running CTest"):
                # The list ends at a blank line or ctest's closing line. Any other line that is not an entry is
                # skipped, never a reason to drop the entries after it (a label format we do not know).
                self._in_failed_list = False
            else:
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
                self.skipped += "Skipped" in state


def count_dirty_steps(output: str) -> int | None:
    """How many steps are out of date by themselves, from a `ninja -n -d explain` dry run; 0 for no work, None when it
    cannot be told (an error, no output, ninja stopping to re-run CMake, or steps planned with no reason given).

    The planned steps ("[n/m]") are no measure: a dry run cannot ask a `restat` step whether its output really changed,
    so it plans everything behind one (CMake's dependency collation was one, in front of every object, before module
    scanning was turned off: 410 planned steps for one touched source whose real build was 10). The explanation names
    the **cause** of each out-of-date step ("output x older than most recent input y", "output x doesn't exist",
    "command line changed for x"), and a step dirty only because something before it ran just says "x is dirty". The
    causes are what a real build must do whatever the restat steps find: one per touched source, and a header's
    includers one each
    (docs/guides/coney-tools.md#build)."""
    if "Re-running CMake" in output or "ninja: error" in output:
        return None
    if "no work to do" in output:
        return 0
    if not any(_PROGRESS.match(line) for line in output.splitlines()):
        return None
    causes = set()
    for line in output.splitlines():
        reason = _EXPLAIN.match(line.strip())
        if reason is None:
            continue
        text = reason.group(1)
        if text.startswith("loading dyndep") or text.endswith(" is dirty") or "phony edge" in text:
            continue
        causes.add(text)
    # Steps planned but no cause named: an older or odd ninja, so nothing can be said.
    return len(causes) or None


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


# Windows: start console children with no console window of their own, so a build run from a process without a
# console (a background task) does not pop up terminals that take the focus. Zero elsewhere.
NO_WINDOW = process_lifetime.CREATE_NO_WINDOW if sys.platform == "win32" else 0


def run_text(command: list[str], env: Mapping[str, str] | None = None) -> str | None:
    """Run a short command and return its stdout, None when it fails. A `cmd /s /c` command is passed as the one
    string cmd wants, quotes and all (a list would be quoted again by Python)."""
    arguments: str | list[str] = command
    if command[:4] == ["cmd", "/d", "/s", "/c"]:
        arguments = f'cmd /d /s /c "{command[4]}"'
    try:
        done = subprocess.run(
            arguments,
            capture_output=True,
            text=True,
            encoding="utf-8",
            errors="replace",
            check=False,
            env=env,
            creationflags=NO_WINDOW,
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

    def capture(self, command: list[str], cwd: Path | None, timeout: float | None = None) -> tuple[int, str]:
        """Run a short command (in `cwd`, else the checkout) and return its exit code and output."""
        ...

    def dry_run(self, directory: Path, targets: Sequence[str] = ()) -> tuple[int, str]:
        """`ninja -n -d explain` of the build folder (of `targets` only, when given), on a copy of its manifest (see
        ProcessToolchain.dry_run)."""
        ...


class ProcessToolchain:
    """The real toolchain: subprocesses of the checkout, with the Visual Studio environment where needed, started at
    below-normal priority so a build does not starve the keyboard, and tied to this process so they end with it
    (process_lifetime.py: the Windows job, a process group of their own elsewhere)."""

    def __init__(self, checkout: Path, env: Mapping[str, str]) -> None:
        self.checkout = checkout
        self.env = dict(env)
        #: The command running now, for `stop` from another thread (the parent watch).
        self.current: subprocess.Popen[str] | None = None
        #: Set by `stop`: no command starts any more.
        self.stopped = False

    def _resolve(self, command: list[str]) -> list[str]:
        """The command with its program found on the environment's PATH: Windows looks a bare name up on this
        process's own PATH, not the child's, so cmake from the Visual Studio environment would not be found."""
        found = shutil.which(command[0], path=env_get(self.env, "PATH") or None)
        return [found, *command[1:]] if found else command

    @staticmethod
    def _lower() -> dict[str, object]:
        """The arguments that start a child at below-normal priority, with no console window of its own; elsewhere in a
        session of its own, so `_stop` can end its whole tree (process_lifetime.child_options)."""
        if sys.platform == "win32":
            return {"creationflags": 0x00004000 | NO_WINDOW}  # BELOW_NORMAL_PRIORITY_CLASS
        return process_lifetime.child_options(nice=5)

    def _start(self, command: list[str], cwd: Path) -> subprocess.Popen[str]:
        """Start a command with merged, line-buffered text output, at below-normal priority."""
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
            **self._lower(),
        )

    @staticmethod
    def _stop(process: subprocess.Popen[str]) -> None:
        """Stop a child and its own children (the compilers), by id, never by name."""
        if process.poll() is not None:
            return
        if sys.platform == "win32":
            subprocess.run(
                ["taskkill", "/T", "/F", "/PID", str(process.pid)],
                capture_output=True,
                check=False,
                creationflags=NO_WINDOW,
            )
        else:
            process_lifetime.stop_group(process.pid)

    def stop(self) -> None:
        """Stop the command running now and its children, and start no other (safe from another thread)."""
        self.stopped = True
        process = self.current
        if process is not None:
            self._stop(process)

    def run(self, command: list[str], on_line: Callable[[str], None]) -> int:
        """Run the command in the checkout, streaming its lines; the child tree is stopped if this is interrupted."""
        if self.stopped:
            on_line(f"{command[0]}: not started, the build is stopping")
            return 130
        try:
            process = self._start(command, self.checkout)
        except OSError as error:
            on_line(f"{command[0]}: {error.strerror or error}")
            return 127
        self.current = process
        try:
            assert process.stdout is not None
            for line in process.stdout:
                on_line(line.rstrip("\r\n"))
            return process.wait()
        except BaseException:
            self._stop(process)
            raise
        finally:
            self.current = None

    def capture(self, command: list[str], cwd: Path | None, timeout: float | None = None) -> tuple[int, str]:
        """Run a short command, with a time limit (`timeout`, else DRY_RUN_SECONDS); a timeout or a missing program is
        a failure."""
        if self.stopped:
            return 130, "not started, the build is stopping"
        try:
            done = subprocess.run(  # type: ignore[call-overload]
                self._resolve(command),
                cwd=cwd or self.checkout,
                env=self.env,
                stdin=subprocess.DEVNULL,
                capture_output=True,
                text=True,
                encoding="utf-8",
                errors="replace",
                check=False,
                timeout=timeout or DRY_RUN_SECONDS,
                **self._lower(),
            )
        except (OSError, subprocess.TimeoutExpired) as error:
            return 127, str(error)
        return done.returncode, done.stdout + done.stderr

    def dry_run(self, directory: Path, targets: Sequence[str] = ()) -> tuple[int, str]:
        """`ninja -n -d explain` of the build folder, run on a copy of `build.ninja`, for `targets` only when given
        (`--target`). The project's globs are CONFIGURE_DEPENDS, so ninja always wants to re-run CMake first, and a dry
        run stops there having planned nothing; the copy is not an output of any step, so ninja has no manifest to
        rebuild and goes on. The copy sits in the build folder (the manifest's paths are relative to it) and is removed
        again."""
        probe = directory / f".coney-probe-{os.getpid()}.ninja"
        try:
            shutil.copyfile(directory / "build.ninja", probe)
        except OSError as error:
            return 127, str(error)
        try:
            return self.capture(["ninja", "-n", "-d", "explain", "-f", probe.name, *targets], directory)
        finally:
            probe.unlink(missing_ok=True)


# -- a run --------------------------------------------------------------------------------------------------------


@dataclass(frozen=True)
class DiscTests:
    """Whether the run's tests have the player's disc: `source` says where `CONEY_DISC` comes from, or is None with
    `why_not` the reason there is no disc (the disc tests then skip)."""

    source: str | None
    why_not: str = ""

    def line(self, skipped: int) -> str:
        """The summary line about the disc tests, given how many tests ctest reported as skipped."""
        if self.source is not None:
            extra = f"; {skipped} tests still skipped (see the log)" if skipped else ""
            return f"disc tests: ran with the disc (CONEY_DISC from {self.source}){extra}"
        return (
            f"disc tests: SKIPPED ({self.why_not}); set game_dir in coney.local.toml or pass --env CONEY_DISC=<disc> "
            "(docs/guides/testing.md#kinds-of-tests)"
        )


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
    #: ctest's `-L`: run only the tests with a label matching this (`disc` for the tests that need the game's disc).
    label: str | None = None
    #: The build folder when it is not the preset's own `build/<preset>` (`--build-dir`), as an absolute path.
    build_dir: Path | None = None
    priority: bool = False
    #: Minutes to wait for a slot, per phase, before giving up with exit code 75.
    max_wait: float = 45.0
    #: Minutes the whole call may take, waits and steps together: a wait that would push the call past it ends with
    #: exit code 75 (0: no budget; the command line's default is 55).
    budget: float = 0.0
    #: Parallel jobs; None shares the cores between the slots.
    jobs: int | None = None
    #: The targets to build (`--target`, such as `coney` for a play build); empty: everything.
    targets: tuple[str, ...] = ()
    #: Whether the disc tests have the disc (set by the caller, who passes it to the run's environment); None: the
    #: summary says nothing of it.
    disc: DiscTests | None = None
    #: `main` sends both phases to a main slot without measuring (`--lane main`); None chooses by size.
    lane: str | None = None

    @property
    def folder(self) -> Path:
        """The build folder of the run."""
        return self.build_dir or self.checkout / "build" / self.preset

    @property
    def filters(self) -> list[str]:
        """The ctest options that choose the tests: `-R` and `-L`."""
        return [*(["-R", self.regex] if self.regex is not None else []), *(["-L", self.label] if self.label else [])]

    @property
    def filter_text(self) -> str:
        """The test filter as a person typed it, for a message."""
        return " ".join(self.filters)


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
    #: The lock of the build folder: one run at a time per folder, from configure to the last test. None: no lock.
    checkout_lock: Slots | None = None
    #: Looks at the build folder again once the lock is held: whether it is configured by then (the run that held the
    #: lock before may have configured it). None: `configured` stands.
    configured_now: Callable[[], bool] | None = None
    #: How long each kind of step took before, for the time budget's estimate, and where finished steps are recorded.
    #: None: typical figures, nothing recorded.
    history: History | None = None
    #: When the call started (the clock of `slots`), the start of the budget; set by `execute` when None.
    call_started: float | None = None
    #: Whether the compiler cache is on for this run (compiler_cache.py): a build's history is kept apart then, as a
    #: build that takes its objects from the cache is several times faster than one that compiles them.
    cached: bool = False
    #: The summary line on the compiler cache after the build step (its hits and misses), or None for none.
    cache_report: Callable[[], str | None] | None = None
    #: Each test's recorded time, read for the test lane and updated after a test run (test_costs.py); None: the lane
    #: goes by the count alone.
    test_costs: TestCosts | None = None
    #: A filtered test run whose selected tests' recorded times add up to more seconds than this takes a main slot
    #: (0: the count alone decides).
    fast_test_seconds: float = 0.0


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


#: What a budget message calls each step.
_STEP_NAMES = {"configure": "the configure", "build": "the build", "test": "the tests"}


def _estimate(context: Context, options: Options, steps: list[tuple[str, str]], lane: str) -> tuple[float, str]:
    """The seconds a phase's (step, size) pairs usually take together, and what that rests on: the median of each
    step's recent runs in the history, else the typical figure of its lane (build_claims.TYPICAL_MINUTES)."""
    total = 0.0
    bases = []
    for step, size in steps:
        found = context.history.expected(step_key(options.preset, step, size)) if context.history else None
        if found is not None:
            total += found[0]
            bases.append(f"median of {found[1]} run{'s' if found[1] != 1 else ''}")
        else:
            total += 60 * (FAST_MINUTES if lane == FAST_LANE else TYPICAL_MINUTES.get(step, TYPICAL_MINUTES["build"]))
            bases.append("typical, no history")
    if len(set(bases)) <= 1:
        return total, bases[0] if bases else "nothing to run"
    return total, "; ".join(f"{step}: {basis}" for (step, _), basis in zip(steps, bases, strict=True))


def _budget_check(
    options: Options, context: Context, clock: Callable[[], float], what: str | None, seconds: float, basis: str
) -> Callable[[float, float], str | None] | None:
    """The `abandon` callback of a wait under the time budget (None without a budget): it answers with the reason, the
    numbers in it, once the call so far plus `seconds` (what `what` would take; None for a wait that has no step of
    its own) passes the budget, or as soon as the queue's own estimate of the wait still ahead, added to those, does:
    a wait that cannot end in time is left at once, not after sitting through most of the budget."""
    if options.budget <= 0:
        return None
    limit = options.budget * 60

    def abandon(waited: float, expected: float) -> str | None:
        used = clock() - (context.call_started if context.call_started is not None else clock())
        done = f"time budget: {used / 60:.0f} min of the call so far"
        step = "" if what is None else f"; {what} would take about {seconds / 60:.0f} min ({basis})"
        if used + seconds > limit:
            return f"{done}{step} and the budget is {options.budget:.0f} min"
        if used + expected + seconds > limit:
            queue = f"; the queue ahead is about {expected / 60:.0f} min"
            return f"{done}{queue}{step} and the budget is {options.budget:.0f} min"
        return None

    return abandon


def _gives_up(options: Options) -> str:
    """When a wait gives up, for a waiting line."""
    budget = f" or before passing the {options.budget:.0f} min budget" if options.budget > 0 else ""
    return f"gives up after {options.max_wait:.0f} min{budget}"


class _Phase:
    """One claim's worth of steps: choose the lane, wait for the slot, run the commands, free the slot."""

    def __init__(
        self,
        options: Options,
        context: Context,
        label: str,
        lane: str,
        note: Callable[[str], None] = lambda step: None,
        size: str | None = None,
    ) -> None:
        self.options = options
        self.context = context
        self.label = label
        self.lane_name = lane
        #: How big the phase's job is, for the history's keys: a test run's "full" or "filtered"; None: the lane for a
        #: build, "main" for a configure.
        self.size = size
        self.slots = context.fast if lane == FAST_LANE else context.slots
        #: Told the step each command starts, for the folder lock's owner file.
        self.note = note
        self.waited = 0.0
        self.queued: SlotTimeout | None = None

    def size_of(self, step: str) -> str:
        """The size part of a step's history key: a configure is always "main" (it does the same work in either lane),
        a build its lane, then `:` and its targets when it builds only some (`main:coney`), then `+ccache` when the
        compiler cache is on; a test run "full" or "filtered"."""
        if self.size is not None:
            return self.size
        if step == "configure":
            return MAIN_LANE
        targets = ":" + "+".join(sorted(self.options.targets)) if self.options.targets else ""
        return self.lane_name + targets + ("+ccache" if self.context.cached else "")

    def run(self, steps: list[tuple[str, list[str], Callable[[str], None]]]) -> list[tuple[str, int, float]]:
        """Hold a slot and run each (step, command, line handler); returns (step, exit code, seconds) for each step
        that ran, stopping at the first failure. A phase that gives up waiting sets `queued` and runs nothing."""
        context, options = self.context, self.options
        started = self.slots.clock()

        def report(waiting: Waiting) -> None:
            context.emit(
                f"waiting for a build slot ({self.lane_name} lane): place {waiting.place} of {waiting.total}, "
                f"about {waiting.minutes:.0f} min; held by {_holders_text(waiting.holders, self.slots.clock())}; "
                f"{_gives_up(options)}"
            )

        # The budget's estimate of this phase: its steps' usual durations together.
        keyed = [(step, self.size_of(step)) for step, _, _ in steps]
        seconds, basis = _estimate(context, options, keyed, self.lane_name)
        what = " and ".join(_STEP_NAMES.get(step, step) for step, _ in keyed)
        abandon = _budget_check(options, context, self.slots.clock, what, seconds, basis)
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
                abandon=abandon,
            ) as claim:
                self.waited = self.slots.clock() - started
                if self.slots.last_takeover is not None:
                    context.emit(f"took over a stale build claim of {self.slots.last_takeover.agent}")
                    _report_reaped(context, self.slots)
                for step, command, on_line in steps:
                    claim = self.slots.set_step(claim, step) or claim
                    self.note(step)
                    context.log.write(f"==== {step}: {' '.join(command)}\n")
                    began = self.slots.clock()
                    code = context.toolchain.run(command, on_line)
                    context.log.flush()
                    took = self.slots.clock() - began
                    results.append((step, code, took))
                    if code != 0:
                        break
                    if context.history is not None:
                        context.history.record(step_key(options.preset, step, self.size_of(step)), took)
        except SlotTimeout as timeout:
            self.queued = timeout
        return results

    def lane_text(self, measured: int | None = None) -> str:
        """The lane, what decided it and any wait, for a summary line."""
        waited = f", waited {_duration(self.waited)}" if self.waited >= 1 else ""
        steps = "" if measured is None else f", {measured} step{'s' if measured != 1 else ''} out of date"
        return f"{self.lane_name} lane{steps}{waited}"


#: What a run says when a command ended in failure and its output names nothing that failed: a process killed from
#: outside (a timeout, a user, another tool) prints no error.
OUTSIDE = "stopped from outside (no failing {what} found)"


def _report_reaped(context: Context, slots: Slots) -> None:
    """Say how many process trees the takeover just made stopped (a dead run's orphaned ctest or cmake)."""
    if slots.last_reaped and slots.last_takeover is not None:
        count = slots.last_reaped
        trees = "process tree" if count == 1 else "process trees"
        context.emit(f"stopped {count} {trees} {slots.last_takeover.agent}'s run left running")


def _failure_lines(scan: BuildScan) -> list[str]:
    """The indented lines that explain a failed build step: its errors, or its last lines; or, when there is neither
    an error nor a failed step, that the build was stopped from outside."""
    lines = [f"  {error}" for error in scan.errors]
    if not scan.errors and not scan.failed:
        lines.append("  " + OUTSIDE.format(what="step"))
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


def _choose_build_lane(options: Options, context: Context) -> tuple[str, int | None]:
    """The lane of the configure and build phase, and how many steps the build has to do (None: not measured). Fast
    for a configured tree with a few steps out of date, else a main slot.

    To measure, the manifest is first brought up to date for real (`ninja build.ninja`: it re-checks the globs, which
    is cheap, and re-runs CMake only when a CMake file or a glob changed, which the build would do first anyway); then
    a dry run on a copy of it counts the steps that are out of date by themselves (count_dirty_steps)."""
    if options.lane == MAIN_LANE:
        return MAIN_LANE, None
    if options.configure or options.configure_only or not context.configured or context.fast_steps <= 0:
        return MAIN_LANE, None
    code, _ = context.toolchain.capture(["ninja", "build.ninja"], options.folder, REFRESH_SECONDS)
    if code != 0:
        return MAIN_LANE, None
    code, output = context.toolchain.dry_run(options.folder, options.targets)
    steps = count_dirty_steps(output) if code == 0 else None
    return (FAST_LANE if steps is not None and steps < context.fast_steps else MAIN_LANE), steps


def _ctest(options: Options, *more: str) -> list[str]:
    """A ctest command for the run's build folder: the preset's, or `--test-dir` for another folder."""
    base = ["ctest", "--preset", options.preset]
    if options.build_dir is not None:
        base = ["ctest", "--test-dir", str(options.build_dir), "--output-on-failure"]
    return [*base, *more]


def _choose_test_lane(options: Options, context: Context) -> tuple[str, int | None]:
    """The lane of the test phase and how many tests the filter selects (None when unknown). Only a `-R` or `-L` filter
    can be small, by its count and, when any selected test has a recorded time, by those times; a full run, and any run
    with `--lane main`, takes a main slot. The count is still listed with `--lane main`: a filter that selects nothing
    fails before it waits."""
    if not options.filters:
        return MAIN_LANE, None
    code, output = context.toolchain.capture(_ctest(options, "-N", *options.filters), None)
    tests = count_ctest_tests(output) if code == 0 else None
    if options.lane == MAIN_LANE:
        return MAIN_LANE, tests
    small = tests is not None and 0 < tests <= context.fast_tests
    if small and context.test_costs is not None and context.fast_test_seconds > 0:
        costs = context.test_costs.load(options.preset, options.folder / COST_DATA)
        known = [costs[name] for name in parse_test_names(output) if name in costs]
        expected = sum(known)
        if known and expected > context.fast_test_seconds:
            context.emit(
                f"measured: {len(known)} test{'s' if len(known) != 1 else ''} expected to take {_duration(expected)} "
                f"by {'their' if len(known) != 1 else 'its'} recorded time, more than the fast lane's "
                f"{_duration(context.fast_test_seconds)}, so the {MAIN_LANE} lane"
            )
            small = False
    return (FAST_LANE if small else MAIN_LANE), tests


def execute(options: Options, context: Context) -> Result:
    """Run one build folder's configure, build and tests under the folder's lock: the lock is taken first and held to
    the end (a second run in the same folder waits for it, up to `max_wait` minutes, then gives up with exit code 75),
    and released however this ends, an interrupt included. What runs inside is `_execute`."""
    if context.call_started is None:
        context.call_started = context.slots.clock()
    lock = context.checkout_lock
    if lock is None:
        return _execute(options, context, lambda step: None)

    def report(waiting: Waiting) -> None:
        context.emit(
            f"waiting for another run in this build folder ({options.folder}): place {waiting.place} of "
            f"{waiting.total}; held by {_holders_text(waiting.holders, lock.clock())}; {_gives_up(options)}"
        )

    try:
        claim = lock.acquire(
            options.agent,
            options.checkout.name,
            options.preset,
            False,
            context.pid,
            poll=context.poll,
            report=report,
            report_every=context.report_every,
            timeout=options.max_wait * 60,
            abandon=_budget_check(options, context, context.slots.clock, None, 0.0, ""),
        )
    except SlotTimeout as timeout:
        return _gave_up(Result(), "build folder", timeout)
    if lock.last_takeover is not None:
        context.emit(f"took over a stale lock of this build folder from {lock.last_takeover.agent}")
        _report_reaped(context, lock)
    if context.configured_now is not None:
        context.configured = context.configured_now()
    held = [claim]

    def note(step: str) -> None:
        held[0] = lock.set_step(held[0], step) or held[0]

    try:
        return _execute(options, context, note)
    finally:
        lock.release_claim(held[0])


def _execute(options: Options, context: Context, note: Callable[[str], None]) -> Result:
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
    custom = options.build_dir is not None
    if options.configure or options.configure_only or not context.configured:
        configure = ["cmake", "--preset", options.preset, *(["-B", str(options.build_dir)] if custom else [])]
        build_steps.append(("configure", configure, build_line))
    if not options.configure_only:
        folder = [str(options.build_dir)] if custom else ["--preset", options.preset]
        targets = ["--target", *options.targets] if options.targets else []
        build_steps.append(("build", ["cmake", "--build", *folder, *targets, "-j", jobs], build_line))
    note("measuring")
    lane, measured = _choose_build_lane(options, context)
    if measured is not None:
        context.emit(f"measured: {measured} step{'s' if measured != 1 else ''} out of date, so the {lane} lane")
    phase = _Phase(options, context, "build", lane, note)
    ran = phase.run(build_steps)
    if phase.queued is not None:
        return _gave_up(result, "build", phase.queued)
    built = " " + " ".join(options.targets) if options.targets else ""
    for step, code, seconds in ran:
        name = step + (built if step == "build" else "")
        if code == 0:
            where = f" ({phase.lane_text(measured)})" if step == "build" or len(ran) == 1 else ""
            result.summary.append(f"{name}: ok in {_duration(seconds)}{where}")
            _report_cache(result, context, step)
        else:
            result.summary.append(f"{name}: FAILED (exit {code}) in {_duration(seconds)} ({phase.lane_text(measured)})")
            _report_cache(result, context, step)
            result.summary += _failure_lines(scan)
            if step == "configure":
                result.summary.append("build: not run")
            return _failed(result, step, options.test, "step" if not scan.errors and not scan.failed else "")
    if not options.test or options.configure_only:
        result.summary.append("result: ok")
        return result

    # Phase 2: the tests, with a claim of their own.
    lane, selected = _choose_test_lane(options, context)
    if selected == 0:
        result.summary.append(f"test: FAILED: no tests match {options.filter_text}")
        if options.regex is not None:
            result.summary.append(
                "  -R is a CMake regular expression: escape . + ( ) [ ] with a backslash, and quote the argument "
                "(docs/guides/coney-tools.md#build)"
            )
        return _failed(result, "test", False)
    tests = TestScan()

    def test_line(line: str) -> None:
        context.log.write(line + "\n")
        tests.feed(line)

    command = _ctest(options, "-j", jobs, *options.filters)
    phase = _Phase(options, context, "test", lane, note, "filtered" if options.filters else "full")
    ran = phase.run([("test", command, test_line)])
    if phase.queued is not None:
        return _gave_up(result, "test", phase.queued)
    if context.test_costs is not None:
        context.test_costs.record(options.preset, options.folder / COST_DATA)
    _, code, seconds = ran[0]
    counts = f"{tests.passed} passed, {tests.failed_count} failed of {tests.total}"
    # ctest counts a skipped test as passed, so the skips are named: a run in which every disc test skipped must not
    # read as a whole one.
    counts += f", {tests.skipped} skipped" if tests.skipped else ""
    if options.disc is not None:
        result.summary.append(options.disc.line(tests.skipped))
    if code == 0 and not tests.none_found:
        result.summary.append(f"test: ok in {_duration(seconds)} ({phase.lane_text(selected)}): {counts}")
        result.summary.append("result: ok")
        return result
    reason = "no tests were found" if tests.none_found else f"exit {code}"
    result.summary.append(f"test: FAILED ({reason}) in {_duration(seconds)} ({phase.lane_text(selected)}): {counts}")
    names = tests.failed
    if names:
        shown = _cut(", ".join(names[:MAX_FAILED_TESTS]))
        more = f", ... and {len(names) - MAX_FAILED_TESTS} more (see the log)" if len(names) > MAX_FAILED_TESTS else ""
        result.summary.append(f"  failed: {shown}{more}")
    outside = not names and tests.failed_count == 0 and not tests.none_found
    if outside:
        result.summary.append("  " + OUTSIDE.format(what="test"))
    return _failed(result, "test", False, "test" if outside else "", names[:3])


def _report_cache(result: Result, context: Context, step: str) -> None:
    """Add the compiler cache's line (this build's hits and misses) once the build step has run."""
    line = context.cache_report() if context.cache_report is not None and step == "build" else None
    if line:
        result.summary.append(line)


def _failed(
    result: Result, step: str, tests_skipped: bool, outside: str = "", tests: list[str] | None = None
) -> Result:
    """Mark a run failed at `step` (exit 1), noting the tests that never ran. The result line says what failed: the
    first failing tests, or (`outside` names the kind of thing, "step" or "test") that nothing was found failing."""
    if tests_skipped:
        result.summary.append("test: not run")
    detail = f" (stopped from outside: no failing {outside} found)" if outside else ""
    if tests:
        detail = f": {_cut(', '.join(tests))}"
    result.summary.append(f"result: FAILED at {step}{detail}")
    result.ok = False
    result.exit_code = 1
    return result


def _gave_up(result: Result, step: str, timeout: SlotTimeout) -> Result:
    """Mark a run that stopped waiting for a slot (or for its build folder): exit code 75, the place, and the way on."""
    result.summary.append(_queued_line(step, timeout))
    result.summary.append(f"result: QUEUED at {step} (exit {EXIT_QUEUED}); {QUEUED_ADVICE}")
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
