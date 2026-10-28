# SPDX-License-Identifier: GPL-3.0-or-later
"""The engineer tracks: which units hold a place, and whether the coordinator may start another agent
(docs/guides/agents.md#slots, docs/guides/issues.md#slots).

A unit works in one of three tracks (maintainer decision 2026-10-28, `tasks.start.slots.tracks`): `fix` for a `kind:
fix` task, `studio` for a task whose issue carries `claims.studio_label` (Coney Studio), `reimplementation` for every
other. A machine holds at most `build_slots` (coney.local.toml, the build slots `coney-tools build` keeps) times
`tasks.start.slots.per_build_slot` units of all tracks together; a track may have its own `max`, and the `min` places
of a track that runs fewer are kept from the others, so Studio never takes the last place a reimplementation unit or
a fix needs. A unit holds its place from `task start` until a passing `reviewed` stamp (a fix: its own) or
`finished`, or until its worktree is gone; a unit whose log cannot be trusted, or whose record cannot be read, keeps
its place. The kind is in `<scratch>/<task>/TASK.md`, the unit in `unit.json`, the progress in the stamp log. Between
the coordinator's Agent call and the engineer's `task start` the agent has no unit yet, so `admit --reserve` writes a
short-lived `slot.json` beside the task file (`tasks.start.slots.reserve_minutes`); a unit record that was started
after it replaces it.

Only a unit with a `unit.json` is counted: work that predates `task start` is invisible to the count (`task slots`
lists its worktrees as "without a unit record"). A unit whose issue carries `claims.exempt_label` now (the units that
build the new workflow) holds no place while `tasks.start.slots.exempt_workflow` is on; the cutover turns it off. Both
labels are read from the issue's held claim card on GitHub (its `exempt` and `studio`, which the claim Action
recomputes from the labels on every pass), never from a file here, so nobody changes a track by editing the scratch
folder. A unit whose card cannot be read is counted where it was before the Studio label existed: a fix as a fix,
any other as reimplementation, so a GitHub that cannot be read costs no start; its Studio model is then not checked.

The Agent guard (`.claude/hooks/slot_guard.py`) runs `coney-tools task slots --admit` for each Agent call of the
coordinator. No `engineer` or `tooling` agent starts without a held claim: the brief carries a `Task id: <id>` line, the
task file must exist (`task create` writes it only after its claim held), the claim must be held now (read from the
claim cards on GitHub, one bounded call per issue) and its track must have room. A Studio unit must also be started on
one of the model and effort pairs of `tasks.start.slots.tracks.studio.models`. When GitHub cannot be read the claim
alone goes unchecked: the place is still counted and reserved. The types that hold no place (a reviewer, the
read-only helpers) need no line at all. The brief is still the coordinator's word for what the agent will do, so the
engineer's own `task start`, which counts the same way, is the backstop.
"""

from __future__ import annotations

import contextlib
import json
import os
import re
import time
import uuid
from collections.abc import Callable, Iterable, Iterator
from dataclasses import dataclass
from datetime import datetime, timedelta
from pathlib import Path
from typing import Any

import yaml

from coney_tools import stamps
from coney_tools.config import ConfigError
from coney_tools.conventions import Conventions
from coney_tools.issue_claim_model import Card, iso_time, parse_time
from coney_tools.issue_claim_pass import latest_held, stored_cards
from coney_tools.issue_claim_remote import Remote
from coney_tools.task_file import TASK_ID, TaskFile, TaskFileError, parse_task_file
from coney_tools.unit_record import UnitRecord, UnitRecordError, read_record

#: The exit code of `task slots --admit` for a start that is refused. Any other failure (0 is a start that may go
#: ahead) is a tool that could not judge, and the guard lets the call through: a crash exits 1, a bad option or a slot
#: lock that cannot be had 2. A GitHub that cannot be read is neither: the claim goes unchecked and the slot is still
#: counted (exit 0 with a `warning:` line, or 3 for a full slot).
REFUSED = 3
#: The reservation file in a task's scratch folder, and its format version.
RESERVATION_FILE = "slot.json"
VERSION = 1
#: The lock that makes "count, then reserve" one step when several Agent calls arrive together.
LOCK_FILE = ".slots.lock"
#: How long a lock may be held before it is taken as left by a crashed process, and how long a caller waits for it:
#: longer than the stale age, so that a lock left by a crash is broken by the first caller to wait it out.
LOCK_STALE_SECONDS = 30.0
LOCK_WAIT_SECONDS = 40.0
#: How long the lock file may go on refusing to be made (PermissionError) while no lock file is there before the
#: folder is taken as one that cannot be written: a deletion in flight on Windows clears in milliseconds.
LOCK_DENIED_SECONDS = 2.0
#: How long one GitHub read of an issue's claim cards may take (the `gh` call is killed then), and after how long
#: since the first read no further read starts: a check reads GitHub for at most their sum.
GH_READ_SECONDS = 20.0
GH_BUDGET_SECONDS = 20.0
#: The longest a `task slots --admit` can take apart from starting Python: the read phase, then the lock wait. The
#: hook's `TIMEOUT` is this plus a margin for `uv run` (a test holds them together); a hook that timed out would pass
#: the call uncounted.
ADMIT_MAX_SECONDS = GH_BUDGET_SECONDS + GH_READ_SECONDS + LOCK_WAIT_SECONDS

