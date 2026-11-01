# SPDX-License-Identifier: GPL-3.0-or-later
"""`coney-tools task check`: run everything a change must pass here, then stamp it
(docs/guides/coney-tools-repo.md#task-check).

What a change needs is decided by the paths it touches (`proof_profiles`, the `proof` section of
`.github/conventions.yml`): a documentation change runs the docs checks, a tooling change adds ruff, mypy, pytest and
actionlint, and anything else also builds with the MSVC `ci` preset (/WX) and runs the C++ tests the change needs
with the disc: the test files its C++ files reach, or the whole suite (`proof.ctest`, ctest_scope.py; `coney-tools
build` leaves a build record the stamp covers, and the stamp's `detail.ctest_scope` says which tests ran). The result
is one `checked` stamp, `pass` or `fail`, bound to the change (`covers.diff_sha256`, `pr_diff`), so a clean rebase
keeps it and any edit makes it stale. A fault
of the machine (a program missing, no disc, no build slot, a worktree that changed under the run) is not a verdict
about the change and writes no stamp. The profiles and the path classes are read from the base's
`.github/conventions.yml`, not the branch's, so a branch cannot shrink the profile it is checked against (`pr verify`
must derive them from the base too). Only the `proof` section and the scope lists of `paths` are read, ignoring keys
the branch's parser does not know; the stamp's `detail.rules` says `base`, or `branch` when the base has no `proof`
section yet. A base whose rules cannot be read stops the run (exit 2).

Exit codes: 0 every check passed and the stamp is written; 1 a check failed (a `fail` stamp is written); 2 it cannot
run or cannot stamp (nothing is written); 75 the build or the pytest run gave up waiting for a slot (nothing is
written; run it again). Everything that touches the machine is in `Env`, so the order, the stamp and the refusals are
tested with fakes.

The run is live: each check prints a `start` line when it begins and its result when it ends, with the time of day, and
every line is flushed at once (a run redirected to a file shows it grow). The full suites (pytest here, the engine's
ctest run inside the build) wait for a check slot first (`check_slots`, check_slots.py), and a long check prints a
progress line every minute with its output going to `check-logs/` in the task's scratch folder
(live_progress.py; docs/guides/coney-tools-repo.md#task-check). The same run writes where it is to
`check-progress.json` in that folder, from the same numbers (check_progress.py; docs/guides/coney-tools-repo.md
#check-progress): the check and its place in the list, its kind and time, how far it is, its place in a slot queue, and
at the end whether it passed, failed or was abandoned.
"""

from __future__ import annotations

import argparse
import os
import sys
from collections.abc import Callable, Mapping
from contextlib import AbstractContextManager, nullcontext
from dataclasses import asdict, dataclass, field, replace
from datetime import UTC, datetime
from pathlib import Path
from typing import Any

from coney_tools import (
    build_claims,
    build_cli,
    build_record,
    build_run,
    check_progress,
    check_slots,
    ctest_scope,
    detached_runs,
    live_progress,
    pr_diff,
    proof_profiles,
    stamps,
    task_overlap,
    unit,
)
from coney_tools.build_claims import SlotTimeout
from coney_tools.config import ConfigError, find_repo_root
from coney_tools.conventions import Conventions, load_conventions, load_proof_rules_at
from coney_tools.local_gate import StepResult, run_command
from coney_tools.task_check_steps import IMPLEMENTED, Runner, Step, StepContext, pytest_workers
from coney_tools.unit_record import log_task_of

#: Exit codes (see the module docstring).
EXIT_FAILED = 1
EXIT_UNABLE = 2
#: The build run gave up waiting for a slot: the same code `coney-tools build` uses.
EXIT_QUEUED = build_run.EXIT_QUEUED
#: The most changed paths and status lines a message names.
SHOWN = 5
#: A detached run (`--detach`): its result and its console output in the worktree's scratch folder (detached_runs.py).
DETACHED = detached_runs.Kind(
    "task check", "coney-tools task check --result", "task-check-result.json", "task-check-detached.out"
)


