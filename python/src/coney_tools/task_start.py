# SPDX-License-Identifier: GPL-3.0-or-later
"""`task start`: turn a claimed task into a unit of work (docs/guides/issues.md#starting).

The engineer runs it once for a task the coordinator made with `task create`. It checks that the claim is still the
engineer's, then makes everything the later stages look for: the branch and the worktree (in the workspace layout,
from the tip of the default branch), the unit record beside the task file, the first two stamps
(`assigned`, `claimed`) of the task's log, and a `/progress claimed` on each issue.

Nothing is made until every check has passed, so a refused start leaves nothing behind. A start that stops half way
leaves a unit record, and running the command again finishes what is missing instead of making a second unit. A fix
task (`kind: fix`, made by `task create --fix-of`) makes no branch or worktree: it works in the unit of the task it
fixes, so it gets a record that points there, and its `claimed` stamp joins that task's log. `--adopt` makes a unit of a
branch that already exists, with its worktree when it has one (a pull request's branch made before `task start`
existed): the branch keeps its name, so the pull request is not renamed, and the record starts at the commit the branch
left the base. `--adopt-branch BRANCH` is the same for a branch that is not named like the task; either one moves an old
stamp log of the task aside and starts a fresh one (`task_start_adopt`, docs/guides/issues.md#adopting).

The engineer tracks (`task_slots`) are counted and reported here, and a start over the limit (its track's cap, or
the machine's places with the places other tracks keep set aside) is refused while `tasks.start.slots.enforce` is on;
a task whose issue carries the `workflow` label now (`claims.exempt_label`, read from its held claim card) holds no
place while `tasks.start.slots.exempt_workflow` is on, one with the Studio label is in the Studio track and one with
the networking label in the networking track and one with the renderer label in the renderer track. GitHub is read
through `Remote` and the clock and the agent's identity are passed in, so the tests touch neither the network nor the
hooks.
"""

from __future__ import annotations

import subprocess
from collections.abc import Callable, Sequence
from dataclasses import dataclass, replace
from datetime import datetime
from pathlib import Path
from typing import Any

from coney_tools import stamps, task_slots
from coney_tools.build_claims import DEFAULT_SLOTS
from coney_tools.config import ConfigError, main_checkout
from coney_tools.conventions import Conventions
from coney_tools.issue_claim_engine import Engine
from coney_tools.issue_claim_model import Card, iso_time
from coney_tools.issue_claim_remote import Remote
from coney_tools.issue_claim_rules import limit_for, open_task_keys
from coney_tools.task_file import TaskFile, parse_task_file
from coney_tools.unit import head_commit
from coney_tools.unit_record import UnitRecord, read_record, write_record


@dataclass(frozen=True)
class StartRequest:
    """What `task start` was asked to do."""

    task: str
    #: What the branch starts from; default `<remote>/<repo.default_branch>`.
    base: str | None = None
    remote: str = "origin"
    #: Fetch the base's branch from the remote first.
    fetch: bool = True
    dry_run: bool = False
    #: Take over the branch named like the task (and its worktree), made before units existed, instead of making one:
    #: the unit keeps the branch's name, the base is where the branch left `base`, and the history is not touched.
    adopt: bool = False
    #: The same for a branch of another name (a pull request opened by hand whose task has the card's id); an old stamp
    #: log of the task is moved aside. docs/guides/issues.md#adopting.
    adopt_branch: str | None = None


@dataclass
class StartEnv:
    """The world `start_task` works in; every part can be replaced in a test."""

    #: A checkout of the repository (the main one or a worktree); the main checkout is found from it.
    root: Path
    remote: Remote
    conventions: Conventions
    #: The scratch folder; the task is `<scratch>/<task>/`.
    scratch: Path
    now: Callable[[], datetime]
    #: The `who` of the stamps (`stamps.who_from_env`), read once at the start of the command.
    who: Callable[[], dict[str, Any]]
    out: Callable[[str], None] = print
    #: The machine's build slots (`build_slots` in coney.local.toml), which set its limit of units in work.
    build_slots: int = DEFAULT_SLOTS
    #: `(task, record, write) -> lines`: the pending work that overlaps the unit (docs/guides/pending-work.md),
    #: written to the task file when `write` (not for a dry run); None reads none (a test that does not ask for it).
    overlap: Callable[[TaskFile, UnitRecord | None, bool], list[str]] | None = None


