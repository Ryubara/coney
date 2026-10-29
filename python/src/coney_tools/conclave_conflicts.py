# SPDX-License-Identifier: GPL-3.0-or-later
"""Conclave's conflict events: a pull request's conflict becomes work (docs/guides/conclave.md#conflicts).

A conflict used to be one `pr-stuck` wake, routed to the author, an agent that has usually finished; a waiver then
silenced it for as long as it was renewed, and only one fix runs at a time, so reviewed and green pull requests sat
open for days. This module turns each conflict of a unit with a worktree into the next step, in this order:

1. **Try the rebase** (`conflict_work`): for an open pull request in conflict, and for a unit ready to be released
   that `main` has moved past, rebase in a throwaway worktree. Clean, with the stamps still covering the change: the
   event is `unit-ready`, and the coordinator runs `pr open` (the pull request is updated; no engineer is needed).
2. **Make the fix task**: a real conflict, or stamps the rebase made stale, is one `conflict-fix` event whose line
   carries the ready command (`task create --fix-of <issue>` for a released unit, the unit's own task for one that was
   never released, the way to adopt the branch for one that predates units) and a brief. Oldest first: the fix track
   has one place (`tasks.start.slots.tracks.fix.max`), and a conflict that must wait says which fix it waits behind.
3. **No silent parking**: a hold with `--who` handles a conflict; a waiver does so only when its note names a
   maintainer decision (`conclave.watch.conflict_waiver`), and a waived conflict older than
   `conclave.watch.conflict_retell_hours` is raised again every that many hours.

A conflict whose branch has no worktree here (an outside contributor's), or whose trial could not be made, stays what it
was: a `pr-stuck` event for the coordinator. Everything here is read-only apart from the throwaway worktree and the
ledger in the scratch directory (`conflict_work`).
"""

from __future__ import annotations

from dataclasses import dataclass, field
from datetime import datetime
from pathlib import Path
from typing import Any

from coney_tools import conflict_work, stamps, task_slots
from coney_tools.conclave_sources import Sources, event_row, iso_utc
from coney_tools.config import ConfigError
from coney_tools.conflict_work import Ledger, Trial
from coney_tools.conventions import ConclaveWatch
from coney_tools.pr_hold import Hold, HoldsRead, covers_conflict
from coney_tools.unit import Unit

#: The cause of a pull request that conflicts with its base (`pr list-stuck`).
CAUSE = "conflict"
#: How the work on a conflict starts: a fix task on a released unit, the unit's own task, or an adopted branch.
FIX, OWN, ADOPT = "fix", "own", "adopt"


@dataclass
class Item:
    """One conflict that needs an engineer, with what the line about it says."""

    #: The ledger key and the id part: the pull request's number, or `unit-<branch>` for a unit not yet released.
    key: str
    pr: int | None
    branch: str
    tree: Path
    #: The head the conflict was first seen on (seven digits).
    head: str
    since: datetime
    trial: Trial
    #: The unit's task folder and the issues its task file names.
    task: str
    issues: tuple[int, ...]
    route: str
    #: Set by `_queue`: whether the fix place is free for it, and what it waits behind otherwise.
    go: bool = True
    behind: str = ""
    #: Set by `_queue`: the fix task already made for one of the item's issues (queued or running), if any.
    own_fix: str = ""


@dataclass
class Found:
    """What `conflict_events` hands back: its events and the `unit-ready` events of the queue after its changes."""

    events: list[dict[str, Any]] = field(default_factory=list)
    ready: list[dict[str, Any]] = field(default_factory=list)


def _heads(prs: dict[str, Any]) -> dict[int, dict[str, Any]]:
    """The open pull requests by number."""
    return {int(row["number"]): row for row in prs.get("prs", [])}


def _item(
    src: Sources,
    units: dict[str, dict[str, Any]],
    key: str,
    number: int | None,
    branch: str,
    tree: Path,
    head: str,
    since: datetime,
    trial: Trial,
) -> Item:
    """The item of a branch with its unit's task and issues (if it has a task file) and the route to it."""
    folder = src.coord.scratch / branch.replace("/", "-")
    issues = tuple(int(n) for n in (units.get(folder.name) or {}).get("issues", []))
    return Item(key, number, branch, tree, head, since, trial, folder.name, issues, _release_route(folder, src))


