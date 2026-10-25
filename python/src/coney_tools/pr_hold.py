# SPDX-License-Identifier: GPL-3.0-or-later
"""`coney-tools pr hold`: say that somebody owns a pull request's problems (docs/guides/pr.md#hold).

A pull request with a failed check, a conflict or the like is a problem the coordinator must see (Conclave's watcher
wakes it and its stop guard keeps the session from ending its turn on one). A *hold* ends that: it records that an agent
is fixing the problem (`--who`) or that waiting is right (a waiver: no `--who`, a `--note` that says why), for a few
hours, optionally for one cause. A hold that runs out raises the problem again, so a fix that stalls cannot hide.

The store is one JSON file per pull request in the scratch directory (`conclave.watch.holds_dir`), written atomically,
so two commands never write one file and the snapshot (`conclave snapshot`) reads them without a lock. Only the
coordinator runs this command (`.claude/hooks/hooks.toml` refuses it to the other roles).
"""

from __future__ import annotations

import argparse
import json
import os
import re
import sys
from collections.abc import Callable
from dataclasses import dataclass, field
from datetime import UTC, datetime, timedelta
from pathlib import Path
from typing import Any

from coney_tools.build_cli import scratch_root
from coney_tools.config import ConfigError, find_repo_root
from coney_tools.conventions import PR_HEALTH_CAUSES, ConclaveWatch, load_conventions

#: The `who` of a waiver: a known wait, with nobody working on it.
WAIVED = "waived"
#: The files of the store: `<pull request>.json`.
HOLD_FILE = re.compile(r"^(\d+)\.json$")


@dataclass(frozen=True)
class Hold:
    """One pull request's hold: who fixes it (or `waived`), for which cause, until when."""

    pr: int
    #: The agent working on the problem, or `WAIVED`.
    who: str
    #: The one cause covered (a `repo pr-health` cause); None covers every cause of the pull request.
    cause: str | None
    until: datetime
    note: str
    #: When the hold was made.
    at: datetime

    @property
    def waived(self) -> bool:
        """Whether this is a waiver (nobody works on the problem; the note says why waiting is right)."""
        return self.who == WAIVED

    def covers(self, cause: str, now: datetime) -> bool:
        """Whether the hold is still running at `now` and covers `cause`."""
        return self.until > now and (self.cause is None or self.cause == cause)

    def as_json(self) -> dict[str, Any]:
        """The hold as its file and the JSON output write it."""
        return {
            "pr": self.pr,
            "who": self.who,
            "cause": self.cause,
            "until": iso(self.until),
            "note": self.note,
            "at": iso(self.at),
        }


@dataclass
class HoldsRead:
    """The holds that still run, and the files of the store that could not be read (each named with the reason)."""

    holds: list[Hold] = field(default_factory=list)
    problems: list[str] = field(default_factory=list)


def iso(moment: datetime) -> str:
    """A moment as ISO 8601 UTC with seconds, `2026-10-25T16:00:00Z`."""
    return moment.astimezone(UTC).strftime("%Y-%m-%dT%H:%M:%SZ")


def _moment(value: object) -> datetime:
    """A stored moment; ValueError when it is not one."""
    if not isinstance(value, str):
        raise ValueError("a time is not text")
    return datetime.fromisoformat(value.replace("Z", "+00:00")).astimezone(UTC)


def holds_folder(scratch: Path, rules: ConclaveWatch) -> Path:
    """The folder of the store in the scratch directory."""
    return scratch / rules.holds_dir


def _from_file(path: Path, number: int) -> Hold:
    """The hold a file holds; ValueError or OSError when it cannot be read."""
    data = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(data, dict) or data.get("pr") != number:
        raise ValueError("it does not hold this pull request's hold")
    cause = data.get("cause")
    if cause is not None and not isinstance(cause, str):
        raise ValueError("cause is not text")
    who = data.get("who")
    if not isinstance(who, str) or not who:
        raise ValueError("who is missing")
    return Hold(
        pr=number,
        who=who,
        cause=cause,
        until=_moment(data.get("until")),
        note=str(data.get("note") or ""),
        at=_moment(data.get("at")),
    )


def _files(folder: Path) -> list[tuple[int, Path]]:
    """The store's files by pull request number; none for a folder that is not there."""
    if not folder.is_dir():
        return []
    found = [(int(match.group(1)), path) for path in folder.iterdir() if (match := HOLD_FILE.match(path.name))]
    return sorted(found)


def read_active(folder: Path, now: datetime) -> HoldsRead:
    """The holds that still run at `now`; a file that cannot be read is a problem, never a crash or a silent gap."""
    result = HoldsRead()
    for number, path in _files(folder):
        try:
            held = _from_file(path, number)
        except (OSError, ValueError, KeyError, TypeError) as error:
            result.problems.append(f"{path.name}: {error}")
            continue
        if held.until > now:
            result.holds.append(held)
    return result