#: A `Task id: <id>` line of a brief: the line starts with the words, with no list mark, quote or bold in front, so a
#: brief that quotes the rule does not satisfy it. The brief's line ends are made `\n` first.
_TASK_LINE = re.compile(r"^[ \t]*task id[ \t]*:[ \t]*(?P<id>[A-Za-z0-9][A-Za-z0-9-]*)(?=[ \t]|$)", re.I | re.M)

#: Reads the held claim card of an issue from GitHub: the card, or None when no card is held. ConfigError when GitHub
#: cannot be read.
ReadCard = Callable[[int], Card | None]


class Cards:
    """The held claim cards of the issues one check needs, each read from GitHub at most once, within one budget.

    The first read that fails ends the reading: every issue not read yet is then unreadable at once, so a GitHub that
    hangs costs one `GH_READ_SECONDS` timeout, not one per issue. No read starts once `GH_BUDGET_SECONDS` have passed
    since the first, so the whole read phase is at most `GH_BUDGET_SECONDS + GH_READ_SECONDS`. After `freeze` nothing
    more is read: the reads happen before the slot lock is taken, so a slow GitHub never holds the lock past its stale
    age."""

    def __init__(self, read: ReadCard, clock: Callable[[], float] = time.monotonic) -> None:
        self._read = read
        self._clock = clock
        self._known: dict[int, Card | None] = {}
        self._stop = ""
        self._started: float | None = None

    def freeze(self) -> None:
        """Read nothing more from GitHub."""
        self._stop = self._stop or "it was not read before the slot lock was taken"

    def held(self, number: int) -> Card | None:
        """The held card of issue `number`, or None; ConfigError when it cannot be read: a read failed before, the
        budget is spent, or it was not read before the freeze."""
        if number in self._known:
            return self._known[number]
        if self._started is None:
            self._started = self._clock()
        elif not self._stop and self._clock() - self._started >= GH_BUDGET_SECONDS:
            self._stop = f"GitHub took more than {GH_BUDGET_SECONDS:.0f} s for the claim cards"
        if self._stop:
            raise ConfigError(f"the claim card of #{number} was not read: {self._stop}")
        try:
            card = self._read(number)
        except (ConfigError, OSError) as error:
            self._stop = f"an earlier read failed ({error})"
            raise ConfigError(str(error)) from error
        self._known[number] = card
        return card


def held_card(remote: Remote, number: int, conventions: Conventions) -> Card | None:
    """The held claim card of issue `number`: one read of its comments. ConfigError when GitHub cannot be read."""
    found = latest_held(stored_cards(remote.comments(number), conventions.claims))
    return found.card if found is not None else None


#: The tracks, in the order reports list them (`tasks.start.slots.tracks` has one entry for each).
TRACKS = ("reimplementation", "fix", "studio")


@dataclass(frozen=True)
class Holder:
    """One unit (or one started agent) that holds a place."""

    task: str
    kind: str
    #: The track it holds a place in: one of `TRACKS`.
    track: str
    #: `in work` (a unit record), `record unreadable` (a `unit.json` that cannot be read keeps the place) or
    #: `reserved` (the Agent call was let through, `task start` has not run yet).
    state: str
    since: datetime

    def line(self) -> str:
        """The unit as one line of a report."""
        return f"{self.task} ({self.kind}, {self.state} since {iso_time(self.since)})"


