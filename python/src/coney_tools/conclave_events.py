# SPDX-License-Identifier: GPL-3.0-or-later
"""The events of the Conclave snapshot: what Coney itself raises for the coordinator (docs/guides/conclave.md#watchers).

Five things need the coordinator and are found here, each as an event with an id that names the occurrence and a state:

* `check-failed` and `pr-stuck`: an open pull request that `pr list-stuck` calls stuck (a failed required check, or a
  conflict, a CI run that never ends, auto-merge off). The id is the pull request, the cause and the head commit (and
  the failing checks), so a new push is a new event and the same failure read again is the same one. The state is
  `handled` while a hold (`pr hold`) covers the cause on that head, with the hold in `handled_by`; a hold that runs
  out, or that was made on an earlier head, leaves the event open, which is why handled problems are listed.
* `merged`: a pull request merged within `conclave.watch.merged_minutes`.
* `unit-ready`: a release-queue file (`PR.md`) of a unit whose branch has no pull request since the file changed and
  whose proof `pr open` would pass (`pr_ready.readiness`: a passing Check and Review that cover its change, the compared
  functions, the tests that failed first and the description), unless a `HOLD` file lies in its scratch folder. The id
  carries the change the stamps cover, not the file's time, so an edit to the description that the review still covers,
  or a rebase, is the same event; a unit that was released leaves the list.
* `question`: `QUESTION.md` (or `QUESTION-<n>.md`) in the scratch folder of a task unit or of any branch with a worktree
  (most tracks are plain branches, as for `unit-ready`) with no newer `ANSWER` of the same number.
* `disk-low`: a watched drive (watched_drives.py) under its floor, so the coordinator runs `workspace clean` before
  builds fail. The id is the drive, so it is told once while the drive stays low and again if it recovers and falls.

An event that cannot come back once handled leaves the list when it is handled. `guard` marks the events the stop guard
holds the coordinator's turn end for (the pull request problems nobody owns). A kind whose source could not be read is
named in `unread`, so a short list is never taken for "nothing there". Everything here is read-only.
"""

from __future__ import annotations

import hashlib
import re
from datetime import UTC, datetime
from pathlib import Path
from typing import Any

from coney_tools.conclave_conflicts import CAUSE as CONFLICT
from coney_tools.conclave_conflicts import Found, conflict_events
from coney_tools.conclave_sources import Sources, event_row, iso_utc
from coney_tools.config import ConfigError
from coney_tools.conventions import ConclaveWatch
from coney_tools.pr_hold import Hold, HoldsRead, covers_conflict, holds_folder, read_active
from coney_tools.unit import Unit

#: Every kind of event, in the order `unread` lists them.
KINDS = ("check-failed", "pr-stuck", "conflict-fix", "merged", "unit-ready", "question", "disk-low")
#: The kinds that read the open and merged pull requests, the release queue and the unit folders.
PR_KINDS = ("check-failed", "pr-stuck", "conflict-fix", "merged")
#: How many digits of the change hash an event's id keeps.
CHANGE_DIGITS = 12
#: The longest first line of a question that is sent (an event is one line in a wake).
QUESTION_CHARACTERS = 200


def _hold_text(hold: Hold) -> str:
    """Who holds a problem and until when, as the event says it."""
    return f"{hold.who} until {iso_utc(hold.until)}"


def _pull_problems(
    prs: dict[str, Any], holds: HoldsRead, now: datetime, rules: ConclaveWatch, conflicts_apart: bool
) -> list[dict[str, Any]]:
    """An event for each problem `pr list-stuck` found, handled when a running hold covers its cause.

    With `conflicts_apart` the conflicts are left out: `conclave_conflicts` makes work of them."""
    heads = {int(row["number"]): row for row in prs.get("prs", [])}
    found: list[dict[str, Any]] = []
    for item in (prs.get("health") or {}).get("stuck", []):
        number = int(item["number"])
        cause = str(item["cause"])
        if conflicts_apart and cause == CONFLICT:
            continue
        row = heads.get(number, {})
        head = str(row.get("head") or "unknown")[:7]
        checks = sorted(str(name) for name in item.get("checks") or [])
        event_id = f"pr:{number}:{cause}:{head}"
        if checks:
            event_id += ":" + hashlib.sha1(",".join(checks).encode("utf-8")).hexdigest()[:6]
        branch = str(item.get("branch") or row.get("branch") or "")
        detail = str(item.get("detail") or "")
        text = f"#{number}{' ' + branch if branch else ''}: {cause}{' ' + detail if detail else ''}"
        hold = next(
            (
                held
                for held in holds.holds
                if held.pr == number
                and held.covers(cause, now, head)
                and (cause != CONFLICT or covers_conflict(held, rules))
            ),
            None,
        )
        found.append(
            event_row(
                event_id,
                "check-failed" if cause == "checks-failed" else "pr-stuck",
                text,
                f"fix it or start an agent for it, then record it: coney-tools pr hold {number} --who <agent> "
                "(a known wait: no --who, a --note that says why)",
                str(row.get("updated") or "") or None,
                {"pr": number, "task": None, "branch": branch or None},
                guard=True,
                handled_by=None if hold is None else _hold_text(hold),
            )
        )
    return found


