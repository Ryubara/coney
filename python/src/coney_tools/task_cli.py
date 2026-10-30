# SPDX-License-Identifier: GPL-3.0-or-later
"""The `coney-tools task ...` commands: create a task from issues, start it, search the leads, refresh the index,
check it and review it, and show the engineer slots.

`task create` is the coordinator's triage step (docs/guides/issues.md#task-files): it claims the issues through the
claim Action, waits until the claim is certain and then writes `<scratch>/<task>/TASK.md`. `task start` is the
engineer's first step (docs/guides/issues.md#starting): it makes the branch, worktree and unit record of a claimed
task. `task leads` runs the same search without claiming, and `task index` rebuilds the cached index. `task check`
(task_check.py) is the local gate that writes the `checked` stamp, and `task review` (task_review.py) the review stage
that records the `reviewed` one. `gh`, the clock and the sleep are injected, so the tests never touch the network or
wait.
"""

from __future__ import annotations

import argparse
import json
import os
import shutil
import subprocess
import sys
import time
from collections.abc import Callable
from datetime import UTC, datetime
from pathlib import Path

from coney_tools import build_claims, slot_fill, slot_fill_blocks, slot_waivers, stamps, task_check, task_slots
from coney_tools.audit_github import Runner, run_gh
from coney_tools.build_claims import MAIN_LANE
from coney_tools.config import ConfigError, find_repo_root, load_config, main_checkout, workspace_folder
from coney_tools.conventions import Conventions, load_conventions
from coney_tools.issue_claim_model import Card, parse_scope
from coney_tools.issue_claim_remote import GhRemote, Remote
from coney_tools.pr_hold import holds_folder, read_active
from coney_tools.slot_fill_github import GhSources, bounded_runner
from coney_tools.task_create import CreateEnv, CreateRequest, create_task, match_lines
from coney_tools.task_leads import LeadsResult, Searcher
from coney_tools.task_leads_index import CachedIndex, prepare_index
from coney_tools.task_review import add_review_parser, run_review_command
from coney_tools.task_start import StartEnv, StartRequest, start_task
from coney_tools.unit_record import UnitRecord
from coney_tools.workspace_clean import list_worktrees