@dataclass(frozen=True)
class Scan:
    """What holds the places now."""

    holders: tuple[Holder, ...]
    #: Units in work that hold no place (a `workflow` unit while the exemption is on).
    exempt: tuple[str, ...]
    #: Fix tasks with a task file and no unit, oldest first: the order they are started in.
    queued_fixes: tuple[str, ...]
    #: The worktrees given to `scan` that no unit record names: work the count cannot see.
    unknown_trees: tuple[Path, ...] = ()
    #: Units counted (a fix as a fix, any other as reimplementation) because the labels of their issues could not be
    #: read from GitHub.
    unchecked: tuple[str, ...] = ()

    def held(self, track: str) -> list[Holder]:
        """The holders of one track, oldest first."""
        return sorted((h for h in self.holders if h.track == track), key=lambda h: (h.since, h.task))


@dataclass(frozen=True)
class Verdict:
    """The answer to "may this agent start?"."""

    allowed: bool
    #: What to tell the caller: the refusal's reasons, or a note on why the start is fine.
    message: str
    #: A start that was let through with a check skipped (GitHub could not be read): the guard logs it.
    warning: str = ""


@dataclass(frozen=True)
class Labels:
    """What the held claim cards of a unit's issues say about it."""

    #: One of them carries `claims.exempt_label` (a workflow unit) and not the Studio label.
    workflow: bool
    #: One of them carries `claims.studio_label`.
    studio: bool


def track_of(kind: str, studio: bool) -> str:
    """The track a task works in: `fix` for a fix task, else `studio` for an issue labelled Studio, else
    `reimplementation`."""
    if kind == "fix":
        return "fix"
    return "studio" if studio else "reimplementation"


def machine_limit(conventions: Conventions, build_slots: int) -> int:
    """How many units of all tracks one machine holds: its build slots times `tasks.start.slots.per_build_slot`."""
    return max(build_slots, 1) * conventions.tasks.start.slots.per_build_slot


def unit_labels(issues: Iterable[int], cards: Cards, conventions: Conventions) -> Labels | None:
    """The labels of a unit on these issues, from their held claim cards (`studio`, and `exempt` without `studio` for
    the workflow label). `workflow` is false while `tasks.start.slots.exempt_workflow` is off, so the unit counts like
    any other. A workflow card that was read decides even when another card cannot be; otherwise None when a card
    cannot be read: the caller counts the unit, a fix as a fix and any other as reimplementation."""
    found: list[Card] = []
    unreadable = False
    for number in issues:
        try:
            card = cards.held(number)
        except ConfigError:
            unreadable = True
            continue
        if card is not None:
            found.append(card)
    studio = any(card.studio for card in found)
    workflow = conventions.tasks.start.slots.exempt_workflow and any(card.exempt and not card.studio for card in found)
    if unreadable and not workflow:
        return None
    return Labels(workflow=workflow, studio=studio)


#: The labels of a started unit, or None when they cannot be told (counted, as reimplementation unless a fix).
LabelsOf = Callable[[UnitRecord], Labels | None]


def holds_a_slot(agent_type: str, conventions: Conventions) -> bool:
    """Whether an agent of this subagent type needs a claim and a slot (`tasks.start.slots.agent_types`, any case)."""
    return agent_type.strip().casefold() in (name.casefold() for name in conventions.tasks.start.slots.agent_types)


def unit_over(record: UnitRecord, report: stamps.Report) -> bool:
    """Whether a unit no longer holds a slot: it is finished, or a Review passed after it was claimed (a fix: after
    its own `claimed` stamp). A unit whose claimed stamp is not found keeps its slot."""
    if report.latest("finished") is not None:
        return True
    claimed = [
        stamp
        for stamp in report.stamps
        if stamp.step == "claimed" and stamp.detail.get("fix", "") == (record.task if record.parent else "")
    ]
    reviewed = report.latest("reviewed", result="pass")
    return bool(claimed) and reviewed is not None and reviewed.seq > claimed[-1].seq


def _reservation(path: Path, now: datetime, conventions: Conventions) -> Holder | None:
    """The live reservation in `path`, or None when there is none, it expired or it cannot be read. A reservation
    written before the tracks names a `slot` (`task` or `fix`) instead: `task` reads as reimplementation."""
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
        at = parse_time(str(data["at"]))
        track = str(data["track"]) if "track" in data else track_of(str(data["kind"]), False)
        if track not in TRACKS:
            return None
        holder = Holder(str(data["task"]), str(data["kind"]), track, "reserved", at)
    except (OSError, ValueError, KeyError, TypeError):
        return None
    if now - at > timedelta(minutes=conventions.tasks.start.slots.reserve_minutes):
        return None
    return holder