@dataclass(frozen=True)
class Request:
    """What was asked: the task (default: `CONEY_TASK`, else the branch), the worktree (default: the current checkout),
    the branch the change is measured from (default: `origin/<default branch>`), the id for the build slots, a profile
    bigger than the change needs, whether to go on after a failed check, and whether to only print the plan."""

    task: str | None = None
    worktree: Path | None = None
    base: str | None = None
    agent: str | None = None
    profile: str | None = None
    keep_going: bool = False
    dry_run: bool = False
    max_wait: float = build_cli.DETACHED_MAX_WAIT


@dataclass
class Env:
    """The machine `task check` runs on: the checks by name, the build, whether there is a disc (None: there is, else
    why not), the clock, where lines go, who is running it and how a command is run."""

    steps: Mapping[str, Step]
    #: The build and its tests: checkout, agent id, preset, task and the ctest label of the tests to run (None: all).
    build: Callable[[Path, str, str, str, str | None], build_run.Result]
    disc: Callable[[Path], str | None]
    clock: Callable[[], datetime]
    say: Callable[[str], None]
    who: Callable[[], dict[str, Any]]
    run: Runner = run_command
    #: A runner that streams and logs into the given folder (live_progress.streaming_runner), or None: `run` is used.
    stream: Callable[[Path], Runner] | None = None
    #: Holds a check slot (checkout, agent id, what runs) while a full suite runs; the default holds nothing.
    hold: Callable[[Path, str, str], AbstractContextManager[None]] = lambda root, agent, what: nullcontext()
    #: The pytest workers of the checkout's machine (a check slot's share of the cores); the default runs one.
    workers: Callable[[Path], int] = lambda root: 1
    #: The writer of the run's progress file; `_run` attaches it to the unit's scratch folder. The real environment
    #: hands the same one to the build, the streaming runner and the check slot, so that all of them report to it.
    progress: check_progress.Progress = field(default_factory=check_progress.Progress)
    #: `(checkout, task, conventions, say, write)`: prints the pending work that overlaps the unit again when it differs
    #: from the task file's record of it, and records it (`task_overlap.report_stage`); the default reads nothing.
    overlap: Callable[[Path, str, Conventions, Callable[[str], None], bool], None] = task_overlap.skip_stage


def no_disc_reason(root: Path) -> str | None:
    """Why the tests would not have the disc (as `coney-tools build` would say), or None when they have it."""
    _, disc = build_cli.disc_environment(dict(os.environ), root)
    if disc.source is None:
        return disc.why_not
    given = build_run.env_get(dict(os.environ), "CONEY_DISC")
    if given and not Path(given).exists():
        return f"CONEY_DISC is set to {given}, which does not exist"
    return None


def _say(line: str) -> None:
    """Print a line and flush it at once: a run redirected to a file must show each line as it happens."""
    print(line, flush=True)


def _real_hold(
    max_wait: float, progress: check_progress.Progress | None = None
) -> Callable[[Path, str, str], AbstractContextManager[None]]:
    """The check slot of this machine: the queue of `check_slots`, saying the place while it waits (and telling the
    `progress` file), giving up after `max_wait` minutes with a SlotTimeout."""

    def hold(root: Path, agent: str, what: str) -> AbstractContextManager[None]:
        return check_slots.hold(
            check_slots.default_check_slots(root), agent, root.name, what, _say, max_wait=max_wait, progress=progress
        )

    return hold


def default_env(max_wait: float = build_cli.DETACHED_MAX_WAIT) -> Env:
    """The real machine: the checks of `task_check_steps`, the build through `coney-tools build`'s own code, the disc
    the build would give the tests, and the identity the hook vouched for. Its checks stream their output into a log
    and say a progress line every minute, and pytest waits for a check slot. One `Progress` writes the run's progress
    file and is handed to all of them."""
    progress = check_progress.Progress()
    return Env(
        steps=IMPLEMENTED,
        build=lambda root, agent, preset, task, label: build_cli.build_and_test(
            root, agent, preset, task=task, max_wait=max_wait, progress=progress, label=label
        ),
        disc=no_disc_reason,
        clock=lambda: datetime.now(UTC),
        say=_say,
        who=lambda: stamps.who_from_env("coney-tools task check"),
        stream=lambda logs: live_progress.streaming_runner(_say, logs, progress=progress),
        hold=_real_hold(max_wait, progress),
        workers=_workers,
        progress=progress,
        overlap=task_overlap.report_stage,
    )