def add_task_parser(groups: argparse._SubParsersAction[argparse.ArgumentParser]) -> None:
    """Add the `task` group: `create`, `start`, `leads`, `index`, `check` and `review`."""
    group = groups.add_parser("task", help="claim issues and write a task file (docs/guides/issues.md#task-files)")
    commands = group.add_subparsers(dest="command", required=True)
    create = commands.add_parser(
        "create",
        help="claim the issues through the claim Action, wait until the claim holds, write <scratch>/<task>/TASK.md",
    )
    create.add_argument(
        "issues", nargs="*", type=int, metavar="ISSUE", help="the issues of the task (before the options)"
    )
    create.add_argument("--terms", nargs="+", default=[], metavar="TERM", help="names to search the indexes for")
    create.add_argument("--scope", help="the planned folders, files and addresses, comma separated")
    create.add_argument("--kind", help="the task kind (default: from the labels)")
    create.add_argument("--name", help="the task id (default: <first issue>-<words of its title>)")
    create.add_argument(
        "--fix-of",
        type=int,
        metavar="ISSUE",
        help="a fix task: reuse your claim on this issue (issues given beside it are the fix's own)",
    )
    create.add_argument("--resume", action="store_true", help="continue a claim of yours that landed late")
    create.add_argument("--dry-run", action="store_true", help="check and print; post and write nothing")
    start = commands.add_parser(
        "start",
        help="make the branch, worktree and unit record of a claimed task and stamp assigned and claimed",
    )
    start.add_argument("task", nargs="?", help="the task id (default: CONEY_TASK)")
    start.add_argument("--base", metavar="REF", help="what the branch starts from (default: <remote>/<default branch>)")
    start.add_argument("--remote", default="origin", help="the git remote to fetch from (default: origin)")
    start.add_argument("--no-fetch", action="store_true", help="start from the base as it is here, without fetching")
    start.add_argument("--dry-run", action="store_true", help="check and print; make, stamp and post nothing")
    start.add_argument(
        "--adopt",
        action="store_true",
        help="make a unit of the branch named like the task that exists already (made before units existed or opened "
        "by hand), keeping its name and its worktree; an old stamp log is moved aside",
    )
    start.add_argument(
        "--adopt-branch",
        metavar="BRANCH",
        help="the same for an existing branch that is not named like the task",
    )
    slots = commands.add_parser(
        "slots",
        help="show the engineer tracks (reimplementation, fix, Studio, networking, renderer) and the machine's "
        "places, or check that an agent may start (the coordinator's Agent guard)",
    )
    slots.add_argument("--json", action="store_true", help="print the tracks as JSON")
    slots.add_argument(
        "--fill-check",
        action="store_true",
        help="exit 1 with what to start when a place is free, work can be started and nothing excuses the gap "
        "(the coordinator's Stop hook; docs/guides/slot-fill.md)",
    )
    slots.add_argument(
        "--session", metavar="ID", help="with --fill-check: the session being checked; its blocks in a row are counted"
    )
    slots.add_argument(
        "--waive",
        type=int,
        metavar="N",
        help="leave N free places free for --hours, for the --reason; logged, expiring, listed by `task slots`",
    )
    slots.add_argument("--reason", metavar="CODE", help="with --waive: a code of tasks.start.fill.waivers.codes")
    slots.add_argument(
        "--hours", type=float, metavar="H", help="with --waive: how long it runs (within the code's hours)"
    )
    slots.add_argument("--note", metavar="TEXT", help="with --waive: why, in words")
    slots.add_argument(
        "--issues",
        metavar="N,M",
        help="with --waive: the issues the work is blocked on (a code that blocks issues names them)",
    )
    slots.add_argument(
        "--admit",
        action="store_true",
        help="check a start instead of listing: exit 0 when it may, 3 with the reasons when it is refused "
        "(any other code: it could not judge)",
    )
    slots.add_argument("--agent-type", default="engineer", help="with --admit: the subagent type (default: engineer)")
    slots.add_argument("--task", help="with --admit: the task id (default: the `Task id:` line of the brief)")
    slots.add_argument(
        "--brief", metavar="FILE", help="with --admit: the agent's brief, a file or - for standard input"
    )
    slots.add_argument(
        "--reserve", action="store_true", help="with --admit: keep the place for the agent until its task start"
    )
    slots.add_argument("--model", help="with --admit: the Agent call's model (a Studio unit must name an allowed one)")
    slots.add_argument(
        "--effort", help="with --admit: the Agent call's effort (a Studio unit must name an allowed one)"
    )
    leads = commands.add_parser("leads", help="search the indexes for the terms (no claim, nothing written)")
    leads.add_argument("--terms", nargs="+", required=True, metavar="TERM", help="names to search for")
    leads.add_argument("--issue", type=int, metavar="N", help="also match the identifiers this issue writes")
    leads.add_argument("--refresh", action="store_true", help="rebuild the index first")
    index = commands.add_parser("index", help="rebuild the cached leads index")
    index.add_argument("--no-gh", action="store_true", help="leave the merged pull requests out")
    task_check.add_parser(commands)
    add_review_parser(commands)


def _head(root: Path) -> str:
    """The commit `HEAD` points at, or `unknown` when git cannot say (the index is then rebuilt by age only)."""
    try:
        done = subprocess.run(
            ["git", "-C", str(root), "rev-parse", "HEAD"], capture_output=True, text=True, check=False
        )
    except OSError:
        return "unknown"
    return done.stdout.strip() if done.returncode == 0 else "unknown"