def _front(path: Path) -> dict[str, Any] | None:
    """The YAML block of a task file as a mapping, or None when the file is not a task file."""
    try:
        text = path.read_text(encoding="utf-8").replace("\r\n", "\n")
    except (OSError, UnicodeDecodeError):
        return None
    if not text.startswith("---\n") or "\n---\n" not in text:
        return None
    try:
        front = yaml.safe_load(text[4:].split("\n---\n", 1)[0])
    except yaml.YAMLError:
        return None
    return front if isinstance(front, dict) else None


def scan(
    scratch: Path,
    conventions: Conventions,
    now: datetime,
    trees: Iterable[Path] = (),
    labels: LabelsOf | None = None,
) -> Scan:
    """The units and reservations on this machine that hold a place, and the fix tasks waiting to start.

    A unit counts while its record, its worktree and no passing Review (or `finished`) since the claim exist; a log that
    cannot be trusted, or a record that cannot be read (while its folder has a task file), keeps the unit counted. A
    folder that is not a task is ignored. `labels` says which live units are workflow units (no place) and which are
    Studio units (from their claim cards); without it every unit counts, a fix as a fix and any other as
    reimplementation, and so does a unit whose labels cannot be told (listed in `unchecked`). `trees` are the
    worktrees of the workspace: those no readable record names come back as `unknown_trees`."""
    holders: list[Holder] = []
    exempt: list[str] = []
    unchecked: list[str] = []
    queued: list[tuple[datetime, str]] = []
    known: set[Path] = set()
    if not scratch.is_dir():
        return Scan((), (), (), tuple(trees))
    for folder in sorted(scratch.iterdir()):
        if not folder.is_dir():
            continue
        front = _front(folder / conventions.tasks.file)
        record: UnitRecord | None = None
        unit_path = folder / conventions.tasks.start.unit_file
        if unit_path.is_file():
            try:
                record = read_record(unit_path)
            except UnitRecordError:
                kind = str(front.get("kind")) if front and front.get("kind") else "feature"
                holders.append(Holder(folder.name, kind, track_of(kind, False), "record unreadable", now))
                continue
        live = False
        if record is not None:
            known.add(record.worktree.resolve())
            if record.worktree.is_dir():
                report = stamps.verify(scratch / record.log_task / "stamps", conventions.stamps)
                live = not (report.ok and unit_over(record, report))
        if record is not None and live:
            told = labels(record) if labels is not None else Labels(workflow=False, studio=False)
            if told is not None and told.workflow:
                exempt.append(record.task)
                continue
            if told is None:
                unchecked.append(record.task)
            studio = told.studio if told is not None else False
            holders.append(Holder(record.task, record.kind, track_of(record.kind, studio), "in work", record.started))
            continue
        reserved = _reservation(folder / RESERVATION_FILE, now, conventions)
        if reserved is not None and (record is None or reserved.since > record.started):
            holders.append(reserved)
            continue
        if record is None and front is not None and front.get("kind") == "fix":
            try:
                queued.append((parse_time(str(front["claimed_at"])), str(front["task"])))
            except (KeyError, ValueError, TypeError):
                continue
    queued.sort()
    unknown = tuple(tree for tree in trees if tree.resolve() not in known)
    return Scan(tuple(holders), tuple(exempt), tuple(name for _, name in queued), unknown, tuple(unchecked))


def brief_task(brief: str) -> str | None:
    """The task id a brief names with a `Task id: <id>` line, or None.

    A line counts only when it starts with the words: a list bullet, a quote or bold in front of them (a brief that
    quotes the rule) does not, and neither does the placeholder `<id>`."""
    found = _TASK_LINE.search(brief.replace("\r\n", "\n").replace("\r", "\n"))
    return found.group("id").lower() if found is not None else None


def claim_problem(task: TaskFile, cards: Cards) -> str | None:
    """Why the claim of `task` is not held now, or None while every issue's claim is the claimant's for this task.

    One read of the issue's comments each (the claim cards): a claim released since `task create` wrote the file
    (idle, `/unclaim`, lost) is found here. A fix task's claim is its parent's. ConfigError when GitHub cannot be
    read."""
    expected = task.parent or task.task
    for number in task.issues:
        card = cards.held(number)
        if card is None:
            return f"the claim on #{number} is not held now (released, never made, or idle too long)"
        if card.claimant != task.claimant:
            return f"#{number} is claimed by {card.claimant}, and the task file names {task.claimant}"
        if card.task != expected:
            return f"#{number} is claimed for the task '{card.task}', not '{expected}'"
    return None