def front_sha256(task: TaskFile) -> str:
    """The SHA-256 of what the tools read from a task file (its YAML block), not of the prose the coordinator prunes."""
    front: dict[str, Any] = {
        "task": task.task,
        "issues": list(task.issues),
        "kind": task.kind,
        "claimant": task.claimant,
        "claimed_at": iso_time(task.claimed_at),
        "scope": list(task.scope),
        "terms": list(task.terms),
        "parent": task.parent,
    }
    return stamps.sha256_hex(stamps.canonical(front))


# --- slots ----------------------------------------------------------------------------------------------------------


def _slot_check(task: TaskFile, env: StartEnv, engine: Engine) -> tuple[str, str | None]:
    """(the tracks line for the report, the refusal when the limit is enforced and exceeded). Whether a unit is a
    workflow, Studio, networking or renderer unit comes from the held claim cards `engine` read (their `workflow`,
    `studio`, `networking` and `renderer`), not from any file here; the room is `task_slots.room`, the rule the Agent
    guard applies."""
    conventions = env.conventions
    slots = conventions.tasks.start.slots
    held_cards = engine.held_cards()
    cards = task_slots.Cards(held_cards.get)
    told = task_slots.unit_labels(task.issues, cards, conventions)
    if told is not None and told.workflow:
        return "slots: a workflow task (its issue has the workflow label) holds no place until the cutover", None

    def labels(record: UnitRecord) -> task_slots.Labels | None:
        """Another unit's labels, from the same cards."""
        return task_slots.unit_labels(record.issues, cards, conventions)

    track = task_slots.track_of(
        task.kind,
        told.studio if told is not None else False,
        told.networking if told is not None else False,
        told.renderer if told is not None else False,
    )
    current = task_slots.scan(env.scratch, conventions, env.now(), labels=labels)
    # A kept place whose track had nothing to start when the fill check last looked is open (tasks.start.slots.lapse).
    lapsed = task_slots.read_lapses(env.scratch, conventions, env.now())
    fit = task_slots.room(current, track, task.task, conventions, env.build_slots, task.kind, lapsed)
    line = f"slots: {fit.note(task.task)}"
    if fit.fits:
        return line, None
    if not slots.enforce:
        return line + " (over the limit, which is not enforced: tasks.start.slots.enforce)", None
    busy = [holder for holder in current.holders if holder.task != task.task]
    if fit.track_full:
        why = f"the {track} track is full ({fit.count} of {fit.maximum})"
    else:
        kept = "".join(f", {places} kept for {name}" for name, places in fit.kept.items())
        why = f"the machine has no place for a {track} unit ({fit.total} of {fit.machine} in use{kept})"
    return line, (
        f"{why} (tasks.start.slots in .github/conventions.yml, build_slots {env.build_slots}): "
        f"{', '.join(f'{h.line()} [{h.track}]' for h in busy)}. Finish one (it passes Review) before starting another"
    )


# --- the checks -----------------------------------------------------------------------------------------------------


def _claim_problem(task: TaskFile, user: str, expected_task: str, engine: Engine, remote: Remote) -> str | None:
    """Why the claim on any issue of the task is not `user`'s for `expected_task`, or None while it holds."""
    held = engine.held_cards()
    for number in task.issues:
        issue = remote.issue(number)
        if issue.is_pr or issue.state != "open":
            return f"#{number} is closed" if issue.state != "open" else f"#{number} is a pull request"
        card: Card | None = held.get(number)
        if card is None:
            return f"#{number}: no claim card is held (it was never made, or it was released): ask the coordinator"
        if card.claimant != user:
            return f"#{number} is claimed by {card.claimant}, not {user}"
        if not card.task:
            return (
                f"#{number}: the claim has no task id (it was made by hand, not by `task create`): "
                "ask the coordinator to claim it again with task create"
            )
        if card.task != expected_task:
            return f"#{number}: the claim was made for the task '{card.task}', not '{expected_task}'"
    return None