def _hold_for(holds: HoldsRead, number: int, now: datetime, head: str, rules: ConclaveWatch) -> Hold | None:
    """The running hold that covers this pull request's conflict on this head: a waiver only with a decision."""
    for held in holds.holds:
        if held.pr == number and held.covers(CAUSE, now, head) and covers_conflict(held, rules):
            return held
    return None


def _hold_text(held: Hold) -> str:
    """Who holds a problem and until when, as an event says it."""
    return f"{held.who} until {iso_utc(held.until)}"


def _stuck_event(
    number: int, branch: str, head: str, detail: str, since: datetime, guard: bool, handled_by: str | None
) -> dict[str, Any]:
    """A conflict as it was told before conflicts became work: a `pr-stuck` event, the coordinator's to handle."""
    return event_row(
        f"pr:{number}:{CAUSE}:{head}",
        "pr-stuck",
        f"#{number}{' ' + branch if branch else ''}: {CAUSE}{' ' + detail if detail else ''}",
        f"fix it or start an agent for it, then record it: coney-tools pr hold {number} --who <agent> "
        f"--cause {CAUSE} (a known wait needs a note that names a maintainer decision)",
        iso_utc(since),
        {"pr": number, "task": None, "branch": branch or None},
        guard=guard,
        handled_by=handled_by,
    )


def _release_route(folder: Path, src: Sources) -> str:
    """How an engineer gets to the branch: `fix` when its unit was released (a fix task works in it), `own` when the
    unit exists but never was, `adopt` when the branch has no unit record at all (it predates `task start`)."""
    conventions = src.conventions
    if not (folder / conventions.tasks.start.unit_file).is_file():
        return ADOPT
    report = stamps.verify(folder / "stamps", conventions.stamps)
    return FIX if report.latest("pr-opened", result="pass") is not None else OWN


def _command(item: Item, src: Sources) -> str:
    """The ready command for the item's route, and nothing else."""
    issue = item.issues[0] if item.issues else None
    if item.route == FIX and issue is not None:
        return f"coney-tools task create --fix-of {issue}"
    if item.route == ADOPT:
        return (
            f"coney-tools task create {issue if issue is not None else '<issue>'} --name {item.branch}; the engineer "
            f"then runs coney-tools task start {item.branch} --adopt (the branch predates units)"
        )
    return f"start the engineer of the unit's own task {item.task} (coney-tools task start {item.task} resumes it)"


def _brief(item: Item, upstream: str) -> str:
    """The brief line: what the engineer does, and how the coordinator records that somebody does."""
    trial = item.trial
    if trial.status == conflict_work.CONFLICT:
        work = f"rebase {item.branch} onto {upstream} in {item.tree} and resolve {trial.paths_text()}"
    else:
        work = f"rebase {item.branch} onto {upstream} in {item.tree} (clean) and redo the stamps it made stale"
    after = f"then pr open {item.branch}"
    if item.pr is not None:
        after += f"; record the fix: pr hold {item.pr} --who <agent> --cause {CAUSE}"
    return f"brief: {work}, {after}"


def _fix_event(item: Item, src: Sources, upstream: str) -> dict[str, Any]:
    """The `conflict-fix` event of one item: the ready command and the brief, or where it waits."""
    trial = item.trial
    who = f"#{item.pr} {item.branch}" if item.pr is not None else f"{item.branch} (not released)"
    if trial.status == conflict_work.CONFLICT:
        text = f"{who}: conflicts with {upstream} in {trial.paths_text()}"
    else:
        have = "it has no stamps that cover it" if item.route == ADOPT else "the stamps no longer cover it"
        text = f"{who}: rebases clean onto {upstream} but {have} ({trial.reason})"
    do = f"{_command(item, src)}; {_brief(item, upstream)}"
    if not item.go:
        do = f"waits behind {item.behind}; when it frees: {do}"
    if item.own_fix:
        # the fix exists (made since the event was told, before a hold was recorded or after it ran out): making it
        # again would be a second fix task that takes the track's one place, so the line only asks to record it
        do = f"the fix task {item.own_fix} exists, do not create another"
        do += (
            f"; record it: coney-tools pr hold {item.pr} --who <agent> --cause {CAUSE}"
            if item.pr is not None
            else "; its engineer works the branch"
        )
    state = "fixing" if item.own_fix else "go" if item.go else "wait"
    return event_row(
        f"conflict-fix:{item.key}:{item.head}:{state}",
        "conflict-fix",
        text,
        do,
        iso_utc(item.since),
        {"pr": item.pr, "task": item.task if item.route != ADOPT else None, "branch": item.branch},
        guard=item.pr is not None and (item.go or bool(item.own_fix)),
    )