def _read_task(scratch: Path, task: str, conventions: Conventions) -> TaskFile | str:
    """The task record of `task`, or why it cannot be used."""
    if not TASK_ID.match(task):
        return f"'{task}' is not a task id"
    path = scratch / task / conventions.tasks.file
    if not path.is_file():
        return f"there is no task file for '{task}' (expected {path}): the coordinator writes it with `task create`"
    try:
        found = parse_task_file(path.read_text(encoding="utf-8"), conventions.tasks)
    except (TaskFileError, OSError, UnicodeDecodeError) as error:
        return f"the task file of '{task}' cannot be read ({error})"
    if found.task != task:
        return f"{path} is the task file of '{found.task}', not '{task}'"
    return found


def _howto() -> str:
    """The sentence that tells how to name the unit in a brief."""
    return (
        "Put a line `Task id: <id>` in the brief (the id `task create` printed once its claim held; the engineer then "
        "runs `task start`), at the start of a line with nothing in front of it. There is no way to start an engineer "
        "or tooling agent without a held claim"
    )


@dataclass(frozen=True)
class Room:
    """Whether one more unit of a track fits on the machine, and the numbers behind the answer."""

    track: str
    #: Units of the track in work, and its own cap (None: only the machine's).
    count: int
    maximum: int | None
    #: Units of every track in work, and the machine limit.
    total: int
    machine: int
    #: The places kept for other tracks that run fewer than their `min`: track to places.
    kept: dict[str, int]

    @property
    def track_full(self) -> bool:
        """The track's own cap is reached."""
        return self.maximum is not None and self.count + 1 > self.maximum

    @property
    def machine_full(self) -> bool:
        """The machine has no place left once the kept places are set aside."""
        return self.total + 1 + sum(self.kept.values()) > self.machine

    @property
    def fits(self) -> bool:
        """One more unit of the track may start."""
        return not (self.track_full or self.machine_full)

    def note(self, task: str) -> str:
        """The line of a start that is let through (or would be, unenforced)."""
        text = f"{self.total + 1} of {self.machine} places on this machine in use with {task} ({self.track} track"
        text += f", {self.count + 1} of {self.maximum})" if self.maximum is not None else ")"
        return text


def room(current: Scan, track: str, task: str, conventions: Conventions, build_slots: int) -> Room:
    """Whether a unit of `track` (the task `task`, which is left out of the count) fits beside what `current` holds.

    It fits while its track is under its own `max` and the machine (`machine_limit`) has a place once the `min`
    places of every other track that runs fewer than its `min` are set aside (`tasks.start.slots.tracks`)."""
    tracks = conventions.tasks.start.slots.tracks
    others = [holder for holder in current.holders if holder.task != task]
    counts = {name: sum(1 for holder in others if holder.track == name) for name in TRACKS}
    kept = {
        name: tracks.of(name).min - counts[name]
        for name in TRACKS
        if name != track and counts[name] < tracks.of(name).min
    }
    return Room(
        track=track,
        count=counts[track],
        maximum=tracks.of(track).max,
        total=len(others),
        machine=machine_limit(conventions, build_slots),
        kept=kept,
    )


def _full(scan_: Scan, fit: Room, task: TaskFile, build_slots: int, conventions: Conventions) -> str:
    """The refusal for a start that does not fit: why (the track's cap or the machine), the busy units and what frees
    a place."""
    per = conventions.tasks.start.slots.per_build_slot
    if fit.track_full:
        busy = scan_.held(fit.track)
        lines = [
            f"Blocked: the {fit.track} track is full ({fit.count} of {fit.maximum} in use), so the agent for "
            f"'{task.task}' ({task.kind}) may not start now. Busy:"
        ]
    else:
        busy = sorted(scan_.holders, key=lambda h: (TRACKS.index(h.track), h.since, h.task))
        kept = ", ".join(
            f"{places} kept for {name}, which runs fewer than its minimum" for name, places in fit.kept.items()
        )
        lines = [
            f"Blocked: no place on this machine for a {fit.track} unit ({fit.total} of {fit.machine} in use"
            + (f"; {kept}" if kept else "")
            + f"; the limit is {build_slots} build slot(s) x {per}), so the agent for '{task.task}' ({task.kind}) "
            "may not start now. Busy:"
        ]
        busy = [h for h in busy if h.task != task.task]
    lines += [f"  - {holder.line()}" + ("" if fit.track_full else f" [{holder.track}]") for holder in busy]
    if scan_.unchecked:
        lines.append(
            f"Counted because GitHub could not say whether their issue has the workflow or the Studio label (taken "
            f"as reimplementation unless a fix): "
            f"{', '.join(scan_.unchecked)}."
        )
    lines.append(
        "A place frees when its unit passes Review (`task review --record`), is finished or loses its worktree; "
        "the agent of a unit that waits for CI holds none. Start this agent when one of them does"
        + (
            f" (fix tasks start oldest first; queued: {', '.join(scan_.queued_fixes) or 'none'})."
            if fit.track == "fix"
            else "."
        )
    )
    lines.append(
        "Limits: tasks.start.slots.tracks and per_build_slot in .github/conventions.yml, build_slots in "
        "coney.local.toml; a unit whose issue has the workflow label holds no place. A unit that was abandoned is "
        "freed by removing its worktree or `finished`-stamping it. Rule: docs/guides/agents.md#slots"
    )
    return "\n".join(lines) + "\n"


