# SPDX-License-Identifier: GPL-3.0-or-later
"""`coney-tools task check`: run everything a change must pass here, then stamp it
(docs/guides/coney-tools-repo.md#task-check).

What a change needs is decided by the paths it touches (`proof_profiles`, the `proof` section of
`.github/conventions.yml`): a documentation change runs the docs checks, a tooling change adds ruff, mypy, pytest and
actionlint, and anything else also builds with the MSVC `ci` preset (/WX) and runs the whole suite with the disc
(`coney-tools build` leaves a build record the stamp covers). The result is one `checked` stamp, `pass` or `fail`,
bound to the change (`covers.diff_sha256`, `pr_diff`), so a clean rebase keeps it and any edit makes it stale. A fault
of the machine (a program missing, no disc, no build slot, a worktree that changed under the run) is not a verdict
about the change and writes no stamp. The profiles and the path classes are read from the base's
`.github/conventions.yml`, not the branch's, so a branch cannot shrink the profile it is checked against (`pr verify`
must derive them from the base too). Only the `proof` section and the scope lists of `paths` are read, ignoring keys
the branch's parser does not know; the stamp's `detail.rules` says `base`, or `branch` when the base has no `proof`
section yet. A base whose rules cannot be read stops the run (exit 2).

Exit codes: 0 every check passed and the stamp is written; 1 a check failed (a `fail` stamp is written); 2 it cannot
run or cannot stamp (nothing is written); 75 the build gave up waiting for a slot (nothing is written; run it again).
Everything that touches the machine is in `Env`, so the order, the stamp and the refusals are tested with fakes.
"""

from __future__ import annotations

import argparse
import os
from collections.abc import Callable, Mapping
from dataclasses import asdict, dataclass
from datetime import UTC, datetime
from pathlib import Path
from typing import Any

from coney_tools import build_cli, build_record, build_run, pr_diff, proof_profiles, stamps, unit
from coney_tools.config import ConfigError, find_repo_root
from coney_tools.conventions import Conventions, load_conventions, load_proof_rules_at
from coney_tools.local_gate import StepResult, run_command
from coney_tools.task_check_steps import IMPLEMENTED, Runner, Step, StepContext

#: Exit codes (see the module docstring).
EXIT_FAILED = 1
EXIT_UNABLE = 2
#: The build run gave up waiting for a slot: the same code `coney-tools build` uses.
EXIT_QUEUED = build_run.EXIT_QUEUED
#: The most changed paths and status lines a message names.
SHOWN = 5


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
    build: Callable[[Path, str, str, str], build_run.Result]
    disc: Callable[[Path], str | None]
    clock: Callable[[], datetime]
    say: Callable[[str], None]
    who: Callable[[], dict[str, Any]]
    run: Runner = run_command


def _no_disc_reason(root: Path) -> str | None:
    """Why the tests would not have the disc (as `coney-tools build` would say), or None when they have it."""
    _, disc = build_cli.disc_environment(dict(os.environ), root)
    if disc.source is None:
        return disc.why_not
    given = build_run.env_get(dict(os.environ), "CONEY_DISC")
    if given and not Path(given).exists():
        return f"CONEY_DISC is set to {given}, which does not exist"
    return None


def default_env(max_wait: float = build_cli.DETACHED_MAX_WAIT) -> Env:
    """The real machine: the checks of `task_check_steps`, the build through `coney-tools build`'s own code, the disc
    the build would give the tests, and the identity the hook vouched for."""
    return Env(
        steps=IMPLEMENTED,
        build=lambda root, agent, preset, task: build_cli.build_and_test(
            root, agent, preset, task=task, max_wait=max_wait
        ),
        disc=_no_disc_reason,
        clock=lambda: datetime.now(UTC),
        say=print,
        who=lambda: stamps.who_from_env("coney-tools task check"),
    )


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
    task = unit.task_name(root, request.task)
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
    if request.dry_run:
        env.say("  dry run: nothing was run and no stamp was written")
        return 0
    if builds:
        why_not = env.disc(root)
        if why_not:
            raise ConfigError(f"the full suite needs the disc: {why_not}")
    started = env.clock()
    context = StepContext(root, base, change.paths, conventions, env.run)
    # Only a build takes a slot, so only a profile that builds needs an id for one.
    agent = agent_for(request.agent, who, _person) if builds else ""
    outcome = _execute(request, env, context, checks, builds, task, agent, change)
    if isinstance(outcome, int):
        return outcome
    ran, failed, record, tests = outcome
    _unchanged(root, base, change)
    return _stamp(env, root, conventions, task, change, who, profile, ran, failed, record, tests, started, rules_from)


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
) -> int | tuple[list[str], list[str], Path | None, dict[str, int] | None]:
    """Run the checks in order, stopping at the first failure unless asked not to. Returns the exit code when the run
    cannot go on (the build had no slot), else the checks that ran, those that failed, the build record's path and the
    test counts."""
    ran: list[str] = []
    failed: list[str] = []
    record: Path | None = None
    tests: dict[str, int] | None = None
    queue = list(checks)
    while queue:
        name = queue.pop(0)
        if name in builds:
            queue = [other for other in queue if other not in builds]
            results = _build_results(env, context, task, agent, change, builds)
            if isinstance(results, int):
                return results
            step_results, record, tests = results
        else:
            step = env.steps.get(name)
            if step is None:
                raise ConfigError(f"check {name!r} is in .github/conventions.yml but this tool does not know it")
            started = env.clock()
            step_results = [_timed(step(context), env, started)]
        for result in step_results:
            ran.append(result.name)
            env.say(f"  {'ok  ' if result.ok else 'FAIL'} {result.name}: {result.detail}")
            for line in result.output:
                env.say(f"       {line}")
            if not result.ok:
                failed.append(result.name)
        if failed and not request.keep_going:
            break
    return ran, failed, record, tests


def _timed(result: StepResult, env: Env, started: datetime) -> StepResult:
    """`result` with the minutes it took in its detail when that is a minute or more."""
    minutes = int((env.clock() - started).total_seconds() // 60)
    return (
        result
        if minutes < 1
        else StepResult(result.name, result.ok, f"{result.detail} ({minutes} min)", output=result.output)
    )


def _build_results(
    env: Env, context: StepContext, task: str, agent: str, change: pr_diff.Change, builds: list[str]
) -> int | tuple[list[StepResult], Path | None, dict[str, int] | None]:
    """Run the build once and answer its two checks from what it left: the first (`msvc-ci-build`) is whether it
    built, the second (`ctest-full-disc`) whether its build record is one whole clean run with the disc. Returns the
    exit code when the build gave up waiting for a slot."""
    preset = context.conventions.proof.build.preset
    env.say(f"  build: coney-tools build --preset {preset} --test as {agent} (waits for a slot; this takes a while)")
    result = env.build(context.root, agent, preset, task)
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
    problems = build_record.problems(record, tip=change.tip, preset=preset)
    counts = record.get("tests") if isinstance(record.get("tests"), dict) else None
    detail = (
        f"{counts['passed']} passed, {counts['failed']} failed, {counts['skipped']} skipped of {counts['total']}"
        if counts
        else "no test run"
    )
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
    started: datetime,
    rules_from: str,
) -> int:
    """Append the `checked` stamp and say so; 0 for a pass, 1 for a fail."""
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
        help="run every check the change needs (MSVC ci build and the full suite with the disc for engine changes) "
        "and stamp the result, bound to the change",
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


def run(args: argparse.Namespace) -> int:
    """`task check`: the command line of `run_check`."""
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
    return run_check(request, default_env(args.max_wait))
