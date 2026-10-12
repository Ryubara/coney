# SPDX-License-Identifier: GPL-3.0-or-later
"""The `coney-tools build` command: configure, build and test a checkout under the build slots, `build status` and
`build release` (the slots are in build_claims.py, the run in build_run.py; docs/guides/coney-tools.md#build)."""

from __future__ import annotations

import argparse
import json
import os
import re
import sys
import time
from dataclasses import asdict
from datetime import datetime
from pathlib import Path
from typing import Any

from coney_tools import build_claims, build_run
from coney_tools.build_claims import FAST_LANE, MAIN_LANE, BuildClaimError, Slots
from coney_tools.config import INT_KEYS, ConfigError, find_repo_root, load_config, local_config_file, main_checkout
from coney_tools.pcsx2_claims import AGENT_NAME

PRESETS = ("dev", "release", "ci", "asan")
#: How many logs of one worktree are kept; older ones are removed when a run starts.
KEEP_LOGS = 20
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
        default=45.0,
        metavar="MIN",
        help="minutes to wait for a slot, per phase, before exiting with code 75 (default 45)",
    )
    group.add_argument("-j", "--jobs", type=int, help="parallel jobs (default: the cores divided by the slots)")
    commands = group.add_subparsers(dest="build_command")
    status = commands.add_parser("status", help="who holds the slots, who waits, and stale entries")
    status.add_argument("--json", action="store_true", help="print JSON instead of text")
    release = commands.add_parser("release", help="manual cleanup: free an agent's slots and drop its queue places")
    release.add_argument("--agent", dest="release_agent", required=True, help="the agent whose entries to remove")
    release.add_argument("--force", action="store_true", help="also another agent's, or a build that still runs")
    release.add_argument("--slot", type=int, help="one slot by number (with --lane for the fast lane)")
    release.add_argument("--lane", choices=(MAIN_LANE, FAST_LANE), help="one lane only (default: both)")


def run(args: argparse.Namespace) -> int:
    """Dispatch `build`, `build status` or `build release`."""
    if args.build_command == "status":
        return run_status(args.dir, args.json)
    if args.build_command == "release":
        return run_release(args.dir, args.release_agent, args.force, args.slot, args.lane)
    return run_build(args)


def _root(directory: Path | None) -> Path:
    """The checkout a command is about."""
    return find_repo_root(directory or Path.cwd())


def _numbers(root: Path) -> dict[str, int]:
    """The number settings of coney.local.toml (defaults where unset)."""
    return load_config(root).numbers if local_config_file(root) else dict(INT_KEYS)


def _log_folder(root: Path) -> Path:
    """The worktree's folder in the scratch directory (`scratch_dir`, else the shared scratch beside the main checkout,
    then the worktree's name)."""
    configured = load_config(root).paths.get("scratch_dir") if local_config_file(root) else None
    scratch = configured or (main_checkout(root) / ".." / ".." / "scratch").resolve()
    return scratch / root.name


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


def run_build(args: argparse.Namespace) -> int:
    """`build`: precheck, then configure, build and test under the slots; print the log path first and a short
    summary last. Exit 0 on success, 1 on a failed step, 75 when it gave up waiting for a slot."""
    if not args.agent:
        raise ConfigError("pass --agent <your id> (see docs/guides/coney-tools.md, 'build')")
    if not AGENT_NAME.match(args.agent):
        raise ConfigError(f"agent id {args.agent!r}: use letters, digits, '.', '_' and '-' (at most 64)")
    root = _root(args.dir)
    # The precheck: the preset exists, the settings are well formed and the tools are found, before anything waits.
    build_run.check_preset(root, args.preset)
    _environment({}, args.env)  # a malformed --env is refused now; it is applied after the Visual Studio environment
    build_dir = (root / args.build_dir).resolve() if args.build_dir else None
    env = _environment(build_run.load_msvc_environment(os.environ, build_run.run_text), args.env)
    build_run.find_programs(env)
    numbers = _numbers(root)
    folder = build_dir if build_dir is not None else root / "build" / args.preset
    log_path = _new_log(root, args.preset, build_dir.name if build_dir else None, args.agent)
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
        max_wait=args.max_wait,
        jobs=args.jobs,
    )
    print(f"log: {log_path}", flush=True)
    with log_path.open("a", encoding="utf-8", buffering=1) as log:
        context = build_run.Context(
            slots=build_claims.default_slots(MAIN_LANE, root),
            fast=build_claims.default_slots(FAST_LANE, root),
            toolchain=build_run.ProcessToolchain(root, env),
            emit=lambda line: print(line, flush=True),
            log=log,
            pid=os.getpid(),
            cpus=os.cpu_count(),
            fast_steps=numbers["build_fast_lane_steps"],
            fast_tests=numbers["build_fast_lane_tests"],
            configured=(folder / "CMakeCache.txt").is_file(),
            checkout_lock=build_claims.folder_lock(folder, root),
            configured_now=lambda: (folder / "CMakeCache.txt").is_file(),
        )
        result = build_run.execute(options, context)
    for line in result.summary:
        print(line)
    return result.exit_code


def _minutes(since: float, now: float) -> str:
    """Whole minutes between two epoch times, as text."""
    return f"{max(0.0, now - since) / 60:.0f} min"


def run_status(directory: Path | None, as_json: bool) -> int:
    """`build status`: the slots of both lanes with their holders, the queues in order, and stale entries."""
    root = _root(directory)
    lanes = {lane: build_claims.default_slots(lane, root) for lane in (MAIN_LANE, FAST_LANE)}
    snapshots = {lane: slots.status() for lane, slots in lanes.items()}
    now = lanes[MAIN_LANE].clock()
    if as_json:
        print(
            json.dumps(
                {
                    lane: {
                        "slots": [
                            {"slot": r.number, "stale": r.stale, "claim": asdict(r.claim) if r.claim else None}
                            for r in snap.slots
                        ],
                        "queue": [{"position": r.position, "stale": r.stale, **asdict(r.ticket)} for r in snap.queue],
                    }
                    for lane, snap in snapshots.items()
                }
            )
        )
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