def _workers(root: Path) -> int:
    """This machine's pytest workers: its cores shared out over its check slots, so the full suites that may run at
    once together use about every core."""
    return pytest_workers(os.cpu_count(), check_slots.default_check_slots(root).slots)


def agent_for(explicit: str | None, who: Mapping[str, Any], person: Callable[[], str | None]) -> str:
    """The id for the build slots: the one given, else the agent the hook vouched for, else a person's default; a
    ConfigError when nobody can say."""
    if explicit:
        return explicit
    known = str(who.get("agent", {}).get("id", "unknown"))
    if known != "unknown":
        return known
    guess = person()
    if guess:
        return guess
    raise ConfigError("pass --agent <your id> (the build slots need one; docs/guides/coney-tools-build.md)")


def run_check(request: Request, env: Env) -> int:
    """Run the checks the change needs and stamp the result; see the module docstring for the exit codes."""
    try:
        return _run(request, env)
    except ConfigError as error:
        env.say(f"task check: {error}")
        return EXIT_UNABLE


def _run(request: Request, env: Env) -> int:
    """`run_check` without the catch of faults."""
    who = env.who()  # first: a hook's identity file lives only a few minutes
    root = find_repo_root(request.worktree or Path.cwd())
    conventions = load_conventions(root)
    named = unit.task_name(root, request.task)
    task = _log_task(root, named, conventions, env.say)
    base = request.base or f"origin/{conventions.repo.default_branch}"
    conventions, rules_from = _rules(root, base, conventions, env.say)
    dirty = unit.status_lines(root)
    if dirty:
        shown = ", ".join(line[3:] for line in dirty[:SHOWN]) + (" ..." if len(dirty) > SHOWN else "")
        raise ConfigError(f"the worktree has uncommitted changes: {shown}; commit or remove them first")
    change = pr_diff.change_of(root, base, "HEAD")
    if not change.paths:
        raise ConfigError(f"the branch changes no file against {base}: nothing to check")
    needed = proof_profiles.required_profile(conventions, change.paths)
    profile = request.profile or needed
    if not proof_profiles.at_least(profile, needed, conventions):
        raise ConfigError(
            f"profile {profile} is too small: this change needs {needed} (touches {_first(change.paths)})"
        )
    checks = proof_profiles.required_checks(conventions, profile, change.paths)
    build = conventions.proof.build
    builds = [name for name in checks if name in build.checks]
    env.say(
        f"task check: {task}: profile {profile}, {len(checks)} checks, change {change.sha256[:12]} on "
        f"{change.base[:10]} ({len(change.paths)} files: {_first(change.paths)})"
    )
    env.say("  checks: " + ", ".join(checks))
    scope = proof_profiles.pytest_scope(conventions, change.paths) if "pytest" in checks else None
    if scope is not None:
        env.say(f"  pytest: {scope.text}")
    tests = _ctest_scope(root, conventions, change) if build.checks[-1] in builds else None
    if tests is not None:
        env.say(f"  ctest: {tests.text}")
    env.overlap(root, named, conventions, env.say, not request.dry_run)
    if request.dry_run:
        env.say("  dry run: nothing was run and no stamp was written")
        return 0
    if builds:
        why_not = env.disc(root)
        if why_not:
            raise ConfigError(f"the full suite needs the disc: {why_not}")
    started = env.clock()
    # Only a build and a full suite take a slot, so only a run that has one needs an id for it.
    holds = bool(builds) or (scope is not None and scope.whole)
    agent = agent_for(request.agent, who, _person) if holds else ""
    folder = unit.Unit.of(root, task).dir
    run = env.stream(folder / "check-logs") if env.stream is not None else env.run

    def hold(what: str) -> AbstractContextManager[None]:
        """A check slot for this run's checkout and id."""
        return env.hold(root, agent, what)

    context = StepContext(root, base, change.paths, conventions, run, hold, env.workers(root) if scope else 1)
    # From here the run writes where it is; whatever ends it without a verdict (a refusal, an interrupt, a wait that
    # gave up) leaves the file abandoned, and the heartbeat keeps it fresh while a check says nothing.
    progress = env.progress
    progress.attach(folder / check_progress.FILE_NAME)
    progress.plan(len(checks))
    try:
        with live_progress.Heartbeat(live_progress.HEARTBEAT_SECONDS, progress.beat):
            outcome = _execute(request, env, context, checks, builds, task, agent, change, tests)
            if isinstance(outcome, int):
                progress.abandon()
                return outcome
            ran, failed, record, counts, counted = outcome
            detail_more: dict[str, Any] = {}
            _unchanged(root, base, change)
            if scope is not None and "pytest" in ran:
                detail_more["pytest_scope"] = proof_profiles.scope_record(scope)
            if tests is not None and build.checks[-1] in ran:
                # the list of test files is too long for the stamp's detail: it is kept beside the stamps, by hash
                ctest_scope.write_files(unit.Unit.of(root, task).dir / ctest_scope.FOLDER, tests)
                detail_more["ctest_scope"] = ctest_scope.scope_record(tests)
                if tests.narrowed:  # narrowed by reach: CI and the after-merge disc suite run the rest
                    detail_more["ctest_narrowed"] = tests.narrowed
            progress.finish(not failed)
            return _stamp(
                env,
                root,
                conventions,
                task,
                change,
                who,
                profile,
                ran,
                failed,
                record,
                counts,
                counted,
                started,
                rules_from,
                detail_more,
            )
    except BaseException:
        progress.abandon()
        raise


