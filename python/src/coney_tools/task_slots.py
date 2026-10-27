# SPDX-License-Identifier: GPL-3.0-or-later
"""The engineer slots: which units hold one, and whether the coordinator may start another agent
(docs/guides/agents.md#slots, docs/guides/issues.md#slots).

A machine runs `tasks.start.slots.task` task units and `tasks.start.slots.fix` fix units at a time. A unit holds its
slot from `task start` until a passing `reviewed` stamp (a fix: its own) or `finished`, or until its worktree is gone;
a unit whose log cannot be trusted keeps its slot. The task records say everything: the kind is in
`<scratch>/<task>/TASK.md`, the unit in `unit.json`, the progress in the stamp log. Between the coordinator's Agent
call and the engineer's `task start` the agent has no unit yet, so `admit --reserve` writes a short-lived
`slot.json` beside the task file (`tasks.start.slots.reserve_minutes`); a unit record that was started after it
replaces it.

Kinds in `tasks.start.slots.exempt_kinds` (until the cutover, the tooling units that build the workflow) hold no slot.
`kind: fix` takes the fix slot, every other kind a task slot. The Agent guard (`.claude/hooks/slot_guard.py`) runs
`coney-tools task slots --admit` for each Agent call of the coordinator; the brief names the unit with a
`Task id: <id>` line, or says `Slot: none <reason>` for work that has no unit (another repository, a read-only
survey). Nothing here reads GitHub or the network.
"""

from __future__ import annotations

import contextlib
import json
import os
import re
import time
from collections.abc import Callable, Iterator
from dataclasses import dataclass
from datetime import datetime, timedelta
from pathlib import Path
from typing import Any

import yaml

from coney_tools import stamps
from coney_tools.config import ConfigError
from coney_tools.conventions import Conventions
from coney_tools.issue_claim_model import iso_time, parse_time
from coney_tools.task_file import TASK_ID, TaskFile, TaskFileError, parse_task_file
from coney_tools.unit_record import UnitRecord, UnitRecordError, read_record

#: The reservation file in a task's scratch folder, and its format version.
RESERVATION_FILE = "slot.json"
VERSION = 1
#: The lock that makes "count, then reserve" one step when several Agent calls arrive together.
LOCK_FILE = ".slots.lock"
#: How long a lock may be held before it is taken as left by a crashed process, and how long a caller waits for it.
LOCK_STALE_SECONDS = 30.0
LOCK_WAIT_SECONDS = 10.0

#: `Task id: <id>` in a brief (a list bullet, quotes or bold around it are fine).
_TASK_LINE = re.compile(
    r"^[ \t>*_`-]*task[ \t]+id[ \t]*[:=][ \t*_`]*(?P<id>[A-Za-z0-9][A-Za-z0-9-]*)", re.IGNORECASE | re.MULTILINE
)
#: `Slot: none <reason>` in a brief: work that has no unit, with the reason.
_NO_SLOT_LINE = re.compile(
    r"^[ \t>*_`-]*slot[ \t]*[:=][ \t*_`]*none\b[ \t*_`:,;-]*(?P<why>\S.*)$", re.IGNORECASE | re.MULTILINE
)


@dataclass(frozen=True)
class Holder:
    """One unit (or one started agent) that holds a slot."""

    task: str
    kind: str
    #: `task` or `fix`: the slot it holds.
    slot: str
    #: `in work` (a unit record) or `reserved` (the Agent call was let through, `task start` has not run yet).
    state: str
    since: datetime

    def line(self) -> str:
        """The unit as one line of a report."""
        return f"{self.task} ({self.kind}, {self.state} since {iso_time(self.since)})"


@dataclass(frozen=True)
class Scan:
    """What holds the slots now."""

    holders: tuple[Holder, ...]
    #: Units of an exempt kind in work: they hold no slot.
    exempt: tuple[str, ...]
    #: Fix tasks with a task file and no unit, oldest first: the order they are started in.
    queued_fixes: tuple[str, ...]

    def held(self, slot: str) -> list[Holder]:
        """The holders of one slot, oldest first."""
        return sorted((h for h in self.holders if h.slot == slot), key=lambda h: (h.since, h.task))


@dataclass(frozen=True)
class Verdict:
    """The answer to "may this agent start?"."""

    allowed: bool
    #: What to tell the caller: the refusal's reasons, or a note on why the start is fine.
    message: str


def slot_of(kind: str) -> str:
    """The slot a kind of task takes: `fix` for a fix task, `task` for every other kind."""
    return "fix" if kind == "fix" else "task"


def limit(conventions: Conventions, slot: str) -> int:
    """How many units may hold `slot` at once."""
    slots = conventions.tasks.start.slots
    return slots.fix if slot == "fix" else slots.task


