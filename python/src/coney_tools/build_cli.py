# SPDX-License-Identifier: GPL-3.0-or-later
"""The `coney-tools build` command: configure, build and test a checkout under the build slots, `build status` and
`build release` (the slots are in build_claims.py, the run in build_run.py; docs/guides/coney-tools.md#build)."""

from __future__ import annotations

import _thread
import argparse
import contextlib
import json
import os
import re
import subprocess
import sys
import threading
import time
from collections.abc import Callable, Iterator
from dataclasses import asdict
from datetime import datetime
from pathlib import Path
from typing import Any

from coney_tools import build_claims, build_run, compiler_cache, pcsx2_proc, process_lifetime
from coney_tools.build_claims import FAST_LANE, MAIN_LANE, BuildClaimError, Slots
from coney_tools.build_history import FILE_NAME, History
from coney_tools.config import INT_KEYS, ConfigError, find_repo_root, load_config, local_config_file, main_checkout
from coney_tools.pcsx2_claims import AGENT_NAME

PRESETS = ("dev", "release", "ci", "asan")
#: How many logs of one worktree are kept; older ones are removed when a run starts.
KEEP_LOGS = 20
#: Seconds a run whose caller is gone gets to stop on its own before the process ends at once (its claims then go
#: stale and are taken over; on Windows the job ends the children).
ORPHAN_GRACE_SECONDS = 15.0
#: The exit code of a run that ended because its caller was gone.
EXIT_ORPHANED = 130
#: The most minutes a build run may wait for a slot, and take in all, when its caller waits for it: an agent idle
#: for an hour loses its prompt cache and pays to write it again (docs/guides/agents.md#build-waits). Longer waits
#: run with --detach.
MAX_WAIT_CAP = 40.0
BUDGET_CAP = 45.0
#: The default wait of a --detach run, which nobody sits through.
DETACHED_MAX_WAIT = 240.0
#: The file in the worktree's scratch folder that a --detach run records itself in, and its console output.
RESULT_FILE = "build-result.json"
DETACHED_OUTPUT = "build-detached.out"
#: Windows process creation flags of the detached child, spelt out so they exist on every platform.
CREATE_NEW_PROCESS_GROUP = 0x00000200
CREATE_NO_WINDOW = 0x08000000
CREATE_BREAKAWAY_FROM_JOB = 0x01000000
#: A variable name for `--env`.
_ENV_NAME = re.compile(r"^[A-Za-z_][A-Za-z0-9_]*$")