def model_problem(model: str, effort: str, conventions: Conventions) -> str | None:
    """Why a Studio unit may not start on this model and effort, or None: the pair (any letter case) must be one of
    `tasks.start.slots.tracks.studio.models`. An empty list allows any."""
    allowed = conventions.tasks.start.slots.tracks.studio.models
    if not allowed:
        return None
    pair = (model.strip().casefold(), effort.strip().casefold())
    if any(pair == (entry.model, entry.effort) for entry in allowed):
        return None
    named = f"model '{model or 'unset'}' at effort '{effort or 'unset'}'"
    choices = "; ".join(f"{entry.model} at {entry.effort} ({entry.use})" for entry in allowed)
    return (
        f"a Studio unit starts on one of these, named in the Agent call (`model` and `effort`): {choices}, not {named} "
        "(tasks.start.slots.tracks.studio.models, docs/guides/agents.md#models)"
    )


def admit(
    scratch: Path,
    conventions: Conventions,
    agent_type: str,
    brief: str,
    now: datetime,
    read_card: ReadCard,
    task: str | None = None,
    reserve: bool = False,
    clock: Callable[[], float] = time.monotonic,
    *,
    build_slots: int,
    model: str = "",
    effort: str = "",
) -> Verdict:
    """Whether the coordinator may start an agent of `agent_type` now.

    A type that holds no place (`holds_a_slot`) always passes. An `engineer` or `tooling` agent needs, in this order: a
    unit (`task`, or the brief's `Task id:` line), its task file, a claim that is held now (the claim cards, through
    `read_card`), for a Studio unit a `model` and `effort` the Studio track allows, and room in its track
    (`room`, with the machine's `build_slots`). When GitHub cannot be read only the claim goes unchecked (the verdict's
    `warning` says so): the task is not exempt then, it is judged as reimplementation unless it is a fix (no model
    check), the place is counted and a full track or machine still refuses. A start that passes with `reserve` is
    recorded as a reservation, under a lock so that Agent calls made together are counted one after the other; every
    GitHub read happens before the lock, within the budget of `Cards` (`clock` times it)."""
    slots = conventions.tasks.start.slots
    if not holds_a_slot(agent_type, conventions):
        return Verdict(True, f"{agent_type or 'this agent'} holds no slot")
    named = task or brief_task(brief)
    if named is None:
        return Verdict(
            False,
            f"Blocked: this {agent_type} agent names no unit, so it has no claim to work under and no slot to count. "
            f"{_howto()} (docs/guides/agents.md#slots).\n",
        )
    found = _read_task(scratch, named, conventions)
    if isinstance(found, str):
        return Verdict(False, f"Blocked: {found}. {_howto()} (docs/guides/agents.md#slots).\n")
    cards = Cards(read_card, clock)
    warning = ""
    try:
        problem = claim_problem(found, cards)
    except ConfigError as error:
        problem = None
        warning = f"the claim of '{named}' was not checked, GitHub could not be read ({error})"
    if problem is not None:
        return Verdict(
            False,
            f"Blocked: '{named}' has a task file but no claim to work under: {problem}. "
            "Claim the issue again with `task create` (docs/guides/agents.md#slots).\n",
        )
    told = unit_labels(found.issues, cards, conventions)
    if told is not None and told.workflow:
        return Verdict(
            True, f"{named} is a workflow task (its issue has the workflow label): no place until the cutover", warning
        )
    track = track_of(found.kind, told.studio if told is not None else False)
    if told is not None and told.studio and found.kind != "fix":
        wrong = model_problem(model, effort, conventions)
        if wrong is not None and slots.enforce:
            return Verdict(False, f"Blocked: '{named}' is a Studio task, and {wrong}.\n", warning)

    def labels(record: UnitRecord) -> Labels | None:
        """A live unit's labels, from the cards read for this check."""
        return unit_labels(record.issues, cards, conventions)

    scan(scratch, conventions, now, labels=labels)  # reads the other units' cards before the lock
    cards.freeze()
    with locked(scratch, reserve):
        current = scan(scratch, conventions, now, labels=labels)
        mine = [holder for holder in current.holders if holder.task == named]
        if mine and not (reserve and mine[0].state == "reserved"):
            return Verdict(True, f"{named} holds a {mine[0].track} place already ({mine[0].state})", warning)
        if mine:
            return Verdict(
                False,
                f"Blocked: an agent was started for '{named}' at {iso_time(mine[0].since)} and has not run `task "
                f"start` yet, so a second one would work on the same unit. If that agent never started (the call was "
                f"denied), wait {slots.reserve_minutes} minutes or delete {scratch / named / RESERVATION_FILE} "
                "(docs/guides/agents.md#slots).\n",
            )
        fit = room(current, track, named, conventions, build_slots)
        if not fit.fits and slots.enforce:
            return Verdict(False, _full(current, fit, found, build_slots, conventions))
        note = fit.note(named)
        if not fit.fits:
            note += " (over the limit, which is not enforced: tasks.start.slots.enforce)"
        if reserve:
            write_reservation(scratch / named, found, track, agent_type, now)
        return Verdict(True, note, warning)


