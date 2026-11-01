# SPDX-License-Identifier: GPL-3.0-or-later
"""Keeping the engineer places full: `task slots --fill-check` (docs/guides/slot-fill.md).

The places (`tasks.start.slots`: a machine's build slots times `per_build_slot`, shared by the tracks) are a ceiling,
and the maintainer's rule of 2026-10-27 is that they are also a floor: a free place with startable work and no
reason to leave it free is a failure the coordinator may not end a turn on. This module decides that. It reads what
holds the places (`task_slots.scan`), the work that could fill them and the reasons that excuse a gap, and answers with
the text the Stop hook (`.claude/hooks/slot_fill_guard.py`) shows the coordinator.

A free place is one of four kinds. The places a track keeps (`tracks.<name>.min`) while it runs fewer are *kept*: the
reimplementation track keeps one for game (`feature`) work, the fix track one for a failed pull request, the
networking track one for a networking issue, the renderer track one for a renderer issue, and Studio may not take
them. The rest of the machine's free places are *open*: Coney Studio takes them, or game work beyond the kept one;
networking and renderer work never do, for their tracks hold at most `max` units. *Startable work* is a claimed task
whose agent has not started, then the ready issues that pass the start gate (`tasks.start.fill.gate`, plus
`P-critical`, which comes first as the claim Action demands) and are not blocked and fit the open-claims limit (a
Studio, networking, renderer or `workflow` issue is outside it); for the fix track, a queued fix task, then each open
pull request of this account with a failed required check that no fix unit and no hold covers. *Reasons* the tool
finds itself: no issue passes the gate, the claim limit is reached, a watched drive's free disk is under its floor
(watched_drives.py: the workspace's, the build root's and the `watch_paths` of `coney.local.toml`).
Anything else is a waiver (`slot_waivers.py`).

GitHub is read through `Sources` (`slot_fill_github.py` is the real one), so the decision is pure and the tests never
touch the network. A GitHub that cannot be read passes the check with a warning: a guard that cannot judge blocks
nothing.
"""

from __future__ import annotations

from collections.abc import Collection, Iterable, Sequence
from dataclasses import dataclass
from datetime import datetime
from pathlib import Path
from typing import Protocol

from coney_tools import slot_fill_places as places
from coney_tools import slot_waivers, task_slots
from coney_tools.config import ConfigError
from coney_tools.conventions import Conventions
from coney_tools.issue_claim_model import Card
from coney_tools.issue_claim_rules import limit_for, open_task_keys
from coney_tools.pr_hold import Hold
from coney_tools.task_create import kind_of_labels
from coney_tools.task_file import TaskFile
from coney_tools.unit_record import UnitRecord
from coney_tools.watched_drives import Drive, gb_text

#: What the message of a failing check starts with: the Stop hook blocks only on exit code 1 with this first word, so a
#: crash (which also exits with 1) can never block a turn.
BLOCKED = "Blocked:"
#: The longest an issue's title is shown.
TITLE_WIDTH = 80


@dataclass(frozen=True)
class IssueInfo:
    """An open issue as the check needs it."""

    number: int
    title: str
    labels: tuple[str, ...]
    assignees: tuple[str, ...] = ()
    #: The issue text, for its `Blocked by` section.
    body: str = ""


@dataclass(frozen=True)
class FailedPull:
    """An open pull request of this account with a failed required check."""

    number: int
    branch: str
    #: The required checks that failed.
    checks: tuple[str, ...]
    #: The issues the pull request closes (`Fixes #N`): how a fix task is matched to it.
    closes: tuple[int, ...]
    #: The head commit, so a hold made on an older head does not cover a push made since.
    head: str = ""


class Sources(Protocol):
    """What the check reads from GitHub; each method raises `ConfigError` when GitHub cannot be read."""

    def ready(self) -> list[IssueInfo]:
        """The open issues with the claimable status label."""
        ...

    def claimed(self) -> list[Card]:
        """The held claim cards of the open issues assigned to this account: the cards the claim Action counts."""
        ...

    def blockers(self, issue: IssueInfo) -> list[int]:
        """The issues `issue` is blocked by that are not finished."""
        ...

    def failed_pulls(self) -> list[FailedPull]:
        """This account's open, non-draft pull requests with a failed required check."""
        ...

    def pulls_over(self, issues: Collection[int]) -> bool:
        """Whether the pull request that closes one of `issues` is merged or closed (False when GitHub cannot say):
        a fix task for it has nothing left to fix."""
        ...

    def closed(self, numbers: Collection[int]) -> set[int]:
        """Which of `numbers` (issues or pull requests) are closed or merged: a waiver that names them all ends."""
        ...