def add_parser(groups: Any) -> None:
    """Register `coney-tools build [flags]`, `build status` and `build release`."""
    group = groups.add_parser(
        "build",
        help="configure, build and test under the build slots (at most two at once); status, release",
    )
    group.add_argument("--agent", help="your id: letters, digits, '.', '_' and '-'")
    group.add_argument("--preset", choices=PRESETS, default="dev", help="the CMake preset (default dev)")
    group.add_argument("--dir", type=Path, help="the checkout to build (default: the current one)")
    group.add_argument("--configure", action="store_true", help="configure even when the build folder has a cache")
    group.add_argument(
        "--configure-only", action="store_true", help="configure and stop (no build): the compile database for check"
    )
    group.add_argument("--test", action="store_true", help="run ctest after a successful build")
    group.add_argument(
        "--target",
        action="append",
        default=[],
        metavar="NAME",
        dest="targets",
        help="build only this target and what it needs (repeatable), e.g. `coney` for a play build without the "
        "tests; not with --test",
    )
    group.add_argument(
        "-R",
        "--regex",
        help="run only the tests whose names match (ctest -R, a CMake regular expression: escape . + ( ) with a "
        "backslash); a few tests are quick",
    )
    group.add_argument(
        "-L",
        "--label",
        help="run only the tests with a label that matches (ctest -L); `disc` is every test that needs the game's disc",
    )
    group.add_argument(
        "--build-dir",
        type=Path,
        help="build in this folder instead of build/<preset> (relative to the checkout), with the preset's settings; "
        "for a second configuration, such as the Clang one clang-tidy reads",
    )
    group.add_argument(
        "--env",
        action="append",
        default=[],
        metavar="NAME=VALUE",
        help="set an environment variable for the run, e.g. CC=clang-19 (repeatable); CMake reads CC and CXX at the "
        "first configure of a folder",
    )
    group.add_argument("--priority", action="store_true", help="go ahead of the normal queue (a build someone awaits)")
    group.add_argument(
        "--max-wait",
        "--slots-wait-timeout",
        type=float,
        metavar="MIN",
        help=f"minutes to wait for a slot, per phase, before exiting with code 75 (default and most "
        f"{MAX_WAIT_CAP:.0f}; with --detach default {DETACHED_MAX_WAIT:.0f}, no limit)",
    )
    group.add_argument(
        "--budget",
        type=float,
        metavar="MIN",
        help=f"minutes the whole call may take, waits and steps together: stop waiting (exit 75) once the wait plus "
        f"what the step usually takes would pass it (default and most {BUDGET_CAP:.0f}, inside an hour's prompt "
        "cache; with --detach default 0: no budget)",
    )
    group.add_argument(
        "--detach",
        action="store_true",
        help="run in the background, apart from this shell, and return at once: the result goes to "
        "build-result.json in the worktree's scratch folder (`build result` shows it); for a wait or a run that "
        "would outlast your prompt cache",
    )
    # Internal: how --detach starts its child (where its log and result go).
    group.add_argument("--log-file", type=Path, help=argparse.SUPPRESS)
    group.add_argument("--result-file", type=Path, help=argparse.SUPPRESS)
    group.add_argument("-j", "--jobs", type=int, help="parallel jobs (default: the cores divided by the slots)")
    commands = group.add_subparsers(dest="build_command")
    status = commands.add_parser("status", help="who holds the slots, who waits, and stale entries")
    status.add_argument("--json", action="store_true", help="print JSON instead of text")
    result = commands.add_parser("result", help="the outcome of the worktree's last --detach run")
    result.add_argument("--json", action="store_true", help="print the record as JSON")
    release = commands.add_parser("release", help="manual cleanup: free an agent's slots and drop its queue places")
    release.add_argument("--agent", dest="release_agent", required=True, help="the agent whose entries to remove")
    release.add_argument("--force", action="store_true", help="also another agent's, or a build that still runs")
    release.add_argument("--slot", type=int, help="one slot by number (with --lane for the fast lane)")
    release.add_argument("--lane", choices=(MAIN_LANE, FAST_LANE), help="one lane only (default: both)")


def run(args: argparse.Namespace) -> int:
    """Dispatch `build`, `build status` or `build release`."""
    if args.build_command == "status":
        return run_status(args.dir, args.json)
    if args.build_command == "result":
        return run_result(args.dir, args.json)
    if args.build_command == "release":
        return run_release(args.dir, args.release_agent, args.force, args.slot, args.lane)
    return run_build(args)


def _root(directory: Path | None) -> Path:
    """The checkout a command is about."""
    return find_repo_root(directory or Path.cwd())


def _numbers(root: Path) -> dict[str, int]:
    """The number settings of coney.local.toml (defaults where unset)."""
    return load_config(root).numbers if local_config_file(root) else dict(INT_KEYS)


def scratch_root(root: Path) -> Path:
    """The scratch directory: `scratch_dir`, else the shared scratch beside the main checkout."""
    configured = load_config(root).paths.get("scratch_dir") if local_config_file(root) else None
    return configured or (main_checkout(root) / ".." / ".." / "scratch").resolve()


def _log_folder(root: Path) -> Path:
    """The worktree's folder in the scratch directory (`scratch_root`, then the worktree's name)."""
    return scratch_root(root) / root.name


def _new_log(root: Path, preset: str, folder: str | None, agent: str) -> Path:
    """A log file of this run alone: `build-<preset>[-<folder>]-<agent>-<date>-<time>.log` (with the process id when
    that name is taken), so two runs never write one file; the oldest logs beyond KEEP_LOGS are removed. The file is
    created here, empty, so the name is ours."""
    directory = _log_folder(root)
    directory.mkdir(parents=True, exist_ok=True)
    stamp = datetime.now().strftime("%Y%m%d-%H%M%S")
    stem = f"build-{preset}{'-' + folder if folder else ''}-{agent}-{stamp}"
    for name in (f"{stem}.log", f"{stem}-{os.getpid()}.log"):
        try:
            (directory / name).open("x", encoding="utf-8").close()
            break
        except FileExistsError:
            continue
    else:
        raise ConfigError(f"{directory / (stem + '.log')} already exists")
    logs = sorted(directory.glob("build-*.log"), key=lambda path: (path.stat().st_mtime, path.name))
    for old in logs[:-KEEP_LOGS]:
        old.unlink(missing_ok=True)  # one a running build still holds is refused on Windows: it is not the oldest
    # A run's ccache stats log goes with it (cache_line removes it; one a killed run left behind goes with its log).
    for stats in directory.glob("build-*.ccache-stats"):
        if not stats.with_suffix(".log").exists():
            stats.unlink(missing_ok=True)
    return directory / name