def _queue(items: list[Item], src: Sources, now: datetime) -> None:
    """Order the open items oldest first and say which may go at once and which wait behind which fix.

    The line is the fixes already running in the fix track, the fix tasks made and not started, then the conflicts in
    order; a place frees as the line ahead of it moves, so the item at position p waits behind the one at p minus the
    track's `max`. A fix task for one of the item's own issues is not ahead of it: it is the fix the item asks for
    (made since the event was told, before a hold was recorded or after it ran out), and waiting behind it would send
    the coordinator to make that fix twice. A track with no `max` never makes anyone wait."""
    items.sort(key=lambda item: (item.since, item.pr if item.pr is not None else 10**9, item.branch))
    conventions = src.conventions
    try:
        current = task_slots.scan(src.coord.scratch, conventions, now)
        names = [holder.task for holder in current.held("fix")] + list(current.queued_fixes)
        line = [(name, task_slots.issues_of(src.coord.scratch / name, conventions)) for name in names]
    except (OSError, ConfigError):
        line = []
    maximum = conventions.tasks.start.slots.tracks.of("fix").max
    for item in items:
        item.own_fix = next((name for name, issues in line if issues & set(item.issues)), "")
        ahead = [name for name, issues in line if not (issues & set(item.issues))]
        position = len(ahead)
        name = f"#{item.pr}'s fix" if item.pr is not None else f"{item.task}'s fix"
        if maximum is not None and position >= maximum:
            item.go = False
            item.behind = ahead[position - maximum]
        # a later item is behind this one whatever its issues: only a real fix task is matched by issue
        line.append((name, frozenset()))


def _text_file(release: dict[str, Any] | None, folder: Path) -> Path:
    """The description file of a unit: the release queue's file for its folder, else `PR.md` in it."""
    for row in (release or {}).get("units", []):
        if str(row.get("branch")) == folder.name and row.get("file") == "PR.md":
            return Path(str(row["path"]))
    return folder / "PR.md"


def _ready_event(branch: str, number: int, name: str, change: str, upstream: str, since: datetime) -> dict[str, Any]:
    """A conflicting pull request whose branch rebases clean and whose stamps still cover it is ready for `pr open`."""
    return event_row(
        f"unit:{branch}:{name}:{change[:12]}",
        "unit-ready",
        f"{branch}: #{number} conflicts with {upstream}; it rebases clean and the stamps still cover it, "
        "ready for pr open",
        f"coney-tools pr open {branch}",
        iso_utc(since),
        {"pr": number, "task": branch, "branch": branch},
    )