@dataclass(frozen=True)
class Check:
    """The answer of the check."""

    #: Whether a slot is free, work can be started and nothing excuses it: the check fails (exit code 1).
    blocked: bool
    #: What to print: a refusal that starts with `Blocked:`, else a note on why the check passes.
    message: str
    #: Things the check could not look at (GitHub could not be read): printed as `warning:` lines.
    warnings: tuple[str, ...] = ()


@dataclass(frozen=True)
class Analysis:
    """The ready issues sorted into those that can start and the reasons for the others."""

    eligible: tuple[IssueInfo, ...]
    reasons: tuple[str, ...]
    warnings: tuple[str, ...] = ()


def _short(title: str) -> str:
    """A title cut to `TITLE_WIDTH` characters."""
    return title if len(title) <= TITLE_WIDTH else title[: TITLE_WIDTH - 1].rstrip() + "..."


def _priority(issue: IssueInfo, order: tuple[str, ...]) -> tuple[int, int]:
    """The sort key of an issue: its place in `priority_order` (unlabelled last), then its number."""
    place = next((index for index, label in enumerate(order) if label in issue.labels), len(order))
    return place, issue.number


def _priority_label(issue: IssueInfo, order: tuple[str, ...]) -> str:
    """The priority label of an issue, or `no priority`."""
    return next((label for label in order if label in issue.labels), "no priority")


def passes_gate(issue: IssueInfo, conventions: Conventions) -> bool:
    """Whether the start gate lets the issue start: it carries a gate label, is a listed issue or is `P-critical`; with
    the gate off, every issue."""
    gate = conventions.tasks.start.fill.gate
    if not gate.enabled:
        return True
    return (
        conventions.claims.critical_label in issue.labels
        or issue.number in gate.issues
        or any(label in issue.labels for label in gate.labels)
    )


def _gate_words(conventions: Conventions) -> str:
    """The gate as a phrase for the reasons."""
    gate = conventions.tasks.start.fill.gate
    parts = [f"labelled {', '.join(gate.labels)}"] if gate.labels else []
    parts += [", ".join(f"#{n}" for n in gate.issues)] if gate.issues else []
    parts.append(conventions.claims.critical_label)
    return " and ".join(", ".join(parts).rsplit(", ", 1)) if len(parts) > 1 else parts[0]


def counted_claims(cards: list[Card]) -> list[str]:
    """The keys of the open claims that use up the account's limit, from the held claim cards.

    The test is the claim Action's own (`issue_claim_rules.open_task_keys`: an exempt claim, a Studio one among them,
    and one whose place is free (`Card.counted`: its review passed, or its pull request is ready) do not count; the
    cards of one task count once), so the tool and the Action cannot disagree. A claim paused by `S-on-hold`,
    `S-blocked` or `S-needs-info` still counts."""
    keys: set[str] = set()
    for claimant in {card.claimant for card in cards}:
        keys |= open_task_keys(cards, claimant, 0)
    return sorted(keys)


def holds_no_place(labels: Iterable[str], conventions: Conventions) -> bool:
    """Whether a unit on an issue with these labels holds no place (`exempt_workflow` is on and the issue carries
    `claims.exempt_label`), so starting one never fills a free place and is not work for the check."""
    return conventions.tasks.start.slots.exempt_workflow and conventions.claims.exempt_label in labels


def is_exempt(issue: IssueInfo, conventions: Conventions) -> bool:
    """Whether the issue's claim is outside the open-claims limit: it carries `claims.exempt_label` (`workflow`) or
    `claims.studio_label` (Coney Studio)."""
    return conventions.claims.outside_limit(issue.labels)