def scratch_folder(root: Path) -> Path:
    """The scratch folder: `scratch_dir` of the local config, else the workspace's `scratch/`."""
    try:
        configured = load_config(root).paths.get("scratch_dir")
    except ConfigError:
        configured = None
    return configured or workspace_folder(root, "scratch")


def _index(root: Path, conventions: Conventions, run: Runner | None, refresh: bool, now: datetime) -> CachedIndex:
    """The prepared index (cached under the scratch folder)."""
    cache = scratch_folder(root) / "leads" / "index.json"
    return prepare_index(root, conventions, cache, _head(root), now, run, refresh)


def _workspace_trees(root: Path) -> list[Path]:
    """The worktrees git lists under the workspace's `worktrees/` folder (never the main checkout); none when git
    cannot say."""
    try:
        base = workspace_folder(root, "worktrees").resolve()
        listed = list_worktrees(main_checkout(root))[1:]
    except (ConfigError, OSError):
        return []
    return [tree.path for tree in listed if tree.path.resolve().is_relative_to(base)]


def _build_slots(root: Path) -> int:
    """The machine's build slots (`build_slots` in coney.local.toml, as `coney-tools build` reads it): the tracks'
    machine limit is a multiple of it."""
    return build_claims.default_slots(MAIN_LANE, root).slots


#: How long one `gh` call of the claim check may take: the Agent guard waits for it, and GitHub down must not hold it.
GH_CHECK_SECONDS = task_slots.GH_READ_SECONDS


def _bounded_gh(arguments: list[str]) -> str:
    """Run a `gh` command (read only) for at most `GH_CHECK_SECONDS`; OSError when it fails or takes longer."""
    try:
        done = subprocess.run(
            ["gh", *arguments], capture_output=True, text=True, encoding="utf-8", timeout=GH_CHECK_SECONDS, check=False
        )
    except (OSError, subprocess.SubprocessError) as error:
        raise OSError(f"gh could not run: {error}") from error
    if done.returncode != 0:
        raise OSError(f"gh {' '.join(arguments[:2])} failed: {done.stderr.strip() or done.stdout.strip()}")
    return done.stdout


def _free_gb(folder: Path) -> float | None:
    """The free space, in GB, of the drive `folder` (or its nearest existing parent) is on; None when unreadable."""
    here = folder
    while not here.exists() and here != here.parent:
        here = here.parent
    try:
        return shutil.disk_usage(here).free / 1024**3
    except OSError:
        return None


def _waiver_who() -> str:
    """Who makes a waiver: the agent the hook vouched for (`type id`), else `unverified`."""
    found = stamps.who_from_env("coney-tools task slots")
    agent = found.get("agent") or {}
    if found.get("source") != "hook":
        return "unverified"
    return f"{agent.get('type', 'unknown')} {agent.get('id', 'unknown')}"


def _issue_numbers(text: str | None) -> tuple[int, ...]:
    """The issue numbers of `--issues 12,13` (a `#` in front of one is fine); ConfigError for anything else."""
    if not text:
        return ()
    try:
        return tuple(int(part.strip().lstrip("#")) for part in text.split(",") if part.strip())
    except ValueError:
        raise ConfigError(f"--issues {text!r}: expected issue numbers such as 1030,1031") from None


def _run_waive(args: argparse.Namespace, root: Path, conventions: Conventions, now: datetime) -> int:
    """`task slots --waive N`: record the waiver (ConfigError when it is not allowed) and say what it covers."""
    for name in ("reason", "hours", "note"):
        if getattr(args, name) is None:
            raise ConfigError(f"--waive needs --{name}")
    waiver = slot_waivers.add_waiver(
        scratch_folder(root),
        conventions.tasks.start.fill,
        task_slots.machine_limit(conventions, _build_slots(root)),
        now,
        args.waive,
        args.reason,
        args.hours,
        args.note,
        _issue_numbers(args.issues),
        _waiver_who(),
    )
    print(f"waiver: {waiver.line(now)}")
    return 0