def conflict_events(
    src: Sources,
    report: dict[str, Any],
    release: dict[str, Any] | None,
    holds: HoldsRead,
    ready: list[dict[str, Any]],
    unread: set[str],
) -> Found:
    """The events for every conflicting pull request and for every ready-to-release unit `main` has moved past.

    `ready` is the `unit-ready` events of the release queue; the returned `Found.ready` is the same list, minus the
    units whose rebase turned out not to be clean (they are `conflict-fix` events instead)."""
    prs = report.get("prs")
    if not isinstance(prs, dict) or prs.get("error"):
        return Found([], ready)
    conventions = src.conventions
    rules = conventions.conclave.watch
    now = src.coord.now()
    upstream = f"origin/{conventions.repo.default_branch}"
    ledger = Ledger(src.coord.scratch / rules.conflicts_dir)
    try:
        trees = {branch: path for path, branch in src.coord.worktrees() if branch}
    except (ConfigError, OSError):
        trees = {}
        unread.add("conflict-fix")
    rows = _heads(prs)
    units = {str(row["task"]): row for row in (report.get("units") or {}).get("units", [])}
    found = Found()
    items: list[Item] = []
    keys: set[str] = set()

    for stuck in (prs.get("health") or {}).get("stuck", []):
        if stuck.get("cause") != CAUSE:
            continue
        number = int(stuck["number"])
        row = rows.get(number, {})
        branch = str(stuck.get("branch") or row.get("branch") or "")
        head = str(row.get("head") or "unknown")[:7]
        key = str(number)
        keys.add(key)
        since = ledger.first_seen(key, head, now)
        detail = str(stuck.get("detail") or "")
        held = _hold_for(holds, number, now, head, rules)
        age_hours = (now - since).total_seconds() / 3600
        if held is not None and held.waived and age_hours >= rules.conflict_retell_hours:
            found.events.append(_waiver_reminder(number, branch, head, since, age_hours, held, rules))
            continue
        if held is not None:
            found.events.append(_stuck_event(number, branch, head, detail, since, True, _hold_text(held)))
            continue
        if branch not in trees:
            found.events.append(_stuck_event(number, branch, head, detail, since, True, None))
            continue
        folder = src.coord.scratch / branch.replace("/", "-")
        trial = src.rebase_trial(
            trees[branch], Unit(folder.name, folder), _text_file(release, folder), key, head, now, ledger
        )
        if trial.status == conflict_work.READY:
            if not (folder / rules.unit_hold_file).is_file():
                found.events.append(_ready_event(branch, number, "PR.md", trial.change, upstream, since))
        elif trial.status in (conflict_work.STALE, conflict_work.CONFLICT):
            items.append(_item(src, units, key, number, branch, trees[branch], head, since, trial))
        else:
            found.events.append(_stuck_event(number, branch, head, detail, since, True, None))
    found.ready = _moved_units(src, ready, trees, units, release, ledger, items, keys, upstream, now)
    _queue(items, src, now)
    found.events += [_fix_event(item, src, upstream) for item in items]
    # A pull request whose mergeability GitHub is still computing (after every merge) may be in conflict still: its
    # file stays, or the conflict would look new, and a waived one never a day old.
    keys.update(str(number) for number, row in rows.items() if str(row.get("mergeable") or "").upper() != "MERGEABLE")
    ledger.forget_except(keys)
    return found


def _waiver_reminder(
    number: int, branch: str, head: str, since: datetime, age_hours: float, held: Hold, rules: ConclaveWatch
) -> dict[str, Any]:
    """A waived conflict that is a day old (or more) is told again: one event for each `conflict_retell_hours` of its
    age, open but never a guard, because the decision it waits for is not the coordinator's to make."""
    periods = int(age_hours // rules.conflict_retell_hours)
    days = f"{age_hours / 24:.1f} days" if age_hours >= 48 else f"{age_hours:.0f} hours"
    return event_row(
        f"pr:{number}:{CAUSE}:{head}:waived-{periods}",
        "pr-stuck",
        f"#{number} {branch}: conflict waived for {days}: {held.note}",
        "the waiver names a maintainer decision; ask for it again, or start the fix (task create --fix-of <issue>) "
        f"and end the waiver: coney-tools pr hold {number} --release",
        iso_utc(since),
        {"pr": number, "task": None, "branch": branch or None},
    )


def _moved_units(
    src: Sources,
    ready: list[dict[str, Any]],
    trees: dict[str, Path],
    units: dict[str, dict[str, Any]],
    release: dict[str, Any] | None,
    ledger: Ledger,
    items: list[Item],
    keys: set[str],
    upstream: str,
    now: datetime,
) -> list[dict[str, Any]]:
    """The `unit-ready` events that stay: those of units whose rebase onto `main` is clean and whose stamps hold.

    A ready unit `main` has moved past is tried like a conflicting pull request: `pr open` rebases it, and a conflict
    or a stale stamp would only show there. Such a unit becomes a `conflict-fix` item for its own task instead. A
    trial that could not be made leaves the event as it was."""
    kept: list[dict[str, Any]] = []
    for event in ready:
        branch = str(event["ref"].get("branch") or "")
        if branch not in trees:
            kept.append(event)
            continue
        folder = src.coord.scratch / branch.replace("/", "-")
        key = f"unit-{branch}"
        head = src.tip_of(trees[branch])[:7] or "unknown"
        trial = src.rebase_trial(
            trees[branch], Unit(folder.name, folder), _text_file(release, folder), key, head, now, ledger
        )
        if trial.status in (conflict_work.STALE, conflict_work.CONFLICT):
            keys.add(key)
            since = ledger.first_seen(key, head, now)
            items.append(_item(src, units, key, None, branch, trees[branch], head, since, trial))
        else:
            if trial.status != conflict_work.UNKNOWN:
                keys.add(key)
            kept.append(event)
    return kept