def analyse_issues(
    ready: list[IssueInfo],
    counted: list[str],
    conventions: Conventions,
    sources: Sources,
    want: int,
    want_game: int = 0,
    want_networking: int = 0,
    want_renderer: int = 0,
) -> Analysis:
    """Which ready issues can start now, at least `want` of them when there are so many, most urgent first, and why
    the others cannot.

    `want` counts the issues that are neither networking nor renderer work. The walk goes on past it until `want_game`
    of the eligible issues are game work, `want_networking` are networking work and `want_renderer` are renderer work,
    so an issue of another track that outranks them cannot hide them from the place kept for game work or the ones
    kept for networking and the renderer.

    The order is the claim Action's: while a `P-critical` issue is ready and unclaimed only it can be claimed, so
    the others are not startable; with none, the issues that pass the start gate, by priority then number. A blocked
    issue and one past the open-claims limit are skipped (an issue with `claims.exempt_label`, `claims.studio_label`,
    `claims.networking_label` or `claims.renderer_label` is outside the limit)."""
    fill = conventions.tasks.start.fill
    rules = conventions.claims
    reasons: list[str] = []
    warnings: list[str] = []
    free = [issue for issue in ready if not issue.assignees]
    if not free:
        return Analysis((), ("no issue is ready to start (nothing carries " + rules.claimable_status + ")",))
    critical = [issue for issue in free if rules.critical_label in issue.labels]
    pool = critical or [issue for issue in free if passes_gate(issue, conventions)]
    if not pool:
        reasons.append(
            f"the start gate leaves none of the {len(free)} ready issue(s): only issues {_gate_words(conventions)} "
            "may start now (tasks.start.fill.gate in .github/conventions.yml)"
        )
        return Analysis((), tuple(reasons))
    limit = limit_for(True, rules)
    outside = " or ".join(
        filter(None, (rules.exempt_label, rules.studio_label, rules.networking_label, rules.renderer_label))
    )
    room = limit - len(counted)
    eligible: list[IssueInfo] = []
    blocked: list[str] = []
    limited: list[IssueInfo] = []
    placeless: list[IssueInfo] = []
    game_found = 0
    networking_found = 0
    renderer_found = 0
    #: The issues whose blockers could not be read, by the reason: one warning for each, not one for each issue.
    unread: dict[str, list[int]] = {}
    for issue in sorted(pool, key=lambda item: _priority(item, fill.priority_order)):
        if (
            len(eligible) - networking_found - renderer_found >= want
            and game_found >= want_game
            and networking_found >= want_networking
            and renderer_found >= want_renderer
        ):
            break
        if holds_no_place(issue.labels, conventions):
            placeless.append(issue)  # its unit would hold no place, so it would not fill the free one
            continue
        try:
            open_blockers = sources.blockers(issue)
        except ConfigError as error:
            open_blockers = []
            unread.setdefault(str(error), []).append(issue.number)
        if open_blockers:
            blocked.append(f"#{issue.number} (by {', '.join(f'#{n}' for n in open_blockers)})")
            continue
        exempt = is_exempt(issue, conventions)
        if not exempt and room <= 0:
            limited.append(issue)
            continue
        eligible.append(issue)
        kind = kind_of_labels(issue.labels)
        track = issue_track(issue, kind, conventions)
        game_found += places.fills_game_place(kind, track, conventions)
        networking_found += track == "networking"
        renderer_found += track == "renderer"
        if not exempt:
            room -= 1
    for reason, numbers in unread.items():
        names = ", ".join(f"#{number}" for number in numbers)
        verb = "is" if len(numbers) == 1 else "are"
        warnings.append(f"whether {names} {verb} blocked was not read ({reason})")
    # The reasons are kept when some issue can start too: a place that cannot take it (the kept game place and a
    # Studio issue) is then left free for these.
    if blocked:
        which = "every issue that may start" if not eligible else "some issues"
        reasons.append(f"{which} {'is' if not eligible else 'are'} blocked by an unfinished one: {', '.join(blocked)}")
    if placeless:
        names = ", ".join(f"#{issue.number}" for issue in placeless[:6])
        reasons.append(
            f"{names} carr{'ies' if len(placeless) == 1 else 'y'} {rules.exempt_label}, whose units hold no place "
            "(tasks.start.slots.exempt_workflow), so starting them would not fill one"
        )
    if limited:
        names = ", ".join(f"#{issue.number}" for issue in limited[:6])
        reasons.append(
            f"the open-claims limit is reached ({len(counted)} of {limit}: {', '.join(counted)}), so "
            f"{names} cannot be claimed until a task is finished, given back or has its pull request open "
            f"(an issue labelled {outside} is outside the limit)"
        )
    return Analysis(tuple(eligible), tuple(reasons), tuple(warnings))