def is_exempt(kind: str, conventions: Conventions) -> bool:
    """Whether a unit of this kind holds no slot (`tasks.start.slots.exempt_kinds`)."""
    return kind in conventions.tasks.start.slots.exempt_kinds


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
    """The live reservation in `path`, or None when there is none, it expired or it cannot be read."""
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
        at = parse_time(str(data["at"]))
        holder = Holder(str(data["task"]), str(data["kind"]), str(data["slot"]), "reserved", at)
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


def scan(scratch: Path, conventions: Conventions, now: datetime) -> Scan:
    """The units and reservations on this machine that hold a slot, and the fix tasks waiting to start.

    A unit counts while its record, its worktree and no passing Review (or `finished`) since the claim exist; a log that
    cannot be trusted keeps the unit counted. A folder that is not a task is ignored."""
    holders: list[Holder] = []
    exempt: list[str] = []
    queued: list[tuple[datetime, str]] = []
    if not scratch.is_dir():
        return Scan((), (), ())
    for folder in sorted(scratch.iterdir()):
        if not folder.is_dir():
            continue
        record: UnitRecord | None = None
        live = False
        unit_path = folder / conventions.tasks.start.unit_file
        if unit_path.is_file():
            try:
                record = read_record(unit_path)
            except UnitRecordError:
                record = None
        if record is not None and record.worktree.is_dir():
            report = stamps.verify(scratch / record.log_task / "stamps", conventions.stamps)
            live = not (report.ok and unit_over(record, report))
        if record is not None and live:
            if is_exempt(record.kind, conventions):
                exempt.append(record.task)
            else:
                holders.append(Holder(record.task, record.kind, slot_of(record.kind), "in work", record.started))
            continue
        reserved = _reservation(folder / RESERVATION_FILE, now, conventions)
        if reserved is not None and (record is None or reserved.since > record.started):
            if not is_exempt(reserved.kind, conventions):
                holders.append(reserved)
            continue
        if record is None:
            front = _front(folder / conventions.tasks.file)
            if front is not None and front.get("kind") == "fix":
                try:
                    queued.append((parse_time(str(front["claimed_at"])), str(front["task"])))
                except (KeyError, ValueError, TypeError):
                    continue
    queued.sort()
    return Scan(tuple(holders), tuple(exempt), tuple(name for _, name in queued))


def brief_task(brief: str) -> tuple[str | None, str | None]:
    """(the task id, the reason for no slot) a brief names: its `Task id:` line, or its `Slot: none <reason>` line.

    Both are None when the brief says neither; the task id wins when it says both."""
    task = _TASK_LINE.search(brief)
    if task is not None:
        return task.group("id").lower(), None
    none = _NO_SLOT_LINE.search(brief)
    return (None, none.group("why").strip()) if none is not None else (None, None)


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
        "Put a line `Task id: <id>` in the brief (the id `task create` printed; the engineer then runs `task start`), "
        "or `Slot: none <reason>` for work that has no unit (another repository, a read-only survey, a measurement)"
    )


def _full(scan_: Scan, slot: str, conventions: Conventions, task: TaskFile) -> str:
    """The refusal for a start whose slot is full: the busy units and what frees one."""
    held = scan_.held(slot)
    maximum = limit(conventions, slot)
    word = "fix" if slot == "fix" else "task"
    lines = [
        f"Blocked: no {word} slot is free on this machine ({len(held)} of {maximum} in use), so the agent for "
        f"'{task.task}' ({task.kind}) may not start now. Busy:"
    ]
    lines += [f"  - {holder.line()}" for holder in held]
    lines.append(
        "A slot frees when its unit passes Review (`task review --record`), is finished or loses its worktree; "
        "the agent of a unit that waits for CI holds none. Start this agent when one of them does"
        + (
            "."
            if slot == "task"
            else f" (fix tasks start oldest first; queued: {', '.join(scan_.queued_fixes) or 'none'})."
        )
    )
    exempt = ", ".join(conventions.tasks.start.slots.exempt_kinds) or "none"
    lines.append(
        f"Limits: tasks.start.slots in .github/conventions.yml (kinds without a slot: {exempt}). "
        "Rule: docs/guides/agents.md#slots"
    )
    return "\n".join(lines) + "\n"