def _limit_problem(task: TaskFile, user: str, engine: Engine) -> str | None:
    """Why `user` holds more open claims than the limit allows, or None (a fix task reuses its claim).

    A card's `exempt` flag follows the issue's labels (`claims.exempt_label`, `claims.studio_label`,
    `claims.networking_label`, `claims.renderer_label`) only when a claim run passes over it, and a burst of label
    events leaves some cards stale until the sweep (GitHub cancels pending jobs); so the labels are read from each of
    the user's issues here, as `task create`'s checks read them, and a stale card cannot refuse the start.
    """
    rules = engine.rules
    held = [
        card
        if card.exempt or card.claimant != user
        else card.changed(exempt=rules.outside_limit(engine.remote.issue(card.issue).labels))
        for card in engine.held_cards().values()
    ]
    open_tasks = open_task_keys(held, user, 0)
    maintainer = engine.is_maintainer(user)
    limit = limit_for(maintainer, engine.rules)
    if len(open_tasks) > limit:
        return (
            f"{user} holds {len(open_tasks)} open claim(s) ({limit} at most for "
            f"{'a maintainer' if maintainer else 'a contributor'}): give one back with /unclaim first"
        )
    return None


# --- git ------------------------------------------------------------------------------------------------------------


def _git(main: Path, *args: str) -> subprocess.CompletedProcess[str]:
    """Run git in the main checkout; the caller looks at the exit code."""
    return subprocess.run(["git", "-C", str(main), *args], capture_output=True, encoding="utf-8", check=False)


def _fetch_and_resolve(request: StartRequest, env: StartEnv, main: Path) -> tuple[str, str]:
    """(the ref the branch starts from, the commit it is) after fetching when asked; ConfigError when git cannot."""
    default = env.conventions.repo.default_branch
    base = request.base or f"{request.remote}/{default}"
    prefix = f"{request.remote}/"
    if request.fetch and (request.base is None or base.startswith(prefix)):
        branch = default if request.base is None else base[len(prefix) :]
        done = _git(main, "fetch", "--quiet", request.remote, branch)
        if done.returncode != 0:
            raise ConfigError(
                f"git fetch {request.remote} {branch} failed (pass --no-fetch to start from {base} as it is): "
                f"{done.stderr.strip() or 'no output'}"
            )
    found = _git(main, "rev-parse", "--verify", "--quiet", f"{base}^{{commit}}")
    if found.returncode != 0:
        raise ConfigError(
            f"{base}: not a commit in {main} (fetch the remote, or name another --base such as {prefix}{default})"
        )
    return base, found.stdout.strip()


# --- what a start makes -----------------------------------------------------------------------------------------------


def _log_report(log_task: str, env: StartEnv) -> tuple[stamps.Report, str | None]:
    """The stamp log of `log_task` and, when it cannot be written to (damaged, or another task's), why not."""
    folder = env.scratch / log_task / "stamps"
    report = stamps.verify(folder, env.conventions.stamps, task=log_task)
    if report.ok:
        return report, None
    return report, f"{folder}: the stamp log cannot be used: {report.problems[0]}"


def _missing_steps(record: UnitRecord, report: stamps.Report) -> list[str]:
    """The stamps the log of `record` lacks: `assigned` and `claimed` for an ordinary task, only `claimed` for a fix
    (the parent's log already has the first)."""
    fix = record.task if record.parent else ""
    steps = []
    if not record.parent and report.latest("assigned") is None:
        steps.append("assigned")
    if not any(s.step == "claimed" and s.detail.get("fix", "") == fix for s in report.stamps):
        steps.append("claimed")
    return steps