def _run_fill_check(
    args: argparse.Namespace,
    root: Path,
    conventions: Conventions,
    now: Callable[[], datetime],
    sources: slot_fill.Sources | None,
    free_gb: float | None,
) -> int:
    """`task slots --fill-check`: 1 with the work to start when a place is free for no reason, else 0.

    With `--session` a failing check blocks at most `tasks.start.fill.block.max_in_a_row` times in a row (the Stop
    hook's cap); the turn end after that passes with a `warning:` line. A GitHub that cannot be read passes with a
    warning too."""
    scratch = scratch_folder(root)
    fill = conventions.tasks.start.fill
    read = sources or GhSources(conventions, scratch, bounded_runner(fill.github_seconds), now)
    holds = read_active(holds_folder(scratch, conventions.conclave.watch), now()).holds
    space = free_gb if free_gb is not None else _free_gb(scratch)
    check = slot_fill.fill_check(scratch, conventions, now(), read, _build_slots(root), space, holds)
    warnings = list(check.warnings)
    blocked = check.blocked
    if blocked and args.session and not slot_fill_blocks.may_block(scratch, conventions, args.session, now()):
        blocked = False
        warnings.append(
            f"the check still fails but it has blocked {fill.block_max} turn ends in a row, so this one ends "
            f"(counts again after {fill.block_reset_minutes} minutes): {check.message.splitlines()[0]}"
        )
    elif not blocked and args.session:
        slot_fill_blocks.clear_blocks(scratch, args.session)
    print(check.message if blocked or not check.blocked else "ok: letting the turn end")
    for warning in warnings:
        print(f"warning: {warning}")
    return 1 if blocked else 0


def _run_slots(
    args: argparse.Namespace,
    root: Path,
    conventions: Conventions,
    now: Callable[[], datetime],
    remote: Remote | None,
    sources: slot_fill.Sources | None = None,
    free_gb: float | None = None,
) -> int:
    """`task slots`: the report, or with `--admit` the verdict on one start (0 may start, 3 refused). The claim cards
    (whether a claim is held, whether an issue has the workflow or the Studio label) are read from GitHub, each call
    bounded; a GitHub that cannot be read leaves the claim unchecked and counts every unit, with a `warning:` line. The
    machine's limit comes from its build slots (`_build_slots`). `--fill-check` and `--waive` are the places-full
    commands (docs/guides/slot-fill.md)."""
    modes = [bool(args.admit), bool(args.fill_check), args.waive is not None]
    if sum(modes) > 1:
        raise ConfigError("task slots: --admit, --fill-check and --waive are separate commands")
    if args.fill_check:
        return _run_fill_check(args, root, conventions, now, sources, free_gb)
    if args.waive is not None:
        return _run_waive(args, root, conventions, now())
    scratch = scratch_folder(root)
    build_slots = _build_slots(root)
    rules = conventions.claims
    github = remote or GhRemote(rules.bot_login, rules.card_marker, rules.reply_marker, _bounded_gh)

    def read_card(number: int) -> Card | None:
        """The held claim card of one issue."""
        return task_slots.held_card(github, number, conventions)

    if not args.admit:
        cards = task_slots.Cards(read_card)

        def labels(record: UnitRecord) -> task_slots.Labels | None:
            """A live unit's labels, from its claim cards."""
            return task_slots.unit_labels(record.issues, cards, conventions)

        current = task_slots.scan(scratch, conventions, now(), _workspace_trees(root), labels)
        waivers = slot_waivers.read_waivers(scratch)
        if args.json:
            shown = task_slots.as_json(current, conventions, build_slots)
            shown["waivers"] = [slot_waivers.as_json(waiver) for waiver in slot_waivers.active_waivers(waivers, now())]
            print(json.dumps(shown, indent=2))
        else:
            lines = [*task_slots.report(current, conventions, build_slots), *slot_waivers.report(waivers, now())]
            print("\n".join(lines))
        return 0
    brief = ""
    if args.brief == "-":
        brief = sys.stdin.buffer.read().decode("utf-8", errors="replace")
    elif args.brief:
        brief = Path(args.brief).read_text(encoding="utf-8")
    verdict = task_slots.admit(
        scratch,
        conventions,
        args.agent_type,
        brief,
        now(),
        read_card,
        args.task,
        args.reserve,
        build_slots=build_slots,
        model=args.model or "",
        effort=args.effort or "",
    )
    print(verdict.message.rstrip("\n"))
    if verdict.warning:
        print(f"warning: {verdict.warning}")
    return 0 if verdict.allowed else task_slots.REFUSED


