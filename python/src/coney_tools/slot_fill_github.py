# SPDX-License-Identifier: GPL-3.0-or-later
"""The GitHub reads of `task slots --fill-check` (`slot_fill.Sources` over `gh`), bounded and briefly cached.

The check runs at the end of every coordinator turn (the Stop hook), so it has to be quick and must never hang: each
`gh` call is killed after `tasks.start.fill.github_seconds`, the reads together may take `budget_seconds`, and the
first read that fails ends the reading (every later one fails at once). The answers are kept in the scratch folder for
`cache_seconds`, and dropped as soon as a task file or unit record changed since (a claim just made or a unit just
started must show). Every failure is a `ConfigError`, which the check turns into a pass with a warning.
"""

from __future__ import annotations

import json
import subprocess
import time
from collections.abc import Callable
from datetime import datetime
from pathlib import Path
from typing import Any

from coney_tools import pr_health, task_slots
from coney_tools.audit_github import Runner
from coney_tools.config import ConfigError
from coney_tools.conventions import Conventions
from coney_tools.issue_claim_model import Card, blocked_by_numbers, iso_time, parse_card, parse_time, render_card
from coney_tools.issue_claim_remote import GhRemote, Remote
from coney_tools.issue_form import section_heading, section_text
from coney_tools.slot_fill import FailedPull, IssueInfo

#: The cache file in the scratch folder.
CACHE_FILE = "slot-fill-cache.json"
#: Issues and pull requests read at most.
LIMIT = 200
#: The fields read of a pull request: `pr_health`'s, the head commit and the issues it closes.
PULL_FIELDS = f"{pr_health.PULL_FIELDS},headRefOid,closingIssuesReferences"


def bounded_runner(seconds: float) -> Runner:
    """A `gh` runner (read only) that kills a call after `seconds`; OSError when it fails or takes longer."""

    def run(arguments: list[str]) -> str:
        """Run `gh` with the arguments and return its output."""
        try:
            done = subprocess.run(
                ["gh", *arguments], capture_output=True, text=True, encoding="utf-8", timeout=seconds, check=False
            )
        except (OSError, subprocess.SubprocessError) as error:
            raise OSError(f"gh could not run: {error}") from error
        if done.returncode != 0:
            raise OSError(f"gh {' '.join(arguments[:2])} failed: {done.stderr.strip() or done.stdout.strip()}")
        return done.stdout

    return run


def newest_change(scratch: Path, conventions: Conventions) -> float:
    """The latest modification time (seconds) of a task file or unit record in the scratch folder; 0 when there is none.

    A cache older than that predates a claim or a start the check must see."""
    newest = 0.0
    if not scratch.is_dir():
        return newest
    for folder in scratch.iterdir():
        for name in (conventions.tasks.file, conventions.tasks.start.unit_file, task_slots.RESERVATION_FILE):
            try:
                newest = max(newest, (folder / name).stat().st_mtime)
            except OSError:
                continue
    return newest