def _environment(base: dict[str, str], settings: list[str]) -> dict[str, str]:
    """`base` with the `NAME=VALUE` settings of `--env` applied; a name replaces the same name in any case. Raises
    ConfigError for a setting that is not NAME=VALUE."""
    env = dict(base)
    for setting in settings:
        name, equals, value = setting.partition("=")
        if not equals or not _ENV_NAME.match(name):
            raise ConfigError(f"--env {setting!r}: use NAME=VALUE (letters, digits and '_' in the name)")
        for key in [k for k in env if k.upper() == name.upper()]:
            del env[key]
        env[name] = value
    return env


def limits(args: argparse.Namespace) -> tuple[float, float]:
    """The run's (max wait, budget) in minutes: the flags, else the caps, or for a --detach run DETACHED_MAX_WAIT and
    no budget. Raises ConfigError for a run its caller waits on that asks for more than the caps."""
    detached = args.detach or args.result_file is not None
    max_wait = args.max_wait if args.max_wait is not None else (DETACHED_MAX_WAIT if detached else MAX_WAIT_CAP)
    budget = args.budget if args.budget is not None else (0.0 if detached else BUDGET_CAP)
    if not detached and (max_wait > MAX_WAIT_CAP or budget <= 0 or budget > BUDGET_CAP):
        raise ConfigError(
            f"--max-wait {max_wait:g} --budget {budget:g}: a call you wait on may wait at most {MAX_WAIT_CAP:.0f} min "
            f"and take at most {BUDGET_CAP:.0f} (a budget of 0, no limit, is refused too), or you outlast your prompt "
            "cache. For a longer run use --detach, write STATUS.md and stop: `coney-tools build result` shows the "
            "outcome to whoever continues (docs/guides/agents.md#build-waits)"
        )
    return max_wait, budget


def run_build(args: argparse.Namespace) -> int:
    """`build`: precheck, then configure, build and test under the slots; print the log path first and a short
    summary last. Exit 0 on success, 1 on a failed step, 75 when it gave up waiting for a slot. With --detach, start
    the same run in the background and return at once; the child (--result-file) records its outcome there."""
    if args.result_file is not None:
        return _detached_child(args)
    max_wait, budget = limits(args)
    root, env = _precheck(args)
    if args.detach:
        return _start_detached(args, root, max_wait, budget)
    log_path = _new_log(root, args.preset, args.build_dir.name if args.build_dir else None, args.agent)
    return _build(args, root, env, log_path, (max_wait, budget), _tie_to_caller).exit_code


def _precheck(args: argparse.Namespace) -> tuple[Path, dict[str, str]]:
    """Refuse a run that cannot work before anything waits: the agent id, the preset, the settings and the tools.
    Returns the checkout and the environment of the run."""
    if not args.agent:
        raise ConfigError("pass --agent <your id> (see docs/guides/coney-tools.md, 'build')")
    if args.targets and args.test:
        raise ConfigError(
            "--target with --test: the tests need coney_tests and coney rebuilt, which --target would leave stale; "
            "drop --target to build everything, or --test to build only the targets"
        )
    if args.targets and args.configure_only:
        raise ConfigError("--target with --configure-only: a configure builds no target")
    if not AGENT_NAME.match(args.agent):
        raise ConfigError(f"agent id {args.agent!r}: use letters, digits, '.', '_' and '-' (at most 64)")
    root = _root(args.dir)
    build_run.check_preset(root, args.preset)
    _environment({}, args.env)  # a malformed --env is refused now; it is applied after the Visual Studio environment
    env = _environment(build_run.load_msvc_environment(os.environ, build_run.run_text), args.env)
    build_run.find_programs(env)
    return root, env


