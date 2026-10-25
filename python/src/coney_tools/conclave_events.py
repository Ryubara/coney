# SPDX-License-Identifier: GPL-3.0-or-later
"""The events of the Conclave snapshot: what Coney itself raises for the coordinator (docs/guides/conclave.md#watchers).

Four things need the coordinator and are found here, each as an event with an id that names the occurrence and a state:

* `check-failed` and `pr-stuck`: an open pull request that `repo pr-health` calls stuck (a failed required check, or a
  conflict, a CI run that never ends, auto-merge off). The id is the pull request, the cause and the head commit (and
  the failing checks), so a new push is a new event and the same failure read again is the same one. The state is
  `handled` while a hold (`pr hold`) covers the cause, with the hold in `handled_by`; a hold that runs out makes the
  event open again, which is why handled problems are listed.
* `merged`: a pull request merged within `conclave.watch.merged_minutes`.
* `unit-ready`: a release-queue file (`PR.md`) of a unit whose branch has no pull request since the file changed. The id
  carries the file's time, so a rewritten file is a new event; a unit that was released leaves the list.
* `question`: `QUESTION.md` (or `QUESTION-<n>.md`) in a unit's scratch folder with no newer `ANSWER` of the same number.

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

from coney_tools.conclave_sources import Sources, iso_utc
from coney_tools.conventions import ConclaveWatch
from coney_tools.pr_hold import Hold, HoldsRead, holds_folder, read_active

#: Every kind of event, in the order `unread` lists them.
KINDS = ("check-failed", "pr-stuck", "merged", "unit-ready", "question")
#: The kinds that read the open and merged pull requests, the release queue and the unit folders.
PR_KINDS = ("check-failed", "pr-stuck", "merged")
#: The longest first line of a question that is sent (an event is one line in a wake).
QUESTION_CHARACTERS = 200


def _event(
    event_id: str,
    kind: str,
    text: str,
    do: str,
    since: str | None,
    ref: dict[str, Any],
    guard: bool = False,
    handled_by: str | None = None,
) -> dict[str, Any]:
    """One event row; `handled_by` set makes it a handled one."""
    return {
        "id": event_id,
        "kind": kind,
        "state": "open" if handled_by is None else "handled",
        "ref": ref,
        "since": since,
        "text": text,
        "do": do,
        "guard": guard,
        "handled_by": handled_by,
    }


def _hold_text(hold: Hold) -> str:
    """Who holds a problem and until when, as the event says it."""
    return f"{hold.who} until {iso_utc(hold.until)}"


def _pull_problems(prs: dict[str, Any], holds: HoldsRead, now: datetime) -> list[dict[str, Any]]:
    """An event for each problem `repo pr-health` found, handled when a running hold covers its cause."""
    heads = {int(row["number"]): row for row in prs.get("prs", [])}
    found: list[dict[str, Any]] = []
    for item in (prs.get("health") or {}).get("stuck", []):
        number = int(item["number"])
        cause = str(item["cause"])
        row = heads.get(number, {})
        head = str(row.get("head") or "unknown")[:7]
        checks = sorted(str(name) for name in item.get("checks") or [])
        event_id = f"pr:{number}:{cause}:{head}"
        if checks:
            event_id += ":" + hashlib.sha1(",".join(checks).encode("utf-8")).hexdigest()[:6]
        branch = str(item.get("branch") or row.get("branch") or "")
        detail = str(item.get("detail") or "")
        text = f"#{number}{' ' + branch if branch else ''}: {cause}{' ' + detail if detail else ''}"
        hold = next((held for held in holds.holds if held.pr == number and held.covers(cause, now)), None)
        found.append(
            _event(
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
            _event(
                f"merged:{number}",
                "merged",
                f"#{number} {pull.get('branch', '')} merged",
                "bring the issues it affected up to date (issues.md#after-a-merge)",
                merged_at,
                {"pr": number, "task": None, "branch": str(pull.get("branch") or "") or None},
            )
        )
    return found


def _ready_units(release: dict[str, Any]) -> list[dict[str, Any]]:
    """An event for each release-queue file whose branch has no pull request since the file changed."""
    found: list[dict[str, Any]] = []
    for unit in release.get("units", []):
        if not unit.get("queued"):
            continue
        branch, name, changed = str(unit["branch"]), str(unit["file"]), str(unit["changed"])
        extra = "" if name == "PR.md" else f" --text {unit['path']}"
        found.append(
            _event(
                f"unit:{branch}:{name}:{changed}",
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
                _event(
                    f"question:{task}:{path.name}:{since}",
                    "question",
                    f"{task} asks: {_first_line(path)}",
                    f"read {path}, then answer it: write {answer.name} beside it, or message the agent",
                    since,
                    {"pr": None, "task": task, "branch": task},
                )
            )
    return found


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
    if not isinstance(prs, dict) or prs.get("error"):
        unread.update(PR_KINDS)
    else:
        found += _pull_problems(prs, holds, now) + _merges(prs, rules, now)
        if prs.get("merged_error"):
            unread.add("merged")
    if release is None or release.get("error"):
        unread.add("unit-ready")
    else:
        found += _ready_units(release)
    units = report.get("units") or {}
    if units.get("error"):
        unread.add("question")
    else:
        found += _questions(scratch, [str(row["task"]) for row in units.get("units", [])], rules)
    found.sort(key=lambda event: (event["since"] or "", event["id"]))
    return {
        "error": None,
        "events": found,
        "unread": [kind for kind in KINDS if kind in unread],
        "holds_problems": holds.problems,
    }