def _ctest_scope(root: Path, conventions: Conventions, change: pr_diff.Change) -> ctest_scope.CtestScope:
    """The C++ tests the change needs (`proof.ctest`), from its C++ files at its tip: what `pr verify` recomputes."""
    tidy = conventions.ci.tidy
    tree = ctest_scope.tree_at(root, change.tip, tidy.folders, (*tidy.sources, *tidy.headers))
    return ctest_scope.ctest_scope(conventions, change.paths, tree)


def _log_task(root: Path, task: str, conventions: Conventions, say: Callable[[str], None]) -> str:
    """The task whose log the stamp goes in: a fix task's parent (its unit record says so), else `task` itself, so a
    fix task named by its own id is stamped where `pr verify` and `pr open` read."""
    owner = log_task_of(unit.Unit.of(root, task).dir, conventions.tasks.start.unit_file, conventions.tasks.file) or task
    if owner != task:
        say(f"task check: {task} is a fix of {owner}: its stamps go in {owner}'s log")
    return owner


def _rules(root: Path, base: str, own: Conventions, say: Callable[[str], None]) -> tuple[Conventions, str]:
    """The conventions with the `proof` section and path classes of `base`, so a branch cannot shrink the profile it is
    checked against by editing them (they are documentation paths themselves), and which rules those are (`base`, or
    `branch` while the base has no `proof` section yet, said so). A base whose rules cannot be read is a ConfigError:
    the run stops with exit 2 rather than check a branch against its own rules."""
    rules, which = load_proof_rules_at(root, base, own)
    if which == "branch":
        say(f"task check: {base} has no proof section yet; using this branch's .github/conventions.yml")
    return rules, which


def _person() -> str | None:
    """A person's default id for the build slots (None for an agent, which must name itself)."""
    return build_cli.default_agent_id()


def _first(paths: tuple[str, ...]) -> str:
    """The first few changed paths, for a message."""
    return ", ".join(paths[:SHOWN]) + (" ..." if len(paths) > SHOWN else "")