def _build(
    args: argparse.Namespace,
    root: Path,
    env: dict[str, str],
    log_path: Path,
    limits_minutes: tuple[float, float],
    lifetime: Callable[[build_run.ProcessToolchain], None],
) -> build_run.Result:
    """Configure, build and test under the slots, writing the full output to `log_path` and the summary to the
    console; `limits_minutes` is (max wait, budget) and `lifetime` ties the toolchain's processes to this run (and the
    run to its caller, when it has one)."""
    build_dir = (root / args.build_dir).resolve() if args.build_dir else None
    numbers = _numbers(root)
    folder = build_dir if build_dir is not None else root / "build" / args.preset
    slots = build_claims.default_slots(MAIN_LANE, root)
    cache = compiler_cache.find(root)
    compiler_cache.write_config(cache)
    stats_log = log_path.with_suffix(".ccache-stats")
    env = {**env, **compiler_cache.environment(cache, root, stats_log, env)}
    options = build_run.Options(
        agent=args.agent,
        preset=args.preset,
        checkout=root,
        configure=args.configure,
        configure_only=args.configure_only,
        test=args.test,
        regex=args.regex,
        label=args.label,
        build_dir=build_dir,
        priority=args.priority,
        max_wait=limits_minutes[0],
        budget=limits_minutes[1],
        jobs=args.jobs,
        targets=tuple(args.targets),
    )
    print(f"log: {log_path}", flush=True)
    toolchain = build_run.ProcessToolchain(root, env)
    lifetime(toolchain)
    with log_path.open("a", encoding="utf-8", buffering=1) as log, _removed_after(stats_log):
        context = build_run.Context(
            slots=slots,
            fast=build_claims.default_slots(FAST_LANE, root),
            toolchain=toolchain,
            emit=lambda line: print(line, flush=True),
            log=log,
            pid=os.getpid(),
            cpus=os.cpu_count(),
            fast_steps=numbers["build_fast_lane_steps"],
            fast_tests=numbers["build_fast_lane_tests"],
            configured=(folder / "CMakeCache.txt").is_file(),
            checkout_lock=build_claims.folder_lock(folder, root),
            configured_now=lambda: (folder / "CMakeCache.txt").is_file(),
            history=History(slots.claims_dir / FILE_NAME),
            cached=cache.on,
            cache_report=lambda: cache_line(cache, folder, stats_log, toolchain),
        )
        result = build_run.execute(options, context)
    for line in result.summary:
        print(line)
    return result


@contextlib.contextmanager
def _removed_after(path: Path) -> Iterator[None]:
    """Remove `path` when the block ends, however it ends: a run that only configures, or whose configure fails, never
    reads its ccache stats log, and it must not be left beside the logs."""
    try:
        yield
    finally:
        path.unlink(missing_ok=True)


def cache_line(
    cache: compiler_cache.CompilerCache, folder: Path, stats_log: Path, toolchain: build_run.Toolchain
) -> str | None:
    """The summary's line on the compiler cache after a build: this run's hits and misses (read from its own stats log,
    which is then removed), why the cache is off, or that the folder was configured without it (CMake takes the
    launcher only when it first configures a folder). None when the build compiled nothing."""
    if not cache.on:
        reason = cache.missing or "ccache_size_gb is 0"
        return f"compiler cache: off ({reason})"
    if stats_log.is_file() and cache.program is not None:
        code, output = toolchain.capture([str(cache.program), "--print-log-stats"], None)
        stats = compiler_cache.parse_stats(output) if code == 0 else None
        stats_log.unlink(missing_ok=True)
        if stats is not None:
            return stats.line()
        needs = compiler_cache.MIN_VERSION
        return f"compiler cache: on, hits unavailable (`ccache --print-log-stats` failed; it needs ccache {needs}+)"
    try:
        configured = (folder / "CMakeCache.txt").read_text(encoding="utf-8", errors="replace")
    except OSError:
        return None
    if "CMAKE_CXX_COMPILER_LAUNCHER" not in configured:
        return (
            f"compiler cache: not used ({folder.name} was configured before the cache was on; delete the folder to "
            "build with it)"
        )
    return None