def waiting_tasks(
    scratch: Path, conventions: Conventions, current: task_slots.Scan, claimed: set[int], now: datetime
) -> list[TaskFile]:
    """The tasks that were claimed and have no agent: a task file with no unit record and no live reservation, not a
    fix, every issue of which still carries this account's claim label. Oldest first."""
    busy = {holder.task for holder in current.holders} | set(current.exempt)
    found: list[TaskFile] = []
    if not scratch.is_dir():
        return found
    for folder in sorted(scratch.iterdir()):
        if not folder.is_dir() or folder.name in busy or (folder / conventions.tasks.start.unit_file).exists():
            continue
        task = task_slots.read_task(scratch, folder.name, conventions)
        if isinstance(task, str) or task.kind == "fix" or not all(number in claimed for number in task.issues):
            continue
        found.append(task)
    return sorted(found, key=lambda item: (item.claimed_at, item.task))


def _fix_issues(scratch: Path, conventions: Conventions, names: list[str]) -> set[int]:
    """The issues the fix tasks `names` work on."""
    numbers: set[int] = set()
    for name in names:
        task = task_slots.read_task(scratch, name, conventions)
        if not isinstance(task, str):
            numbers.update(task.issues)
    return numbers


def uncovered_pulls(
    pulls: list[FailedPull], fix_issues: set[int], holds: list[Hold], now: datetime
) -> list[FailedPull]:
    """The failed pull requests that no fix task (by the issues it closes) and no running hold covers."""
    return [
        pull
        for pull in pulls
        if not fix_issues.intersection(pull.closes)
        and not any(held.pr == pull.number and held.covers("checks-failed", now, pull.head or None) for held in holds)
    ]


def _unit_labels(cards: list[Card] | None, conventions: Conventions) -> task_slots.LabelsOf:
    """`task_slots.scan`'s view of a live unit's labels, from the held claim cards read for this check: its issues'
    `workflow`, `studio`, `networking` and `renderer` flags. With no cards (GitHub was not read) every unit counts as
    reimplementation or fix."""
    by_issue = {card.issue: card for card in cards or []}

    def labels(record: UnitRecord) -> task_slots.Labels | None:
        """A live unit's labels: workflow (no place, while `exempt_workflow` is on), Studio, networking and renderer."""
        if cards is None:
            return None
        found = [by_issue[number] for number in record.issues if number in by_issue]
        workflow = conventions.tasks.start.slots.exempt_workflow and any(card.in_workflow for card in found)
        return task_slots.Labels(
            workflow=workflow,
            studio=any(card.studio for card in found),
            networking=any(card.networking for card in found),
            renderer=any(card.renderer for card in found),
        )

    return labels


def waiver_hint(conventions: Conventions) -> str:
    """The command that waives a gap, with the reason codes of the conventions."""
    codes = "|".join(code.name for code in conventions.tasks.start.fill.waivers)
    return f'coney-tools task slots --waive <places> --reason <{codes}> --hours <hours> --note "<why>" [--issues N,M]'


def issue_track(issue: IssueInfo, kind: str, conventions: Conventions) -> str:
    """The track a unit on this ready issue would work in, by the rule of `task_slots.track_of` (the networking label
    first, then the renderer one, when an issue has several)."""
    rules = conventions.claims
    return task_slots.track_of(
        kind, rules.is_studio(issue.labels), rules.is_networking(issue.labels), rules.is_renderer(issue.labels)
    )


def _game_work(
    task: TaskFile, studio_issues: set[int], networking_issues: set[int], renderer_issues: set[int]
) -> places.Work:
    """A claimed task with no agent as work to start."""
    return places.Work(
        f"claimed, not started: {task.task} ({task.kind}); start its agent with a line `Task id: {task.task}`",
        track=task_slots.track_of(
            task.kind,
            any(number in studio_issues for number in task.issues),
            any(number in networking_issues for number in task.issues),
            any(number in renderer_issues for number in task.issues),
        ),
        kind=task.kind,
    )