def _execute(
    request: Request,
    env: Env,
    context: StepContext,
    checks: list[str],
    builds: list[str],
    task: str,
    agent: str,
    change: pr_diff.Change,
    cpp: ctest_scope.CtestScope | None,
) -> int | tuple[list[str], list[str], Path | None, dict[str, int] | None, dict[str, dict[str, int]]]:
    """Run the checks in order, stopping at the first failure unless asked not to; the build runs the C++ tests of
    `cpp` (None, as with no test check: all of them). Returns the exit code when the run cannot go on (the build had
    no slot), else the checks that ran, those that failed, the build record's path, the build's test counts and what
    the other checks counted (pytest's), by check."""
    ran: list[str] = []
    failed: list[str] = []
    record: Path | None = None
    tests: dict[str, int] | None = None
    counted: dict[str, dict[str, int]] = {}
    queue = list(checks)
    while queue:
        name = queue.pop(0)
        if name in builds:
            queue = [other for other in queue if other not in builds]
            env.say(f"  [{_at()}] start {' and '.join(builds)} (one build run)")
            env.progress.start(" and ".join(builds), "build", checks.index(builds[0]) + 1)
            results = _build_results(env, context, task, agent, change, builds, cpp.label if cpp else None)
            if isinstance(results, int):
                return results
            step_results, record, tests = results
        else:
            step = env.steps.get(name)
            if step is None:
                raise ConfigError(f"check {name!r} is in .github/conventions.yml but this tool does not know it")
            started = env.clock()
            env.say(f"  [{_at()}] start {name}")
            env.progress.start(name, name, checks.index(name) + 1)
            try:
                step_results = [_timed(step(context), env, started)]
            except SlotTimeout as timeout:
                env.say(f"task check: {check_slots.queued_message(timeout, name)}; run task check again")
                return EXIT_QUEUED
        for result in step_results:
            ran.append(result.name)
            if result.ok and result.counts:
                counted[result.name] = dict(result.counts)
            env.say(f"  [{_at()}] {'ok  ' if result.ok else 'FAIL'} {result.name}: {result.detail}")
            for line in result.output:
                env.say(f"       {line}")
            if not result.ok:
                failed.append(result.name)
        if failed and not request.keep_going:
            break
    return ran, failed, record, tests, counted


def _at() -> str:
    """The time of day for a progress line (`14:03:09`): the wall clock, not `env.clock`, which stamps are made from."""
    return live_progress.clock_text()