def write(folder: Path, held: Hold, now: datetime | None = None) -> Path:
    """Record `held` (replacing the pull request's earlier hold) and, with `now`, remove the holds that have run out."""
    folder.mkdir(parents=True, exist_ok=True)
    if now is not None:
        for number, path in _files(folder):
            try:
                expired = _from_file(path, number).until <= now
            except (OSError, ValueError, KeyError, TypeError):
                continue
            if expired:
                path.unlink(missing_ok=True)
    target = folder / f"{held.pr}.json"
    temporary = folder / f".{held.pr}.{os.getpid()}.tmp"
    temporary.write_text(json.dumps(held.as_json(), indent=1) + "\n", encoding="utf-8")
    temporary.replace(target)
    return target


def release(folder: Path, pr: int) -> bool:
    """Remove the pull request's hold; whether there was one."""
    path = folder / f"{pr}.json"
    if not path.is_file():
        return False
    path.unlink()
    return True


def add_parser(commands: Any) -> None:
    """Register `coney-tools pr hold` in the `pr` group."""
    sub = commands.add_parser(
        "hold",
        help="say that somebody owns a pull request's problems for a few hours, so the coordinator is not woken or "
        "kept from stopping for them (docs/guides/pr.md#hold)",
    )
    sub.add_argument("pr", nargs="?", type=int, metavar="PR", help="the pull request's number")
    sub.add_argument("--who", help="the agent or track fixing it; leave out for a waiver (--note then says why)")
    sub.add_argument("--hours", type=float, help="how long the hold lasts (default and most: conclave.watch)")
    sub.add_argument("--cause", choices=PR_HEALTH_CAUSES, help="hold only this cause (default: every cause)")
    sub.add_argument("--note", default="", help="what is being done, or why waiting is right")
    sub.add_argument("--release", action="store_true", help="end the pull request's hold now")
    sub.add_argument("--list", action="store_true", help="print the holds that run")
    sub.add_argument("--json", action="store_true", help="print the result as JSON (with --list)")


def _line(held: Hold) -> str:
    """One hold as a line: the pull request, who, the cause, when it ends and the note."""
    who = "waived" if held.waived else held.who
    cause = held.cause or "every cause"
    return f"#{held.pr} {who} ({cause}) until {iso(held.until)}" + (f": {held.note}" if held.note else "")


def run_hold(
    args: argparse.Namespace,
    folder: Path,
    rules: ConclaveWatch,
    now: datetime,
    out: Callable[[str], None],
    err: Callable[[str], None],
) -> int:
    """`pr hold` against the store in `folder` at `now`: 0 done, 1 nothing to release, 2 a refused request."""
    if args.list:
        if args.pr is not None or args.release or args.who is not None:
            err("pr hold: --list takes no pull request and no other option but --json")
            return 2
        found = read_active(folder, now)
        for problem in found.problems:
            err(f"pr hold: {problem}")
        if args.json:
            out(json.dumps({"holds": [held.as_json() for held in found.holds], "problems": found.problems}, indent=1))
        elif not found.holds:
            out("no holds")
        else:
            for held in found.holds:
                out(_line(held))
        return 0
    if args.pr is None:
        err("pr hold: give the pull request's number (or --list)")
        return 2
    if args.release:
        if args.who is not None or args.hours is not None or args.cause is not None:
            err("pr hold: --release takes only the pull request's number")
            return 2
        if release(folder, args.pr):
            out(f"#{args.pr} released")
            return 0
        err(f"pr hold: no hold on #{args.pr}")
        return 1
    if args.who is None and not args.note.strip():
        err("pr hold: a waiver (no --who) needs a --note that says why waiting is right")
        return 2
    hours = float(rules.hold_default_hours) if args.hours is None else args.hours
    if not 0 < hours <= rules.hold_max_hours:
        err(f"pr hold: --hours must be more than 0 and at most {rules.hold_max_hours}")
        return 2
    held = Hold(
        pr=args.pr,
        who=args.who or WAIVED,
        cause=args.cause,
        until=now + timedelta(hours=hours),
        note=args.note.strip(),
        at=now,
    )
    write(folder, held, now)
    out(f"held {_line(held)}")
    return 0


def _stderr(line: str) -> None:
    """Print one line to stderr."""
    print(line, file=sys.stderr)


def run(args: argparse.Namespace) -> int:
    """`pr hold` against the real store: the scratch directory of the checkout the command runs in."""
    try:
        root = find_repo_root(Path.cwd())
        rules = load_conventions(root).conclave.watch
        folder = holds_folder(scratch_root(root), rules)
    except ConfigError as error:
        _stderr(f"pr hold: {error}")
        return 2
    return run_hold(args, folder, rules, datetime.now(UTC), print, _stderr)