def write_reservation(folder: Path, task: TaskFile, track: str, agent_type: str, now: datetime) -> None:
    """Record that an agent of `agent_type` was let start for `task` in `track`; replaces the file whole."""
    data = {
        "v": VERSION,
        "task": task.task,
        "kind": task.kind,
        "track": track,
        "agent_type": agent_type,
        "at": iso_time(now),
    }
    folder.mkdir(parents=True, exist_ok=True)
    temporary = folder / (RESERVATION_FILE + ".tmp")
    temporary.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8", newline="\n")
    temporary.replace(folder / RESERVATION_FILE)


def _take(path: Path, nonce: str) -> str:
    """Make the lock file holding `nonce`: `taken`, `held` when it exists, or `denied` when the create raised
    PermissionError. On Windows a create that meets a file whose deletion is in flight raises PermissionError instead
    of FileExistsError, and so does a folder that may not be written: `locked` tells them apart by how long it lasts."""
    try:
        handle = os.open(path, os.O_CREAT | os.O_EXCL | os.O_WRONLY)
    except FileExistsError:
        return "held"
    except PermissionError:
        return "denied"
    with os.fdopen(handle, "w", encoding="utf-8") as lock:
        lock.write(nonce)
    return "taken"


def _break_stale(path: Path) -> None:
    """Remove the lock file when it is older than `LOCK_STALE_SECONDS`, so that of two waiters only one removes it.

    The file is renamed away first (one rename wins, the other finds nothing). If the renamed file turns out fresh, it
    was a new holder's lock that took the place of the stale one between the look and the rename, and it is linked back.
    The window that stays open: when a third caller makes a new lock in the instant between that rename and the link
    back, the link fails, and the displaced holder and the third caller both hold the lock until the first of them
    releases (each removes only its own nonce). That needs a lock left past the stale age and, with this waiter, three
    callers interleaved within one rename (four with a crashed holder); the two inside may both take the last free
    slot, one start over the limit.
    The renamed-aside file is always removed."""
    try:
        if time.time() - path.stat().st_mtime <= LOCK_STALE_SECONDS:
            return
        aside = path.with_name(f"{path.name}.stale-{uuid.uuid4().hex}")
        path.replace(aside)
    except OSError:
        return
    try:
        if time.time() - aside.stat().st_mtime <= LOCK_STALE_SECONDS:
            os.link(aside, path)  # fails while another lock stands: the window the docstring names
    except OSError:
        pass
    finally:
        with contextlib.suppress(OSError):
            aside.unlink()


def _release(path: Path, nonce: str) -> None:
    """Remove the lock file if it is still ours (a lock broken as stale and taken by another is left alone)."""
    with contextlib.suppress(OSError):
        if path.read_text(encoding="utf-8") == nonce:
            path.unlink()