def _issue_work(issue: IssueInfo, conventions: Conventions) -> places.Work:
    """A ready issue that may start, as work to start."""
    fill = conventions.tasks.start.fill
    kind = kind_of_labels(issue.labels)
    track = issue_track(issue, kind, conventions)
    kept = {"studio": "Studio track", "networking": "networking track", "renderer": "renderer track"}.get(
        track, "game work"
    )
    return places.Work(
        f"#{issue.number} [{_priority_label(issue, fill.priority_order)}] {_short(issue.title)} ({kept}); "
        f"`task create {issue.number}`, then start its agent",
        track=track,
        kind=kind,
    )


def fill_check(
    scratch: Path,
    conventions: Conventions,
    now: datetime,
    sources: Sources,
    build_slots: int,
    drives: Sequence[Drive] | None = None,
    holds: list[Hold] | None = None,
) -> Check:
    """Decide whether a free place has startable work and no reason to stay free.

    `build_slots` is the machine's (`coney.local.toml`); `drives` are the watched drives with their free space (None:
    not known, so no reason). Reads the places from the scratch folder, the work from `sources`; a GitHub that cannot be
    read passes the check with a warning."""
    fill = conventions.tasks.start.fill
    warnings: list[str] = []
    try:
        mine: list[Card] | None = sources.claimed()
    except ConfigError as error:
        mine = None
        warnings.append(f"GitHub could not be read ({error}), so the places were not checked against the work")
    # A fix task whose pull request merged or closed leaves the queue, but only when GitHub was read.
    current = task_slots.scan(
        scratch,
        conventions,
        now,
        labels=_unit_labels(mine, conventions),
        parent_over=sources.pulls_over if mine is not None else None,
    )
    free = places.free_places(current, conventions, build_slots)
    summary = places.summary(current, conventions, build_slots)
    if free.total == 0:
        return Check(False, f"ok: every place is full ({summary})", tuple(warnings))
    if mine is None:
        return Check(False, f"ok, not checked: {summary}", tuple(warnings))
    low = [drive for drive in drives or () if drive.low]
    if low:
        said = "; ".join(
            f"{gb_text(drive.free_gb)} GB on {drive.name} ({drive.phrase()}), under its {drive.min_free_gb} GB floor"
            for drive in low
        )
        return Check(
            False,
            f"ok: free disk is {said} (workspace.disk_min_free_gb), so no new build folder starts ({summary})",
            tuple(warnings),
        )
    # A claim paused by a label waits for a maintainer: it is not work to start.
    claimed = {card.issue for card in mine if card.waiting not in conventions.claims.paused_labels}
    studio_issues = {card.issue for card in mine if card.studio}
    networking_issues = {card.issue for card in mine if card.networking}
    renderer_issues = {card.issue for card in mine if card.renderer}
    reasons: list[str] = []
    # The places each track keeps now, before any lapses: the work is read for all of them, so a kept place whose
    # track has nothing to start can lapse and open to the rest (tasks.start.slots.lapse).
    kept = places.kept_places(current, conventions)
    # Work for the kept and open places: a claimed task with no agent first, then the ready issues by priority.
    wants = free.game + free.open + free.networking + free.renderer + kept["networking"] + kept["renderer"]
    waiting = waiting_tasks(scratch, conventions, current, claimed, now) if wants else []
    # A claimed workflow task would hold no place once it runs, so it is not work for a free one.
    exempt_workflow = conventions.tasks.start.slots.exempt_workflow
    placeless = {card.issue for card in mine if exempt_workflow and card.in_workflow}
    waiting = [task for task in waiting if not placeless.intersection(task.issues)]
    work = [_game_work(task, studio_issues, networking_issues, renderer_issues) for task in waiting]
    issues_read = False
    if wants:
        try:
            analysis = analyse_issues(
                sources.ready(),
                counted_claims(mine),
                conventions,
                sources,
                # Every free place may be open once a kept one lapses, so enough work for all of them is looked for.
                max(free.total - sum(1 for item in work if item.track not in ("networking", "renderer")), 0),
                max(
                    free.game - sum(1 for item in work if places.fills_game_place(item.kind, item.track, conventions)),
                    0,
                ),
                max(kept["networking"] - sum(1 for item in work if item.track == "networking"), 0),
                max(kept["renderer"] - sum(1 for item in work if item.track == "renderer"), 0),
            )
            issues_read = True
        except ConfigError as error:
            warnings.append(f"the ready issues could not be read ({error})")
            analysis = Analysis((), ())
        warnings += analysis.warnings
        reasons += analysis.reasons
        work += [_issue_work(issue, conventions) for issue in analysis.eligible]
    work_fix: list[str] = []
    pulls_read = False
    if kept["fix"]:
        work_fix += [f"fix task {name} is queued; start its agent (oldest first)" for name in current.queued_fixes]
        fix_names = [holder.task for holder in current.held("fix")] + list(current.queued_fixes)
        try:
            pulls = uncovered_pulls(
                sources.failed_pulls(), _fix_issues(scratch, conventions, fix_names), holds or [], now
            )
            pulls_read = True
        except ConfigError as error:
            pulls = []
            warnings.append(f"the pull requests could not be read ({error})")
        work_fix += [
            f"#{pull.number} ({pull.branch}) failed required check(s) {', '.join(pull.checks)} and has no fix unit; "
            f"`task create --fix-of <issue>`, then start its agent"
            for pull in pulls
        ]
    lapsed = _lapses(conventions, kept, work, work_fix, issues_read, pulls_read)
    task_slots.write_lapses(scratch, now, lapsed)
    reasons += [f"the place kept for {track} lapsed, so it is open: {why}" for track, why in lapsed.items()]
    free = places.free_places(current, conventions, build_slots, lapsed)
    if free.fix and not work_fix:
        reasons.append("no open pull request has a failed required check without a fix unit")
    # The kept game place takes the first game work of a kind that fills it (`feature`), the one kept for networking
    # the first networking work, the one kept for the renderer the first renderer work; every open place, the next work
    # in order that is neither (their tracks hold at most `max` units, so they never take an open place).
    game_work = [item for item in work if places.fills_game_place(item.kind, item.track, conventions)][: free.game]
    networking_work = [item for item in work if item.track == "networking"][: free.networking]
    renderer_work = [item for item in work if item.track == "renderer"][: free.renderer]
    rest = [item for item in work if item not in game_work and item.track not in ("networking", "renderer")][
        : free.open
    ]
    startable = len(game_work) + len(networking_work) + len(renderer_work) + len(rest) + min(free.fix, len(work_fix))
    for track, started in (("networking", networking_work), ("renderer", renderer_work)):
        waits = [item for item in work if item.track == track and item not in started]
        if waits and len(current.held(track)) >= (conventions.tasks.start.slots.tracks.of(track).max or 0):
            reasons.append(places.track_full(current, conventions, track, len(waits)))
    if startable == 0:
        if free.game and work and not game_work:
            reasons.append(
                f"the {free.game} place(s) kept for game work cannot take Coney Studio, networking, renderer, tooling "
                "or docs work, and no game (`feature`) issue can start"
            )
        if free.networking and work and not networking_work:
            reasons.append(
                f"the {free.networking} place(s) kept for networking cannot take game, Coney Studio, renderer, tooling "
                "or docs work, and no networking issue can start"
            )
        if free.renderer and work and not renderer_work:
            reasons.append(
                f"the {free.renderer} place(s) kept for the renderer cannot take game, Coney Studio, networking, "
                "tooling or docs work, and no renderer issue can start"
            )
        text = "; ".join(reasons) or "there is no work to start"
        return Check(False, f"ok: {text} ({summary})", tuple(warnings))
    waivers, ended = _running_waivers(scratch, now, sources, warnings)
    waived = sum(waiver.slots for waiver in waivers)
    if startable <= waived:
        named = "; ".join(waiver.line(now) for waiver in waivers)
        return Check(False, f"ok: waived ({named}); {startable} place(s) could start work ({summary})", tuple(warnings))
    kept_networking = f"{free.networking} kept for networking, " if free.networking else ""
    kept_renderer = f"{free.renderer} kept for the renderer, " if free.renderer else ""
    lines = [
        f"{BLOCKED} {startable} engineer place(s) can be filled now (free: {free.game} kept for game work, "
        f"{free.fix} kept for a fix, {kept_networking}{kept_renderer}{free.open} open). The places stay full unless "
        "there is a reason (docs/guides/slot-fill.md).",
        f"Now: {summary}.",
    ]
    lines += [f"The place kept for {track} lapsed, so it is open: {why}." for track, why in lapsed.items()]
    if game_work:
        lines.append(
            f"Start now, for the {free.game} place(s) kept for game work (a `feature` task), most urgent first:"
        )
        lines += [f"  - {item.text}" for item in game_work]
    if networking_work:
        lines.append(f"Start now, for the {free.networking} place(s) kept for networking (a networking issue):")
        lines += [f"  - {item.text}" for item in networking_work]
    if renderer_work:
        lines.append(f"Start now, for the {free.renderer} place(s) kept for the renderer (a renderer issue):")
        lines += [f"  - {item.text}" for item in renderer_work]
    if rest:
        lines.append(
            f"Start now, for the {free.open} open place(s) (Coney Studio, or more game work), most urgent first:"
        )
        lines += [f"  - {item.text}" for item in rest[: fill.top]]
        if len(rest) > fill.top:
            lines.append(f"  - ... and {len(rest) - fill.top} more")
    if free.fix and work_fix:
        lines.append("Start now, for the place kept for a fix, oldest first:")
        lines += [f"  - {item}" for item in work_fix[: fill.top]]
    if waived:
        lines.append(f"Active waivers cover {waived} place(s); {startable} could start work.")
    lines += [f"waiver #{waiver.id} ended: {waiver.ended}" for waiver in ended]
    lines.append(f"A reason to leave a place free is a waiver (logged, expiring): {waiver_hint(conventions)}")
    return Check(True, "\n".join(lines), tuple(warnings))