def run_task(
    args: argparse.Namespace,
    run: Runner = run_gh,
    now: Callable[[], datetime] = lambda: datetime.now(UTC),
    sleep: Callable[[float], None] = time.sleep,
    remote: Remote | None = None,
    sources: slot_fill.Sources | None = None,
    free_gb: float | None = None,
) -> int:
    """Dispatch a `task` command; 0 on success, 1 when the claim or the task was refused. `sources` and `free_gb`
    stand in for GitHub and the disk in the tests of `task slots --fill-check`."""
    if args.command == "check":
        return task_check.run(args)
    if args.command == "review":
        return run_review_command(args)
    root = find_repo_root(Path.cwd())
    conventions = load_conventions(root)
    rules = conventions.claims
    if args.command == "slots":
        return _run_slots(args, root, conventions, now, remote, sources, free_gb)
    if args.command == "index":
        index = _index(root, conventions, None if args.no_gh else run, True, now())
        print(f"index: {len(index.entries)} entries at {index.head[:10]}")
        for notice in index.notices:
            print(f"note: {notice}")
        return 0
    # The identity the hook vouched for lives for minutes: read it before anything slow (git, GitHub) happens.
    who = stamps.who_from_env("coney-tools task start") if args.command == "start" else {}
    github: Remote = remote or GhRemote(rules.bot_login, rules.card_marker, rules.reply_marker, run)
    if args.command == "start":
        name = args.task or os.environ.get("CONEY_TASK")
        if not name:
            raise ConfigError("task start: name the task (an argument, or CONEY_TASK)")
        start_env = StartEnv(
            root, github, conventions, scratch_folder(root), now, lambda: who, build_slots=_build_slots(root)
        )
        return start_task(
            StartRequest(name, args.base, args.remote, not args.no_fetch, args.dry_run, args.adopt, args.adopt_branch),
            start_env,
        )
    if args.command == "leads":
        text = ""
        if args.issue is not None:
            issue = github.issue(args.issue)
            text = f"{issue.title}\n{issue.body}"
        index = _index(root, conventions, run, args.refresh, now())
        result = Searcher(index.entries, conventions.tasks.leads).search(list(args.terms), text)
        for line in match_lines(result.matches()):
            print(line)
        if result.unmatched:
            print("no match for: " + ", ".join(result.unmatched))
        for notice in index.notices:
            print(f"note: {notice}")
        return 0
    try:
        scope = parse_scope(args.scope) if args.scope else ()
    except ValueError as error:
        raise ConfigError(f"--scope: {error}") from error
    if not args.issues and args.fix_of is None:
        raise ConfigError("task create: name at least one issue")

    def leads(terms: list[str], text: str) -> LeadsResult:
        index = _index(root, conventions, run, False, now())
        for notice in index.notices:
            print(f"note: {notice}")
        return Searcher(index.entries, conventions.tasks.leads).search(terms, text)

    env = CreateEnv(github, conventions, scratch_folder(root), now, sleep, leads)
    request = CreateRequest(
        issues=tuple(args.issues),
        terms=tuple(args.terms),
        scope=scope,
        kind=args.kind,
        name=args.name,
        fix_of=args.fix_of,
        resume=args.resume,
        dry_run=args.dry_run,
    )
    return create_task(request, env)