def _warn_unverified(who: dict[str, Any], env: StartEnv) -> None:
    """Say so when the hook did not vouch for the agent: the stamps then record `unverified`, `pr open` will refuse
    them and a rerun cannot rewrite them."""
    if who.get("source") != "hook":
        env.out(
            "warning: the hook did not vouch for who is running this, so the stamps will say source `unverified` "
            "and pr open will refuse them; start the task from a session started with coney-agent"
        )


def _stamp_start(
    record: UnitRecord,
    task: TaskFile,
    who: dict[str, Any],
    env: StartEnv,
    steps: list[str],
    commit: str,
    base_commit: str,
    adopted: bool = False,
) -> None:
    """Append the stamps `steps` names (from `_missing_steps`) to the log of `record`; `adopted` says in the `claimed`
    stamp that the branch was there before the unit."""
    fix = record.task if record.parent else ""
    entries: list[dict[str, Any]] = []
    if "assigned" in steps:
        entries.append(
            {
                "step": "assigned",
                "subject": "",
                "result": "pass",
                "commit": commit,
                "covers": {"task_sha256": front_sha256(task)},
                "detail": {
                    "issues": list(task.issues),
                    "kind": task.kind,
                    "claimant": task.claimant,
                    "claimed_at": iso_time(task.claimed_at),
                },
                "who": who,
            }
        )
    if "claimed" in steps:
        entries.append(
            {
                "step": "claimed",
                "subject": "",
                "result": "pass",
                "commit": commit,
                "covers": {"base": base_commit},
                "detail": {
                    "branch": record.branch,
                    "base_ref": record.base_ref,
                    "kind": task.kind,
                    "fix": fix,
                    **({"adopted": True} if adopted else {}),
                },
                "who": who,
            }
        )
    if entries:
        folder = env.scratch / record.log_task / "stamps"
        stamps.append_many(folder, rules=env.conventions.stamps, task=record.log_task, entries=entries, clock=env.now)


def _post_progress(record: UnitRecord, record_path: Path, env: StartEnv) -> None:
    """Post `/progress claimed` on each issue and remember it was done; a failure is a warning, the rerun retries."""
    failed = False
    for number in record.issues:
        try:
            env.remote.post_command(number, "/progress claimed")
        except ConfigError as error:
            failed = True
            env.out(f"warning: could not post /progress claimed on #{number} ({error}); run task start again to retry")
    if not failed:
        write_record(record_path, replace(record, progress_posted=True))


def pending_lines(env: StartEnv, task: TaskFile, record: UnitRecord | None, write: bool) -> list[str]:
    """What the start says about the pending work that overlaps the unit, and (with `write`) records in the task file.
    Reading it is never a reason to refuse or to fail a start: a fault becomes a note."""
    if env.overlap is None:
        return []
    try:
        return env.overlap(task, record, write)
    except (ConfigError, OSError, ValueError, subprocess.SubprocessError) as error:
        return [f"pending work that overlaps: not read ({str(error)[:120]})"]


def _summary(
    record: UnitRecord,
    steps: list[str],
    line: str,
    scratch: Path,
    headline: str,
    env: StartEnv,
    extra: Sequence[str] = (),
) -> None:
    """Print what the unit is and where, then `extra` lines (the pending work that overlaps)."""
    out = env.out
    out(f"task {record.task}: {headline}")
    if record.parent:
        out(f"  works in the unit of {record.parent} (branch {record.branch})")
    else:
        out(f"  branch    {record.branch} (from {record.base_ref} at {record.base_commit[:12]})")
    out(f"  worktree  {record.worktree}")
    out(f"  scratch   {scratch / record.task}")
    if steps:
        out(f"  stamps    {', '.join(steps)}")
    out(f"  {line}")
    for more in extra:
        out(f"  {more}")
    # The worktree is printed above; repeating the path here only made the output longer.
    out("next: work in the worktree; the stage commands find this unit from its branch")