def _lapses(
    conventions: Conventions,
    kept: dict[str, int],
    work: list[places.Work],
    work_fix: list[str],
    issues_read: bool,
    pulls_read: bool,
) -> dict[str, str]:
    """The tracks of `tasks.start.slots.lapse.tracks` that keep a place now and have nothing to start, each with why:
    for the fix track no queued fix and no failed pull request without one, for another track no claimed task and no
    ready, unblocked issue of its own. A track whose work could not be read keeps its place (a GitHub that cannot be
    read lapses nothing)."""
    lapsed: dict[str, str] = {}
    for track in conventions.tasks.start.slots.lapse_tracks:
        if not kept.get(track):
            continue
        if track == "fix":
            if pulls_read and not work_fix:
                lapsed[track] = (
                    "no fix task is queued and no open pull request has a failed required check without a fix unit"
                )
        elif issues_read and track == "reimplementation":
            if not any(places.fills_game_place(item.kind, item.track, conventions) for item in work):
                lapsed[track] = "no game (`feature`) task or issue can start"
        elif issues_read and not any(item.track == track for item in work):
            lapsed[track] = f"no {track} task or issue can start (none is claimed and waiting, ready and unblocked)"
    return lapsed


def _running_waivers(
    scratch: Path, now: datetime, sources: Sources, warnings: list[str]
) -> tuple[list[slot_waivers.Waiver], list[slot_waivers.Waiver]]:
    """The active waivers, and those this call ended because every issue they name is closed (one GitHub read for all
    their issues; a waiver that names none runs its hours and reads nothing). A read that fails keeps every waiver,
    with a warning: an end is written only for issues seen closed."""
    active = slot_waivers.active_waivers(slot_waivers.read_waivers(scratch), now)
    named = sorted({number for waiver in active for number in waiver.issues})
    if not named:
        return active, []
    try:
        closed = sources.closed(named)
    except ConfigError as error:
        warnings += [
            f"whether the issues of waiver #{waiver.id} are closed was not read ({error})"
            for waiver in active
            if waiver.issues
        ]
        return active, []
    running: list[slot_waivers.Waiver] = []
    ended: list[slot_waivers.Waiver] = []
    for waiver in active:
        if waiver.issues and all(number in closed for number in waiver.issues):
            why = f"every issue it names is closed ({', '.join(f'#{n}' for n in waiver.issues)})"
            ended.append(slot_waivers.end_waiver(scratch, waiver, now, why))
        else:
            running.append(waiver)
    return running, ended