def _child_command(
    args: argparse.Namespace, root: Path, limits_minutes: tuple[float, float], log: Path, result: Path
) -> list[str]:
    """The command line of a --detach run's child: this run's flags, its limits spelt out, and where its log and
    result go."""
    command = [sys.executable, "-m", "coney_tools.cli", "build", "--agent", args.agent, "--preset", args.preset]
    command += ["--dir", str(root), "--max-wait", str(limits_minutes[0]), "--budget", str(limits_minutes[1])]
    switches = (
        ("--configure", args.configure),
        ("--configure-only", args.configure_only),
        ("--test", args.test),
        ("--priority", args.priority),
    )
    command += [flag for flag, on in switches if on]
    for flag, value in (("-R", args.regex), ("-L", args.label), ("--build-dir", args.build_dir), ("-j", args.jobs)):
        command += [flag, str(value)] if value is not None else []
    for target in args.targets:
        command += ["--target", target]
    for setting in args.env:
        command += ["--env", setting]
    return [*command, "--log-file", str(log), "--result-file", str(result)]


def spawn_detached(command: list[str], cwd: Path, output: Path) -> int:
    """Start `command` apart from this process and its shell (no console, a process group or session of its own, out
    of the caller's job where Windows allows it), its console output in `output`; returns its pid."""
    with output.open("w", encoding="utf-8") as sink:
        common: dict[str, Any] = {"cwd": cwd, "stdin": subprocess.DEVNULL, "stdout": sink, "stderr": subprocess.STDOUT}
        if sys.platform != "win32":
            return subprocess.Popen(command, start_new_session=True, **common).pid
        # CREATE_NO_WINDOW, never DETACHED_PROCESS: the venv python.exe is a launcher whose child interpreter finds no
        # console to inherit, so Windows gives it a new one that Windows Terminal shows as an empty window.
        flags = CREATE_NO_WINDOW | CREATE_NEW_PROCESS_GROUP
        try:
            return subprocess.Popen(command, creationflags=flags | CREATE_BREAKAWAY_FROM_JOB, **common).pid
        except OSError:  # the caller's job forbids leaving it: the child then ends with that job
            return subprocess.Popen(command, creationflags=flags, **common).pid


def _start_detached(args: argparse.Namespace, root: Path, max_wait: float, budget: float) -> int:
    """--detach: record the run as started, start its child and return at once with where to look later. Refuses a
    second detached run in a worktree whose last one still runs (they would share one result file)."""
    result = _log_folder(root) / RESULT_FILE
    previous = _read_record(result)
    if previous is not None and previous.get("state") == "running" and _alive(previous.get("pid")):
        raise ConfigError(
            f"a detached build of {previous.get('agent')} is still running (pid {previous.get('pid')}): "
            "`coney-tools build result` shows how it ends"
        )
    log = _new_log(root, args.preset, args.build_dir.name if args.build_dir else None, args.agent)
    _write_record(result, _record(args, None, log))
    command = _child_command(args, root, (max_wait, budget), log, result)
    pid = spawn_detached(command, root, result.parent / DETACHED_OUTPUT)
    print(f"log: {log}")
    print(f"detached: pid {pid}, result in {result}")
    print(
        "write STATUS.md and stop, or do other work: `coney-tools build result` shows the outcome "
        "(exit 75 while it runs)"
    )
    return 0


def _detached_child(args: argparse.Namespace) -> int:
    """The child of a --detach run: the run itself, its outcome recorded in --result-file however it ends (a refused
    precheck, a failure, an interrupt), so a successor can read it."""
    record = _record(args, os.getpid(), args.log_file)
    _write_record(args.result_file, record)
    code, summary = 1, ["result: ended unexpectedly"]
    try:
        limits_minutes = limits(args)
        root, env = _precheck(args)
        args.log_file.parent.mkdir(parents=True, exist_ok=True)
        result = _build(args, root, env, args.log_file, limits_minutes, lambda toolchain: _detached_lifetime())
        code, summary = result.exit_code, result.summary
        return code
    except ConfigError as error:
        code, summary = 2, [f"coney-tools: {error}"]
        raise
    except KeyboardInterrupt:
        code, summary = 130, ["result: interrupted"]
        raise
    except SystemExit as stop:
        code, summary = (stop.code if isinstance(stop.code, int) else 1), ["result: stopped by a signal"]
        raise
    finally:
        ended = {"state": "done", "exit_code": code, "summary": summary, "ended": time.time()}
        _write_record(args.result_file, {**record, **ended})