def _merges(prs: dict[str, Any], rules: ConclaveWatch, now: datetime) -> list[dict[str, Any]]:
    """An event for each pull request merged within the window."""
    found: list[dict[str, Any]] = []
    for pull in prs.get("merged", []):
        merged_at = str(pull.get("merged_at") or "")
        try:
            moment = datetime.fromisoformat(merged_at.replace("Z", "+00:00"))
        except ValueError:
            continue
        if (now - moment).total_seconds() > rules.merged_minutes * 60:
            continue
        number = int(pull["number"])
        found.append(
            event_row(
                f"merged:{number}",
                "merged",
                f"#{number} {pull.get('branch', '')} merged",
                "bring the issues it affected up to date (issues.md#after-a-merge)",
                merged_at,
                {"pr": number, "task": None, "branch": str(pull.get("branch") or "") or None},
            )
        )
    return found


def _low_drives(report: dict[str, Any], now: datetime, unread: set[str]) -> list[dict[str, Any]]:
    """An event for each watched drive under its floor, from the snapshot's `machine` section; a machine section that
    could not be read names the kind in `unread`, since it may be low and saying nothing is not "all well"."""
    machine = report.get("machine")
    if not isinstance(machine, dict) or machine.get("error"):
        unread.add("disk-low")
        return []
    found: list[dict[str, Any]] = []
    for drive in machine.get("drives", []):
        if drive["free_gb"] >= drive["min_free_gb"]:
            continue
        found.append(
            event_row(
                f"disk-low:{drive['name']}",
                "disk-low",
                f"{drive['name']} has {drive['free_gb']:.1f} GB free, under its {drive['min_free_gb']} GB floor "
                f"({'; '.join(drive['holds'])})",
                "run `coney-tools workspace clean --apply` before builds fail (workspace.md#cleanup)",
                iso_utc(now),
                {"pr": None, "task": None, "branch": None},
            )
        )
    return found


def _ready_units(src: Sources, release: dict[str, Any], unread: set[str]) -> list[dict[str, Any]]:
    """An event for each release-queue file whose unit `pr open` would pass the proof for, and that nobody holds.

    A file in the queue (a unit with a worktree and no pull request since the file changed) is only a candidate: the
    unit is ready when its stamps cover its change, as `pr_ready.readiness` weighs it, the same test `pr open` makes.
    A unit with a `HOLD` file in its scratch folder is left out for as long as the file is there. The id names the
    change, not the file's time, so editing the description (or rebasing the branch) is the same event. A unit whose
    proof could not be read at all names the kind in `unread`: the unit may be ready, and saying nothing of it is not
    the same as saying it is not."""
    rules = src.conventions.conclave.watch
    now = src.coord.now()
    found: list[dict[str, Any]] = []
    for unit in release.get("units", []):
        if not unit.get("queued") or not unit.get("worktree"):
            continue
        branch, name, changed = str(unit["branch"]), str(unit["file"]), str(unit["changed"])
        folder = src.coord.scratch / branch
        if (folder / rules.unit_hold_file).is_file():
            continue
        ready = src.ready(
            Path(str(unit["worktree"])), Unit(branch, folder), Path(str(unit["path"])), src.conventions, now
        )
        if ready.unknown:
            unread.add("unit-ready")
        if not ready.ready:
            continue
        extra = "" if name == "PR.md" else f" --text {unit['path']}"
        found.append(
            event_row(
                f"unit:{branch}:{name}:{ready.change[:CHANGE_DIGITS]}",
                "unit-ready",
                f"{branch}: {name} is ready for pr open",
                f"coney-tools pr open {branch}{extra}",
                changed,
                {"pr": None, "task": branch, "branch": branch},
            )
        )
    return found