def admit(
    scratch: Path,
    conventions: Conventions,
    agent_type: str,
    brief: str,
    now: datetime,
    task: str | None = None,
    reserve: bool = False,
) -> Verdict:
    """Whether the coordinator may start an agent of `agent_type` now.

    `task` names the unit; when it is None the brief does (`brief_task`). A type that holds no slot always passes. A
    start that passes with `reserve` writes the reservation, under a lock so that Agent calls made together are
    counted one after the other."""
    slots = conventions.tasks.start.slots
    if agent_type.casefold() not in (name.casefold() for name in slots.agent_types):
        return Verdict(True, f"{agent_type or 'this agent'} holds no slot")
    named, why = (task, None) if task else brief_task(brief)
    if named is None:
        if why:
            return Verdict(True, f"no slot: {why}")
        return Verdict(
            False,
            f"Blocked: this {agent_type} agent names no unit, so its slot cannot be counted. {_howto()} "
            "(docs/guides/agents.md#slots).\n",
        )
    found = _read_task(scratch, named, conventions)
    if isinstance(found, str):
        return Verdict(False, f"Blocked: {found}. {_howto()} (docs/guides/agents.md#slots).\n")
    if is_exempt(found.kind, conventions):
        return Verdict(True, f"{named} is a {found.kind} task, which holds no slot until the cutover")
    slot = slot_of(found.kind)
    with locked(scratch, reserve):
        current = scan(scratch, conventions, now)
        mine = [holder for holder in current.holders if holder.task == named]
        others = [holder for holder in current.held(slot) if holder.task != named]
        count = len(others) + 1
        if mine:
            return Verdict(True, f"{named} holds a {slot} slot already ({mine[0].state})")
        if count > limit(conventions, slot) and slots.enforce:
            return Verdict(False, _full(current, slot, conventions, found))
        note = f"{count} of {limit(conventions, slot)} {slot} slots in use with {named}"
        if count > limit(conventions, slot):
            note += " (over the limit, which is not enforced: tasks.start.slots.enforce)"
        if reserve:
            write_reservation(scratch / named, found, slot, agent_type, now)
        return Verdict(True, note)


def write_reservation(folder: Path, task: TaskFile, slot: str, agent_type: str, now: datetime) -> None:
    """Record that an agent of `agent_type` was let start for `task`; replaces the file whole."""
    data = {
        "v": VERSION,
        "task": task.task,
        "kind": task.kind,
        "slot": slot,
        "agent_type": agent_type,
        "at": iso_time(now),
    }
    folder.mkdir(parents=True, exist_ok=True)
    temporary = folder / (RESERVATION_FILE + ".tmp")
    temporary.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8", newline="\n")
    temporary.replace(folder / RESERVATION_FILE)


@contextlib.contextmanager
def locked(
    scratch: Path,
    needed: bool = True,
    clock: Callable[[], float] = time.monotonic,
    sleep: Callable[[float], None] = time.sleep,
) -> Iterator[None]:
    """Hold the slot lock (a file made with O_EXCL in the scratch folder) while counting and reserving.

    A lock older than `LOCK_STALE_SECONDS` is taken as a crashed process's and replaced; a caller that cannot get it
    in `LOCK_WAIT_SECONDS` raises ConfigError (the guard then passes the call). Nothing is locked when `needed` is
    false."""
    if not needed:
        yield
        return
    path = scratch / LOCK_FILE
    scratch.mkdir(parents=True, exist_ok=True)
    deadline = clock() + LOCK_WAIT_SECONDS
    while True:
        try:
            handle = os.open(path, os.O_CREAT | os.O_EXCL | os.O_WRONLY)
        except FileExistsError:
            with contextlib.suppress(OSError):
                if time.time() - path.stat().st_mtime > LOCK_STALE_SECONDS:
                    path.unlink()
                    continue
            if clock() > deadline:
                raise ConfigError(f"{path}: another Agent call holds the slot lock") from None
            sleep(0.05)
            continue
        os.close(handle)
        break
    try:
        yield
    finally:
        with contextlib.suppress(OSError):
            path.unlink()


def report(current: Scan, conventions: Conventions) -> list[str]:
    """The lines `task slots` prints: both slots with their holders, the exempt units and the queued fixes."""
    lines: list[str] = []
    for slot in ("task", "fix"):
        held = current.held(slot)
        lines.append(f"{slot} slots: {len(held)} of {limit(conventions, slot)} in use")
        lines += [f"  {holder.line()}" for holder in held]
    slots = conventions.tasks.start.slots
    if current.exempt:
        lines.append(f"no slot ({', '.join(slots.exempt_kinds)} until the cutover): {', '.join(current.exempt)}")
    if current.queued_fixes:
        lines.append(f"fix tasks waiting, oldest first: {', '.join(current.queued_fixes)}")
    lines.append(f"refuses a start over the limit: {'yes' if slots.enforce else 'no (tasks.start.slots.enforce)'}")
    return lines


def as_json(current: Scan, conventions: Conventions) -> dict[str, Any]:
    """The same as a mapping, for tools that read it (Conclave's band)."""
    return {
        "enforce": conventions.tasks.start.slots.enforce,
        "slots": {
            slot: {
                "limit": limit(conventions, slot),
                "holders": [
                    {"task": h.task, "kind": h.kind, "state": h.state, "since": iso_time(h.since)}
                    for h in current.held(slot)
                ],
            }
            for slot in ("task", "fix")
        },
        "exempt": list(current.exempt),
        "queued_fixes": list(current.queued_fixes),
    }