def _detached_lifetime() -> None:
    """A detached run's tie: its children end with it, but nothing watches the shell that started it, which is
    expected to be gone."""
    process_lifetime.bind_children()
    process_lifetime.stop_on_signals()


def _record(args: argparse.Namespace, pid: int | None, log: Path) -> dict[str, Any]:
    """A running run's record: who runs what, its process (None until the child starts), its log, since when."""
    return {
        "state": "running",
        "agent": args.agent,
        "preset": args.preset,
        "pid": pid,
        "log": str(log),
        "started": time.time(),
    }


def _write_record(path: Path, record: dict[str, Any]) -> None:
    """Write a result record whole (a reader never sees half a file), retrying briefly while a reader on Windows
    holds the old one open."""
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(f"{path.name}.{os.getpid()}.tmp")
    temporary.write_text(json.dumps(record, indent=2) + "\n", encoding="utf-8")
    for attempt in range(20):
        try:
            temporary.replace(path)
            return
        except PermissionError:
            if attempt == 19:
                raise
            time.sleep(0.05)


def _read_record(path: Path) -> dict[str, Any] | None:
    """A result record, or None when there is none or it cannot be read."""
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, ValueError):
        return None
    return data if isinstance(data, dict) else None


def _alive(pid: object) -> bool:
    """Whether a recorded pid still runs (None: the child has not started yet, which counts as running)."""
    return pid is None or (isinstance(pid, int) and pcsx2_proc.pid_alive(pid))


def run_result(directory: Path | None, as_json: bool) -> int:
    """`build result`: the outcome of the worktree's last --detach run. Exit: the run's own code once it has ended,
    75 while it runs, 1 when there is none or it ended without a result."""
    record = _read_record(_log_folder(_root(directory)) / RESULT_FILE)
    if record is None:
        print("no detached build recorded for this worktree")
        return 1
    if record.get("state") == "running":
        if _alive(record.get("pid")):
            code = build_run.EXIT_QUEUED
        else:
            code, record = 1, {**record, "state": "lost"}
    else:
        code = int(record.get("exit_code", 1))
    if as_json:
        print(json.dumps(record, indent=2))
        return code
    started = _minutes(float(record.get("started", 0)), time.time())
    print(f"detached build of {record.get('agent')} ({record.get('preset')}), started {started} ago")
    print(f"log: {record.get('log')}")
    if record["state"] == "running":
        print(f"still running (pid {record.get('pid')}): look again later")
    elif record["state"] == "lost":
        print(f"ended without a result (pid {record.get('pid')} is gone): see the log and {DETACHED_OUTPUT}")
    else:
        for line in record.get("summary", []):
            print(line)
        print(f"exit {code}")
    return code


def _tie_to_caller(toolchain: build_run.ProcessToolchain) -> None:
    """Make sure nothing this run starts outlives it, nor the run its caller (process_lifetime.py): the children end
    when this process does (the Windows job; a process group stopped on SIGTERM and SIGHUP elsewhere), and the run
    stops when the shell that started it is gone."""
    process_lifetime.bind_children()
    process_lifetime.stop_on_signals()
    process_lifetime.watch_parent(lambda gone: _caller_gone(toolchain, gone))


def _exit_later() -> None:
    """End the process after ORPHAN_GRACE_SECONDS, whatever it is doing (a wait for a slot may not see the interrupt
    at once)."""
    timer = threading.Timer(ORPHAN_GRACE_SECONDS, os._exit, (EXIT_ORPHANED,))
    timer.daemon = True
    timer.start()


def _caller_gone(
    toolchain: build_run.ProcessToolchain,
    gone: process_lifetime.Process,
    interrupt: Callable[[], None] = _thread.interrupt_main,
    exit_later: Callable[[], None] = _exit_later,
) -> None:
    """The shell (or launcher) that started this run has ended: stop the running command and anything after it, and
    leave as an interrupt does (releasing the claims), or by force a little later."""
    try:
        print(f"{gone.name} (pid {gone.pid}) that started this build has ended: stopping the build", file=sys.stderr)
        sys.stderr.flush()
    except (OSError, ValueError):
        pass  # the console may be gone with the shell
    toolchain.stop()
    interrupt()
    exit_later()


def _minutes(since: float, now: float) -> str:
    """Whole minutes between two epoch times, as text."""
    return f"{max(0.0, now - since) / 60:.0f} min"