def _first_line(path: Path) -> str:
    """The first line of a question that says something: no `#` marks, cut to fit one line of a wake."""
    try:
        text = path.read_text(encoding="utf-8", errors="replace")
    except OSError:
        return "(the file cannot be read)"
    for line in text.splitlines():
        line = line.strip().lstrip("#").strip()
        if line:
            return line if len(line) <= QUESTION_CHARACTERS else line[: QUESTION_CHARACTERS - 3] + "..."
    return "(empty)"


def _questions(scratch: Path, tasks: list[str], rules: ConclaveWatch) -> list[dict[str, Any]]:
    """An event for each question file in a unit's folder that has no answer of the same number written after it."""
    asking = re.compile(rf"^{re.escape(rules.question_prefix)}(-\d+)?\.md$")
    found: list[dict[str, Any]] = []
    for task in tasks:
        folder = scratch / task
        if not folder.is_dir():
            continue
        for path in sorted(folder.iterdir(), key=lambda p: p.name):
            match = asking.match(path.name)
            if not match or not path.is_file():
                continue
            answer = folder / f"{rules.answer_prefix}{match.group(1) or ''}.md"
            asked = path.stat().st_mtime
            if answer.is_file() and answer.stat().st_mtime >= asked:
                continue
            since = iso_utc(datetime.fromtimestamp(asked, UTC))
            found.append(
                event_row(
                    f"question:{task}:{path.name}:{since}",
                    "question",
                    f"{task} asks: {_first_line(path)}",
                    f"read {path}, then answer it: write {answer.name} beside it, or message the agent",
                    since,
                    {"pr": None, "task": task, "branch": task},
                )
            )
    return found


def _question_folders(src: Sources, report: dict[str, Any], unread: set[str]) -> list[str]:
    """The scratch folders that may hold a question: the task units' and every branch that has a worktree.

    A source that fails is named in `unread`, so a short list is never taken for "nobody asks"."""
    units = report.get("units") or {}
    names: set[str] = set()
    if units.get("error"):
        unread.add("question")
    else:
        names.update(str(row["task"]) for row in units.get("units", []))
    try:
        names.update(branch for _path, branch in src.coord.worktrees() if branch)
    except (ConfigError, OSError):
        unread.add("question")
    return sorted(names)


def collect_events(src: Sources, report: dict[str, Any], release: dict[str, Any] | None) -> dict[str, Any]:
    """The `events` section of a GitHub snapshot: the events, the kinds that could not be read and any unreadable hold.

    `report` is the snapshot so far (its `units` and `prs` sections) and `release` the release queue that
    `coord status` read, or None when it could not be.
    """
    rules = src.conventions.conclave.watch
    now = src.coord.now()
    scratch = src.coord.scratch
    unread: set[str] = set()
    found: list[dict[str, Any]] = []
    holds = read_active(holds_folder(scratch, rules), now)
    prs = report.get("prs")
    prs_read = isinstance(prs, dict) and not prs.get("error")
    ready: list[dict[str, Any]] = []
    if release is None or release.get("error"):
        unread.add("unit-ready")
    else:
        ready = _ready_units(src, release, unread)
    conflicts: Found | None = None
    if prs_read:
        # Conflicts are work, not news (conclave_conflicts): a trial rebase, then a unit-ready or a conflict-fix event.
        try:
            conflicts = conflict_events(src, report, release, holds, ready, unread)
        except (ConfigError, OSError, ValueError, KeyError, TypeError):
            unread.add("conflict-fix")
    if not prs_read or not isinstance(prs, dict):
        unread.update(PR_KINDS)
    else:
        found += _pull_problems(prs, holds, now, rules, conflicts is not None) + _merges(prs, rules, now)
        if prs.get("merged_error"):
            unread.add("merged")
    if conflicts is not None:
        found += conflicts.events
        ready = conflicts.ready
    found += ready
    folders = _question_folders(src, report, unread)
    found += _questions(scratch, folders, rules)
    found += _low_drives(report, now, unread)
    found.sort(key=lambda event: (event["since"] or "", event["id"]))
    return {
        "error": None,
        "events": found,
        "unread": [kind for kind in KINDS if kind in unread],
        "holds_problems": holds.problems,
    }