def _timed(result: StepResult, env: Env, started: datetime) -> StepResult:
    """`result` with the minutes it took in its detail when that is a minute or more."""
    minutes = int((env.clock() - started).total_seconds() // 60)
    return result if minutes < 1 else replace(result, detail=f"{result.detail} ({minutes} min)")


def _build_results(
    env: Env,
    context: StepContext,
    task: str,
    agent: str,
    change: pr_diff.Change,
    builds: list[str],
    label: str | None,
) -> int | tuple[list[StepResult], Path | None, dict[str, int] | None]:
    """Run the build once and answer its two checks from what it left: the first (`msvc-ci-build`) is whether it
    built, the second (`ctest-full-disc`) whether its build record is one clean run with the disc of the whole suite
    or, with a `label`, of the tests `ctest -L <label>` selects. Returns the exit code when the build gave up waiting
    for a slot."""
    preset = context.conventions.proof.build.preset
    tests = f"-L {label}" if label else "--full"
    env.say(
        f"  build: coney-tools build --preset {preset} --test {tests} as {agent} (waits for a slot; may take a while)"
    )
    result = env.build(context.root, agent, preset, task, label)
    if result.exit_code == EXIT_QUEUED:
        env.say("task check: the build gave up waiting for a slot (queued); nothing was stamped, run task check again")
        return EXIT_QUEUED
    build_name, test_name = builds[0], builds[-1]
    built = "test" in result.ran or result.exit_code == 0
    tail = tuple(result.summary[-8:])
    if not built:
        # A failed build may still have left a record: the stamp covers it, so the failure can be traced from it.
        counts = asdict(result.tests) if result.tests is not None else None
        failure = StepResult(build_name, False, f"failed (exit {result.exit_code})", output=tail)
        return [failure], result.record, counts
    results = [StepResult(build_name, True, f"preset {preset} built")]
    if result.record is None:
        raise ConfigError("the build left no build record (is this a git checkout with a task name?): cannot stamp")
    record = build_record.read(result.record.parent, result.record.stem)
    problems = build_record.problems(record, tip=change.tip, preset=preset, label=label)
    counts = record.get("tests") if isinstance(record.get("tests"), dict) else None
    detail = (
        f"{counts['passed']} passed, {counts['failed']} failed, {counts['skipped']} skipped of {counts['total']}"
        if counts
        else "no test run"
    )
    if label and counts is not None and not counts.get("total"):
        # A branch made before the test files had labels lists none: its narrowed run selects nothing.
        problems.append(f"no test has the label {label}: rebase onto the default branch (the labels are new)")
    if problems:
        results.append(StepResult(test_name, False, detail, output=tuple(problems or tail)))
    else:
        results.append(StepResult(test_name, True, detail))
    return results, result.record, counts


def _unchanged(root: Path, base: str, change: pr_diff.Change) -> None:
    """Refuse (ConfigError) when the worktree is not as the run began: a commit, an edit or a file a hook wrote while
    the checks ran means the stamp would describe something else."""
    dirty = unit.status_lines(root)
    now = pr_diff.change_of(root, base, "HEAD")
    if dirty:
        shown = ", ".join(line[3:] for line in dirty[:SHOWN])
        raise ConfigError(f"the worktree changed while the checks ran ({shown}): no stamp; commit it and run again")
    if now.tip != change.tip or now.sha256 != change.sha256:
        raise ConfigError("the branch changed while the checks ran (a new commit): no stamp; run task check again")


def _stamp(
    env: Env,
    root: Path,
    conventions: Conventions,
    task: str,
    change: pr_diff.Change,
    who: dict[str, Any],
    profile: str,
    ran: list[str],
    failed: list[str],
    record: Path | None,
    tests: dict[str, int] | None,
    counted: dict[str, dict[str, int]],
    started: datetime,
    rules_from: str,
    detail_more: Mapping[str, Any],
) -> int:
    """Append the `checked` stamp and say so; 0 for a pass, 1 for a fail. The build's counts go in `tests`, which
    `pr verify` matches against the build record; a check's own counts (pytest's) go under its name, for the facts;
    `detail_more` is added as it is."""
    passed = not failed
    covers = {
        stamps.CHANGE: change.sha256,
        "checks_sha256": stamps.sha256_hex(stamps.canonical(sorted(ran))),
    }
    detail: dict[str, Any] = {
        "profile": profile,
        "rules": rules_from,
        "checks": ran,
        "minutes": int((env.clock() - started).total_seconds() // 60),
    }
    if failed:
        detail["failed"] = failed
    if record is not None:
        covers["build_record_sha256"] = record.stem
        detail["preset"] = conventions.proof.build.preset
    if tests is not None:
        detail["tests"] = dict(tests)
    for name, counts in counted.items():
        detail[name] = counts
    detail.update(detail_more)  # which Python tests ran (`pytest_scope`), which `pr verify` holds to the paths
    stamp = stamps.append(
        unit.Unit.of(root, task).stamps,
        rules=conventions.stamps,
        task=task,
        step="checked",
        subject="",
        result="pass" if passed else "fail",
        commit=change.tip,
        covers=covers,
        detail=detail,
        who=who,
        clock=env.clock,
    )
    env.say(
        f"task check: checked {'pass' if passed else 'FAIL'}: profile {profile}, {len(ran)} checks"
        + (f", failed: {', '.join(failed)}" if failed else "")
        + f"; stamp #{stamp.seq} for change {change.sha256[:12]}"
    )
    return 0 if passed else EXIT_FAILED


# --- the command line ----------------------------------------------------------------------------------------------


def add_parser(commands: Any) -> None:
    """Register `coney-tools task check` in the `task` group."""
    check = commands.add_parser(
        "check",
        help="run every check the change needs (for engine changes the MSVC ci build and the C++ tests the change "
        "reaches, with the disc) and stamp the result, bound to the change",
    )
    check.add_argument("name", nargs="?", metavar="TASK", help="the task (default: CONEY_TASK, else the branch)")
    check.add_argument("--worktree", type=Path, help="the task's worktree (default: the current checkout)")
    check.add_argument("--base", help="the branch the change is measured from (default: origin/<default branch>)")
    check.add_argument("--agent", help="your id for the build slots (default: the agent the hook vouched for)")
    check.add_argument("--profile", help="run a bigger profile than the change needs (docs, tooling or full)")
    check.add_argument("--keep-going", action="store_true", help="run every check even after one fails")
    check.add_argument("--dry-run", action="store_true", help="print the profile and the checks; run nothing")
    check.add_argument(
        "--max-wait",
        type=float,
        default=build_cli.DETACHED_MAX_WAIT,
        metavar="MIN",
        help="minutes the build may wait for a slot before giving up with exit 75 (default %(default)s)",
    )
    check.add_argument(
        "--detach",
        action="store_true",
        help="run apart from this shell (a killed caller does not end it), return at once; --result shows the outcome",
    )
    check.add_argument(
        "--result",
        action="store_true",
        help="print the output and outcome of the worktree's last --detach run (exit 75 while it runs)",
    )
    # Internal: where a --detach run's child records its outcome.
    check.add_argument("--result-file", type=Path, help=argparse.SUPPRESS)


def _detached_folder(args: argparse.Namespace) -> tuple[Path, Path]:
    """The checkout a detached run is about and its folder in the scratch directory (where `build` keeps its own)."""
    root = find_repo_root(args.worktree or Path.cwd())
    return root, build_cli.scratch_root(root) / root.name


def child_command(args: argparse.Namespace, root: Path, result: Path) -> list[str]:
    """The command line of a --detach run's child: this run's options, the worktree spelled out, and its result file."""
    command = [sys.executable, "-m", "coney_tools.cli", "task", "check", *([args.name] if args.name else [])]
    command += ["--worktree", str(root), "--max-wait", str(args.max_wait)]
    for flag, value in (("--base", args.base), ("--agent", args.agent), ("--profile", args.profile)):
        command += [flag, value] if value else []
    command += ["--keep-going"] if args.keep_going else []
    return [*command, "--result-file", str(result)]


def run(args: argparse.Namespace) -> int:
    """`task check`: the command line of `run_check`; with `--detach` the same run apart from the caller, with
    `--result` that run's outcome (detached_runs.py)."""
    if args.result or args.detach:
        if args.dry_run or (args.result and args.detach):
            raise ConfigError("task check: --detach and --result go alone, without --dry-run or each other")
        root, folder = _detached_folder(args)
        if args.result:
            return detached_runs.read(DETACHED, folder)
        result, _ = DETACHED.files(folder)
        agent = args.agent or os.environ.get("CONEY_AGENT") or root.name
        return detached_runs.start(DETACHED, folder, child_command(args, root, result), root, agent)
    if args.result_file is not None:
        return detached_runs.child(DETACHED, args.result_file, args.agent or "task-check", lambda: _run_here(args))
    return _run_here(args)


def _run_here(args: argparse.Namespace) -> int:
    """`task check` in this process."""
    request = Request(
        task=args.name,
        worktree=args.worktree,
        base=args.base,
        agent=args.agent,
        profile=args.profile,
        keep_going=args.keep_going,
        dry_run=args.dry_run,
        max_wait=args.max_wait,
    )
    if args.dry_run:
        return run_check(request, default_env(args.max_wait))
    # The whole run is recorded as running in the build queue, so `workspace clean` never takes the worktree's build for
    # idle during the checks between its builds (build_claims.activity).
    try:
        root = find_repo_root(args.worktree or Path.cwd())
    except ConfigError:
        return run_check(request, default_env(args.max_wait))
    with build_claims.unit_activity(root, args.agent or "task-check", "task check"):
        return run_check(request, default_env(args.max_wait))