class GhSources:
    """`slot_fill.Sources` over `gh`: the ready issues, this account's claims, blockers and failed pull requests."""

    def __init__(
        self,
        conventions: Conventions,
        scratch: Path,
        run: Runner,
        now: Callable[[], datetime],
        clock: Callable[[], float] = time.monotonic,
        changed: Callable[[], float] | None = None,
    ) -> None:
        self._conventions = conventions
        self._scratch = scratch
        self._run = run
        self._now = now
        self._clock = clock
        self._changed = changed or (lambda: newest_change(scratch, conventions))
        self._started: float | None = None
        self._stop = ""
        rules = conventions.claims
        self._remote: Remote = GhRemote(rules.bot_login, rules.card_marker, rules.reply_marker, self._gh)
        self._blocked_heading = section_heading(conventions, "blocked-by")

    # -- the bounded call

    def _gh(self, arguments: list[str]) -> str:
        """One `gh` call within the budget; the first failure ends all reading."""
        fill = self._conventions.tasks.start.fill
        if self._started is None:
            self._started = self._clock()
        elif not self._stop and self._clock() - self._started >= fill.budget_seconds:
            self._stop = f"GitHub took more than {fill.budget_seconds} s"
        if self._stop:
            raise OSError(self._stop)
        try:
            return self._run(arguments)
        except OSError as error:
            self._stop = f"an earlier read failed ({error})"
            raise

    def _json(self, arguments: list[str]) -> Any:
        """A `gh` command's output as JSON; ConfigError when it cannot be read."""
        try:
            return json.loads(self._gh(arguments))
        except (OSError, ValueError) as error:
            raise ConfigError(f"cannot read GitHub with gh ({error})") from error

    # -- the cache

    def _cached(self, key: str, load: Callable[[], Any]) -> Any:
        """`load()`, or the answer kept for `key` while it is younger than `cache_seconds` and no task file changed
        since. A cache that cannot be read or written costs only the read."""
        path = self._scratch / CACHE_FILE
        now = self._now()
        try:
            kept = json.loads(path.read_text(encoding="utf-8"))
            if not isinstance(kept, dict):
                kept = {}
        except (OSError, ValueError):
            kept = {}
        entry = kept.get(key)
        if isinstance(entry, dict):
            try:
                at = parse_time(str(entry["at"]))
                fresh = (now - at).total_seconds() < self._conventions.tasks.start.fill.cache_seconds
                if fresh and at.timestamp() >= self._changed():
                    return entry["data"]
            except (KeyError, ValueError, TypeError):
                pass
        data = load()
        kept[key] = {"at": iso_time(now), "data": data}
        try:
            self._scratch.mkdir(parents=True, exist_ok=True)
            path.write_text(json.dumps(kept), encoding="utf-8", newline="\n")
        except OSError:
            pass
        return data

    # -- Sources

    @staticmethod
    def _issues(data: Any) -> list[IssueInfo]:
        """The issues in `gh issue list --json number,title,labels,assignees,body`."""
        if not isinstance(data, list):
            raise ConfigError("gh issue list did not return a list")
        found: list[IssueInfo] = []
        for item in data:
            try:
                found.append(
                    IssueInfo(
                        number=int(item["number"]),
                        title=str(item.get("title") or ""),
                        labels=tuple(str(label["name"]) for label in item.get("labels") or []),
                        assignees=tuple(str(who["login"]) for who in item.get("assignees") or []),
                        body=str(item.get("body") or ""),
                    )
                )
            except (KeyError, TypeError, ValueError) as error:
                raise ConfigError(f"gh issue list gave an issue that cannot be read ({error})") from error
        return found

    def _trimmed(self, data: Any) -> Any:
        """The issues of a `gh issue list` answer with each body cut to its `Blocked by` section: the rest is never
        read, and the answer is kept in the cache."""
        heading = self._blocked_heading
        for item in data if isinstance(data, list) else []:
            if isinstance(item, dict):
                text = section_text(str(item.get("body") or ""), heading) if heading else None
                item["body"] = f"### {heading}" + chr(10) * 2 + text if text else ""
        return data

    def ready(self) -> list[IssueInfo]:
        """The open issues labelled `claims.claimable_status`."""
        status = self._conventions.claims.claimable_status
        fields = "number,title,labels,assignees,body"
        arguments = ["issue", "list", "--state", "open", "--label", status, "--limit", str(LIMIT), "--json", fields]
        return self._issues(self._cached("ready", lambda: self._trimmed(self._json(arguments))))

    def claimed(self) -> list[Card]:
        """The held claim cards of the open issues assigned to this account.

        The cards are the claim Action's own record, and the only place that knows a claim is still held when its issue
        was paused (`S-on-hold`, `S-blocked`, `S-needs-info` take the working label off), so the limit is counted from
        them, never from labels. One read of the assigned issues, then one of each issue's comments, within the budget;
        the cards are kept in the cache as the text of their claim card."""
        rules = self._conventions.claims
        assigned = ["issue", "list", "--state", "open", "--assignee", "@me", "--limit", str(LIMIT), "--json", "number"]

        def load() -> list[str]:
            """The text of the held claim card of each assigned issue that has one."""
            listed = self._json(assigned)
            if not isinstance(listed, list):
                raise ConfigError("gh issue list did not return a list")
            found: list[str] = []
            for item in listed:
                try:
                    number = int(item["number"])
                except (KeyError, TypeError, ValueError) as error:
                    raise ConfigError(f"gh issue list gave an issue that cannot be read ({error})") from error
                card = task_slots.held_card(self._remote, number, self._conventions)
                if card is not None:
                    found.append(render_card(card, rules))
            return found

        texts = self._cached("claimed", load)
        if not isinstance(texts, list):
            raise ConfigError("the kept claim cards cannot be read")
        cards = [parse_card(str(text), rules) for text in texts]
        return [card for card in cards if card is not None]

    def blockers(self, issue: IssueInfo) -> list[int]:
        """The issues in `issue`'s `Blocked by` section that are not finished (a read of each, within the budget)."""
        if self._blocked_heading is None:
            return []
        text = section_text(issue.body, self._blocked_heading)
        return [
            blocker.number
            for blocker in (self._remote.blocker(number) for number in blocked_by_numbers(text or ""))
            if not blocker.done
        ]

    def failed_pulls(self) -> list[FailedPull]:
        """This account's open, non-draft pull requests with a failed required check."""
        default = self._conventions.repo.default_branch

        def load() -> dict[str, Any]:
            """The pull requests and the required checks of the default branch."""
            pulls = self._json(
                ["pr", "list", "--state", "open", "--author", "@me", "--limit", str(LIMIT), "--json", PULL_FIELDS]
            )
            rules = self._json(["api", f"repos/{{owner}}/{{repo}}/rules/branches/{default}"])
            return {"pulls": pulls, "required": list(pr_health.required_from_rules(rules))}

        data = self._cached("pulls", load)
        pulls = data.get("pulls") if isinstance(data, dict) else None
        if not isinstance(pulls, list):
            raise ConfigError("gh pr list did not return a list")
        required = [str(name) for name in data.get("required", [])]
        found: list[FailedPull] = []
        for pull in pulls:
            if not isinstance(pull, dict) or pull.get("isDraft"):
                continue
            failing = tuple(
                sorted(
                    check.name
                    for check in pr_health.checks_of(pull.get("statusCheckRollup"))
                    if check.name in required and check.state == "failed"
                )
            )
            if not failing:
                continue
            closes = tuple(
                int(ref["number"]) for ref in pull.get("closingIssuesReferences") or [] if isinstance(ref, dict)
            )
            found.append(
                FailedPull(
                    number=int(pull["number"]),
                    branch=str(pull.get("headRefName") or ""),
                    checks=failing,
                    closes=closes,
                    head=str(pull.get("headRefOid") or ""),
                )
            )
        return sorted(found, key=lambda item: item.number)