def _resume(record: UnitRecord, record_path: Path, task: TaskFile, env: StartEnv, line: str, dry_run: bool) -> int:
    """A task that has a unit record: finish what a stopped start left (stamps, progress), never remake. With a dry
    run, only say what would be finished."""
    out = env.out
    if not record.worktree.is_dir():
        out(
            f"refused: the unit's worktree is gone ({record.worktree}); the task cannot be restarted: ask the "
            f"coordinator (the record is {record_path.as_posix()})"
        )
        return 1
    report, problem = _log_report(record.log_task, env)
    if problem is not None:
        out(f"refused: {problem}")
        return 1
    steps = _missing_steps(record, report)
    if dry_run:
        todo = [f"stamp {', '.join(steps)}"] if steps else []
        if not record.progress_posted:
            todo.append("post /progress claimed on each issue")
        what = "would " + " and ".join(todo) if todo else "nothing to do"
        out(f"dry run: task {record.task} is started already; {what}")
        return 0
    who = env.who()
    if steps:
        _warn_unverified(who, env)
        _stamp_start(record, task, who, env, steps, record.base_commit, record.base_commit)
    if not record.progress_posted:
        _post_progress(record, record_path, env)
    _summary(record, steps, line, env.scratch, "already started", env)
    return 0


def start_task(request: StartRequest, env: StartEnv) -> int:
    """Start a claimed task; 0 when it is started (or was), 1 when it was refused. ConfigError when it cannot run."""
    out = env.out
    conventions = env.conventions
    main = main_checkout(env.root)
    path = env.scratch / request.task / conventions.tasks.file
    if not path.is_file():
        out(f"refused: {request.task}: no task file at {path}; the coordinator writes it with `task create`")
        return 1
    task = parse_task_file(path.read_text(encoding="utf-8"), conventions.tasks)
    if task.task != request.task:
        out(f"refused: {path} is the task file of '{task.task}', not '{request.task}'")
        return 1
    if (request.adopt or request.adopt_branch is not None) and task.kind == "fix":
        out("refused: --adopt makes a unit of an ordinary task's branch; a fix task works in its parent's unit")
        return 1
    if task.kind == "fix" and not task.parent:
        out(
            "refused: the task file names no parent task, so there is no unit to work in; "
            f"recreate it with `task create --fix-of {task.issues[0]}`"
        )
        return 1
    user = env.remote.current_user()
    if task.claimant != user:
        out(f"refused: the task file belongs to {task.claimant}, and you are signed in as {user}")
        return 1
    engine = Engine(env.remote, conventions, env.now)
    problem = _claim_problem(task, user, task.parent or task.task, engine, env.remote)
    if problem is not None:
        out(f"refused: {problem}")
        return 1
    record_path = env.scratch / request.task / conventions.tasks.start.unit_file
    slot_line, slot_problem = _slot_check(task, env, engine)
    if record_path.is_file():
        return _resume(read_record(record_path), record_path, task, env, slot_line, request.dry_run)
    if not task.parent:
        problem = _limit_problem(task, user, engine)
        if problem is not None:
            out(f"refused: {problem}")
            return 1
    if slot_problem is not None:
        out(f"refused: {slot_problem}")
        return 1
    if task.parent:
        return _start_fix(request, env, task, user, record_path, slot_line)
    if request.adopt or request.adopt_branch is not None:
        # Imported here because task_start_adopt builds on this module's helpers.
        from coney_tools.task_start_adopt import start_adopted

        return start_adopted(request, env, main, task, user, record_path, slot_line)
    return _start_unit(request, env, main, task, user, record_path, slot_line)