def _status_json(snapshots: dict[str, Any]) -> dict[str, Any]:
    """The JSON of `build status --json`: per lane, its slots with their claims and its queue in order."""
    return {
        lane: {
            "slots": [
                {"slot": r.number, "stale": r.stale, "claim": asdict(r.claim) if r.claim else None} for r in snap.slots
            ],
            "queue": [{"position": r.position, "stale": r.stale, **asdict(r.ticket)} for r in snap.queue],
        }
        for lane, snap in snapshots.items()
    }


def status_json(root: Path) -> dict[str, Any]:
    """What `build status --json` prints for the checkout at `root` (also what `coord status` shows of the slots)."""
    return _status_json({lane: build_claims.default_slots(lane, root).status() for lane in (MAIN_LANE, FAST_LANE)})


def result_record(root: Path) -> dict[str, Any] | None:
    """The worktree's last --detach run as `build result --json` gives it (state `running`, `done`, or `lost` for a
    run whose process is gone without a result), or None when there is none."""
    record = _read_record(_log_folder(root) / RESULT_FILE)
    if record is not None and record.get("state") == "running" and not _alive(record.get("pid")):
        record = {**record, "state": "lost"}
    return record


def run_status(directory: Path | None, as_json: bool) -> int:
    """`build status`: the slots of both lanes with their holders, the queues in order, and stale entries."""
    root = _root(directory)
    lanes = {lane: build_claims.default_slots(lane, root) for lane in (MAIN_LANE, FAST_LANE)}
    snapshots = {lane: slots.status() for lane, slots in lanes.items()}
    now = lanes[MAIN_LANE].clock()
    if as_json:
        print(json.dumps(_status_json(snapshots)))
        return 0
    for lane, snap in snapshots.items():
        print(f"{lane} lane ({lanes[lane].slots} slot{'s' if lanes[lane].slots != 1 else ''}):")
        for row in snap.slots:
            claim = row.claim
            if claim is None:
                print(f"  slot {row.number}: free")
                continue
            state = "STALE " if row.stale else ""
            print(
                f"  slot {row.number}: {state}{claim.agent}, {claim.worktree}, {claim.preset}, {claim.step}, "
                f"{_minutes(_epoch(claim.started), now)} (pid {claim.pid})"
            )
        waiting = [r for r in snap.queue if not r.stale]
        print(f"  queue: {'empty' if not waiting else len(waiting)}")
        for place in waiting:
            t = place.ticket
            priority = ", priority" if t.priority else ""
            print(
                f"    {place.position}. {t.agent}, {t.worktree}, {t.preset}{priority}, waiting {_minutes(t.time, now)}"
            )
        for gone in (r for r in snap.queue if r.stale):
            print(f"  stale ticket: {gone.ticket.agent} (pid {gone.ticket.pid} is gone); the next waiter drops it")
    return 0


def _epoch(iso: str) -> float:
    """An ISO time as epoch seconds (the start of a claim); now for a claim with no date."""
    try:
        return datetime.fromisoformat(iso).timestamp()
    except ValueError:
        return time.time()


def run_release(directory: Path | None, agent: str, force: bool, slot: int | None, lane: str | None) -> int:
    """`build release`: free an agent's slots and drop its queue places (manual cleanup after a crash)."""
    root = _root(directory)
    chosen: list[Slots] = [
        build_claims.default_slots(name, root) for name in ([lane] if lane else [MAIN_LANE, FAST_LANE])
    ]
    if slot is not None:
        chosen = chosen[:1]  # a slot number belongs to one lane: --lane, else the main slots
    released = 0
    problems: list[str] = []
    for slots in chosen:
        try:
            claims, tickets = slots.release(agent, slot, force)
        except BuildClaimError as error:
            problems.append(str(error))
            continue
        for claim in claims:
            print(f"released {slots.lane} slot {claim.slot} (held by {claim.agent}, {claim.preset}, {claim.step})")
        for ticket in tickets:
            print(f"left the {slots.lane} queue: {ticket.agent} ({ticket.preset})")
        released += len(claims) + len(tickets)
    if released == 0:
        raise BuildClaimError("; ".join(problems) or f"nothing to release for {agent}")
    for problem in problems:
        if "nothing to release" not in problem:
            print(f"not released: {problem}", file=sys.stderr)
    return 0