@contextlib.contextmanager
def locked(
    scratch: Path,
    needed: bool = True,
    clock: Callable[[], float] = time.monotonic,
    sleep: Callable[[float], None] = time.sleep,
) -> Iterator[None]:
    """Hold the slot lock (a file made with O_EXCL in the scratch folder) while counting and reserving.

    A lock older than `LOCK_STALE_SECONDS` is taken as a crashed process's and broken; a caller waits for
    `LOCK_WAIT_SECONDS` (longer, so a crash costs one wait, not a refusal to count) and then raises ConfigError (the
    guard passes the call). A create refused with PermissionError while no lock file is there for
    `LOCK_DENIED_SECONDS` is a folder that cannot be written, and raises ConfigError at once instead of waiting. The
    file holds its owner's nonce, and only the owner removes it. Nothing is locked when `needed` is false."""
    if not needed:
        yield
        return
    path = scratch / LOCK_FILE
    scratch.mkdir(parents=True, exist_ok=True)
    nonce = uuid.uuid4().hex
    deadline = clock() + LOCK_WAIT_SECONDS
    denied_since: float | None = None
    while (outcome := _take(path, nonce)) != "taken":
        if outcome == "denied" and not path.exists():
            denied_since = clock() if denied_since is None else denied_since
            if clock() - denied_since > LOCK_DENIED_SECONDS:
                raise ConfigError(f"{path}: the slot lock cannot be made (permission denied; is {scratch} writable?)")
        else:
            denied_since = None
        _break_stale(path)
        if clock() > deadline:
            raise ConfigError(f"{path}: another Agent call has held the slot lock for {LOCK_WAIT_SECONDS:.0f} s")
        sleep(0.05)
    try:
        yield
    finally:
        _release(path, nonce)


def _track_words(conventions: Conventions, track: str) -> str:
    """A track's own rule in words: its minimum and its cap."""
    rule = conventions.tasks.start.slots.tracks.of(track)
    parts = [f"at least {rule.min} kept" if rule.min else "no minimum"]
    parts.append(f"at most {rule.max}" if rule.max is not None else "no cap but the machine's")
    return ", ".join(parts)


def report(current: Scan, conventions: Conventions, build_slots: int) -> list[str]:
    """The lines `task slots` prints: the machine's places, each track with its holders, the units without a place,
    the queued fixes and the worktrees the count cannot see."""
    slots = conventions.tasks.start.slots
    machine = machine_limit(conventions, build_slots)
    lines = [
        f"places: {len(current.holders)} of {machine} in use "
        f"({build_slots} build slot(s) x {slots.per_build_slot}, every track together)"
    ]
    for track in TRACKS:
        held = current.held(track)
        lines.append(f"{track}: {len(held)} in work ({_track_words(conventions, track)})")
        lines += [f"  {holder.line()}" for holder in held]
    if not current.held("reimplementation"):
        lines.append("no reimplementation unit is in work: the track keeps at least one going")
    if current.exempt:
        lines.append(f"no place (workflow units until the cutover): {', '.join(current.exempt)}")
    if current.unchecked:
        lines.append(
            f"counted as reimplementation (or fix), the labels could not be read from GitHub: "
            f"{', '.join(current.unchecked)}"
        )
    if current.queued_fixes:
        lines.append(f"fix tasks waiting, oldest first: {', '.join(current.queued_fixes)}")
    if current.unknown_trees:
        names = ", ".join(tree.name for tree in current.unknown_trees)
        lines.append(f"worktrees without a unit record, which no slot counts ({len(current.unknown_trees)}): {names}")
    lines.append(f"refuses a start over the limit: {'yes' if slots.enforce else 'no (tasks.start.slots.enforce)'}")
    return lines


def as_json(current: Scan, conventions: Conventions, build_slots: int) -> dict[str, Any]:
    """The same as a mapping, for tools that read it."""
    tracks = conventions.tasks.start.slots.tracks
    return {
        "enforce": conventions.tasks.start.slots.enforce,
        "machine": {"limit": machine_limit(conventions, build_slots), "build_slots": build_slots},
        "tracks": {
            track: {
                "min": tracks.of(track).min,
                "max": tracks.of(track).max,
                "holders": [
                    {"task": h.task, "kind": h.kind, "state": h.state, "since": iso_time(h.since)}
                    for h in current.held(track)
                ],
            }
            for track in TRACKS
        },
        "exempt": list(current.exempt),
        "unchecked": list(current.unchecked),
        "queued_fixes": list(current.queued_fixes),
        "worktrees_without_unit": [tree.name for tree in current.unknown_trees],
    }