def _start_unit(
    request: StartRequest, env: StartEnv, main: Path, task: TaskFile, user: str, record_path: Path, line: str
) -> int:
    """Make the branch, worktree, record, stamps and progress of an ordinary task."""
    out = env.out
    template = env.conventions.workspace.worktrees
    if "<branch>" not in template:
        raise ConfigError(f"workspace.worktrees ({template}) must contain <branch>")
    tree = (main / template.replace("<branch>", task.task)).resolve()
    if _git(main, "show-ref", "--verify", "--quiet", f"refs/heads/{task.task}").returncode == 0:
        out(
            f"refused: the branch '{task.task}' exists already; a task's branch is made once (rename or delete it, "
            "or pass --adopt to make a unit of a branch made before units existed)"
        )
        return 1
    if tree.exists():
        out(f"refused: {tree} exists already; a task's worktree is made once")
        return 1
    report, problem = _log_report(task.task, env)
    if problem is not None:
        out(f"refused: {problem}")
        return 1
    base_ref, base_commit = _fetch_and_resolve(request, env, main)
    record = UnitRecord(
        task=task.task,
        kind=task.kind,
        issues=task.issues,
        claimant=user,
        branch=task.task,
        worktree=tree,
        base_ref=base_ref,
        base_commit=base_commit,
        started=env.now(),
    )
    if request.dry_run:
        out(f"dry run: would make the branch '{task.task}' from {base_ref} ({base_commit[:12]}) in {tree}")
        out(f"dry run: would write {record_path}, stamp assigned and claimed, and post /progress claimed on each issue")
        out(f"dry run: {line}")
        for more in pending_lines(env, task, None, False):
            out(f"dry run: {more}")
        return 0
    done = _git(main, "worktree", "add", "-b", task.task, str(tree), base_commit)
    if done.returncode != 0:
        raise ConfigError(f"git worktree add {tree} failed: {done.stderr.strip() or 'no output'}")
    write_record(record_path, record)
    who = env.who()
    steps = _missing_steps(record, report)
    _warn_unverified(who, env)
    _stamp_start(record, task, who, env, steps, base_commit, base_commit)
    _post_progress(record, record_path, env)
    _summary(record, steps, line, env.scratch, "started", env, pending_lines(env, task, record, True))
    return 0


def _start_fix(request: StartRequest, env: StartEnv, task: TaskFile, user: str, record_path: Path, line: str) -> int:
    """Open the parent's unit for a fix task: a record that points at it, and a `claimed` stamp in its log."""
    out = env.out
    parent_path = env.scratch / task.parent / env.conventions.tasks.start.unit_file
    if not parent_path.is_file():
        out(f"refused: no unit of task '{task.parent}': it was never started (`task start {task.parent}`)")
        return 1
    parent = read_record(parent_path)
    if not parent.worktree.is_dir():
        out(f"refused: the worktree is gone ({parent.worktree}), so the unit of '{task.parent}' cannot be fixed")
        return 1
    report, problem = _log_report(parent.log_task, env)
    if problem is not None:
        out(f"refused: {problem}")
        return 1
    if report.latest("pr-opened", result="pass") is None:
        out(
            f"refused: the unit of '{task.parent}' was never released (its log has no pr-opened stamp), "
            "so there is no pull request to fix"
        )
        return 1
    record = UnitRecord(
        task=task.task,
        kind="fix",
        issues=task.issues,
        claimant=user,
        branch=parent.branch,
        worktree=parent.worktree,
        base_ref=parent.base_ref,
        base_commit=parent.base_commit,
        started=env.now(),
        parent=task.parent,
        progress_posted=True,
    )
    if request.dry_run:
        out(f"dry run: would open the unit of '{task.parent}' (branch {parent.branch}) for the fix and stamp claimed")
        out(f"dry run: {line}")
        return 0
    write_record(record_path, record)
    who = env.who()
    steps = _missing_steps(record, report)
    _warn_unverified(who, env)
    _stamp_start(record, task, who, env, steps, head_commit(parent.worktree), parent.base_commit)
    _summary(record, steps, line, env.scratch, "started", env, pending_lines(env, task, record, True))
    return 0
