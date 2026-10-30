# SPDX-License-Identifier: GPL-3.0-or-later
"""Waivers of the slot-fill check: a logged, expiring permission to leave engineer places free
(docs/guides/slot-fill.md).

`task slots --fill-check` fails while a place is free and work can be started. Some gaps are right, and only the
coordinator knows them: the maintainer asked for a pause, the work waits on a maintainer decision, the machine is too
loaded. `task slots --waive N --reason <code>` records that for N places, for at most the hours the reason code allows
(`tasks.start.fill.waivers` in `.github/conventions.yml`, which holds the codes). The log is one JSON object per line
in the scratch folder and is never edited: a waiver ends by expiring, and `task slots` lists the active ones.
"""

from __future__ import annotations

import json
from dataclasses import dataclass
from datetime import datetime, timedelta
from pathlib import Path

from coney_tools.config import ConfigError
from coney_tools.conventions import FillRules
from coney_tools.issue_claim_model import iso_time, parse_time

#: The waiver log in the scratch folder, and its format version.
WAIVER_FILE = "slot-waivers.jsonl"
VERSION = 1


@dataclass(frozen=True)
class Waiver:
    """One waiver: `slots` free places may stay free from `at` until `until`."""

    id: int
    at: datetime
    until: datetime
    slots: int
    #: A reason code of `tasks.start.fill.waivers.codes`.
    reason: str
    note: str
    #: The issues the work is blocked on (a `maintainer-decision` names them).
    issues: tuple[int, ...]
    #: Who made it: the agent type and id the hook vouched for, or `unverified`.
    who: str

    def active(self, now: datetime) -> bool:
        """Whether the waiver still runs at `now`."""
        return self.at <= now < self.until

    def line(self, now: datetime) -> str:
        """The waiver as one line of a report."""
        left = max(self.until - now, timedelta(0))
        hours = left.total_seconds() / 3600
        named = f" (blocked: {', '.join(f'#{n}' for n in self.issues)})" if self.issues else ""
        return (
            f"#{self.id} {self.slots} place(s), {self.reason}{named}, until {iso_time(self.until)} "
            f"({hours:.1f} h left), by {self.who}: {self.note}"
        )


def as_json(waiver: Waiver) -> dict[str, object]:
    """The waiver as the JSON object `task slots --json` and the log hold (the log adds its format version)."""
    return {
        "id": waiver.id,
        "at": iso_time(waiver.at),
        "until": iso_time(waiver.until),
        "slots": waiver.slots,
        "reason": waiver.reason,
        "note": waiver.note,
        "issues": list(waiver.issues),
        "who": waiver.who,
    }


def _parse(line: str) -> Waiver | None:
    """The waiver a log line holds, or None for a line that is not one (a half-written or damaged line)."""
    try:
        data = json.loads(line)
        if data["v"] != VERSION:
            return None
        return Waiver(
            id=int(data["id"]),
            at=parse_time(str(data["at"])),
            until=parse_time(str(data["until"])),
            slots=int(data["slots"]),
            reason=str(data["reason"]),
            note=str(data["note"]),
            issues=tuple(int(n) for n in data["issues"]),
            who=str(data["who"]),
        )
    except (ValueError, KeyError, TypeError):
        return None


def read_waivers(scratch: Path) -> list[Waiver]:
    """Every waiver in the log, oldest first; a line that cannot be read is skipped (a damaged log excuses nothing)."""
    try:
        text = (scratch / WAIVER_FILE).read_text(encoding="utf-8")
    except (OSError, UnicodeDecodeError):
        return []
    return [waiver for waiver in (_parse(line) for line in text.splitlines() if line.strip()) if waiver is not None]


def active_waivers(waivers: list[Waiver], now: datetime) -> list[Waiver]:
    """The waivers that still run at `now`."""
    return [waiver for waiver in waivers if waiver.active(now)]


def waived_slots(waivers: list[Waiver], now: datetime) -> int:
    """How many free places the active waivers excuse in all."""
    return sum(waiver.slots for waiver in active_waivers(waivers, now))


def add_waiver(
    scratch: Path,
    rules: FillRules,
    total_slots: int,
    now: datetime,
    slots: int,
    reason: str,
    hours: float,
    note: str,
    issues: tuple[int, ...],
    who: str,
) -> Waiver:
    """Record a waiver; ConfigError says what is wrong when it is not allowed.

    The reason must be a code of the conventions, the hours a positive number within the code's cap (and the overall
    cap), the note at least `note_min_characters` long, `slots` between 1 and the number of places the machine has, and
    a code that `needs_issues` must name them. Nothing is written for a refused waiver."""
    code = rules.waiver(reason)
    if code is None:
        known = ", ".join(item.name for item in rules.waivers)
        raise ConfigError(f"--reason {reason!r} is not a waiver code (known: {known})")
    if not 1 <= slots <= total_slots:
        raise ConfigError(f"--waive {slots}: the machine has {total_slots} places, waive 1 to {total_slots}")
    longest = min(code.max_hours, rules.waiver_max_hours)
    if not 0 < hours <= longest:
        raise ConfigError(f"--hours {hours:g}: a '{reason}' waiver runs more than 0 and at most {longest} hours")
    if len(note.strip()) < rules.note_min_characters:
        raise ConfigError(f"--note: say why in at least {rules.note_min_characters} characters")
    if code.needs_issues and not issues:
        raise ConfigError(
            f"--reason {reason}: name the blocked issues with --issues (a '{reason}' waiver {code.meaning})"
        )
    previous = read_waivers(scratch)
    waiver = Waiver(
        id=max((item.id for item in previous), default=0) + 1,
        at=now,
        until=now + timedelta(hours=hours),
        slots=slots,
        reason=reason,
        note=" ".join(note.split()),
        issues=issues,
        who=who,
    )
    record = {"v": VERSION, **as_json(waiver)}
    scratch.mkdir(parents=True, exist_ok=True)
    with (scratch / WAIVER_FILE).open("a", encoding="utf-8", newline="\n") as log:
        log.write(json.dumps(record) + "\n")
    return waiver


def report(waivers: list[Waiver], now: datetime) -> list[str]:
    """The lines `task slots` prints about waivers: the active ones, and how many ended."""
    running = active_waivers(waivers, now)
    lines = [f"waivers: {len(running)} active"] if running else ["waivers: none active"]
    lines += [f"  {waiver.line(now)}" for waiver in running]
    ended = len(waivers) - len(running)
    if ended:
        lines.append(f"  ({ended} ended, in {WAIVER_FILE})")
    return lines
