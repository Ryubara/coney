# SPDX-License-Identifier: GPL-3.0-or-later
"""The `coney-tools claim ...` commands: what the claim Action runs and what the mod reads.

`claim run` is the whole workflow step: it takes the event's name and its payload file (never text from the event on a
command line), picks the issue or issues the event concerns and reconciles each; `schedule` and `workflow_dispatch`
run the sweep. `claim sweep` runs the sweep by hand, `claim list` prints the held claims (`--json` for the mod).
(docs/guides/issues.md#claiming) `gh` is run through an injected runner, so the tests never touch the network.
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from collections.abc import Callable
from datetime import UTC, datetime
from pathlib import Path
from typing import Any

from coney_tools.audit_github import Runner, run_gh
from coney_tools.config import ConfigError, find_repo_root
from coney_tools.conventions import Conventions, load_conventions
from coney_tools.issue_claim_engine import Engine
from coney_tools.issue_claim_model import Card, iso_time, show_time
from coney_tools.issue_claim_remote import GhRemote, Remote


def add_claim_parser(groups: argparse._SubParsersAction[argparse.ArgumentParser]) -> None:
    """Add the `claim` group: `run`, `sweep` and `list`."""
    group = groups.add_parser(
        "claim", help="claim cards, the claim Action's steps and the sweep (docs/guides/issues.md)"
    )
    commands = group.add_subparsers(dest="command", required=True)
    run = commands.add_parser("run", help="handle one GitHub event (the claim workflow's step)")
    run.add_argument(
        "--event", required=True, help="the event name: issue_comment, issues, pull_request_target, schedule"
    )
    run.add_argument("--payload", required=True, type=Path, help="the event's JSON file ($GITHUB_EVENT_PATH)")
    commands.add_parser("sweep", help="reconcile every claimed issue and every unhandled command now")
    listing = commands.add_parser("list", help="print the held claims")
    listing.add_argument("--json", action="store_true", help="print the cards' data as JSON for the mod")


def _remote(conventions: Conventions, run: Runner) -> GhRemote:
    """A GitHub remote for the repository `gh` finds."""
    rules = conventions.claims
    return GhRemote(rules.bot_login, rules.card_marker, rules.reply_marker, run)


def _utc_now() -> datetime:
    """The current time, in UTC."""
    return datetime.now(UTC)


def closing_numbers(body: str, conventions: Conventions) -> list[int]:
    """The issue numbers a pull request body closes: lines `Fixes #N` (the `pull_requests.closing` keyword)."""
    keyword = re.escape(conventions.pull_requests.closing.keyword)
    found = re.findall(rf"(?im)^[ \t]*{keyword}[ \t]+#(\d+)[ \t]*$", body.replace("\r\n", "\n"))
    return list(dict.fromkeys(int(number) for number in found))


def issues_of_event(name: str, payload: dict[str, Any], conventions: Conventions) -> list[int] | None:
    """The issues an event concerns, or None when it calls for the sweep (`schedule`, `workflow_dispatch`)."""
    if name in ("schedule", "workflow_dispatch"):
        return None
    if name in ("issue_comment", "issues"):
        issue = payload.get("issue")
        if not isinstance(issue, dict) or "pull_request" in issue or not isinstance(issue.get("number"), int):
            return []
        return [int(issue["number"])]
    if name in ("pull_request_target", "pull_request"):
        pull = payload.get("pull_request")
        if not isinstance(pull, dict):
            return []
        return closing_numbers(str(pull.get("body") or ""), conventions)
    raise ConfigError(f"claim run: no handler for the event {name!r}")


def run_event(
    engine: Engine, name: str, payload: dict[str, Any], conventions: Conventions, now: Callable[[], datetime]
) -> tuple[list[str], list[str]]:
    """Handle an event: the log lines and the errors. A push to a pull request (`synchronize`) is activity."""
    numbers = issues_of_event(name, payload, conventions)
    if numbers is None:
        result = engine.sweep()
        return result.lines, result.errors
    push_at = now() if name.startswith("pull_request") and payload.get("action") == "synchronize" else None
    lines: list[str] = []
    errors: list[str] = []
    for number in numbers:
        try:
            lines.extend(engine.reconcile(number, push_at))
        except ConfigError as error:
            errors.append(f"#{number}: {error}")
    return lines, errors


def run_claim(args: argparse.Namespace, run: Runner = run_gh, now: Callable[[], datetime] = _utc_now) -> int:
    """Dispatch a `claim` command; 0 on success, 1 when an issue could not be finished."""
    conventions = load_conventions(find_repo_root(Path.cwd()))
    remote = _remote(conventions, run)
    if args.command == "list":
        return _list(remote, conventions, args.json)
    engine = Engine(remote, conventions, now)
    if args.command == "sweep":
        result = engine.sweep()
        lines, errors = result.lines, result.errors
    else:
        try:
            payload = json.loads(args.payload.read_text(encoding="utf-8"))
        except (OSError, ValueError) as error:
            raise ConfigError(f"--payload {args.payload}: cannot be read as JSON ({error})") from error
        if not isinstance(payload, dict):
            raise ConfigError(f"--payload {args.payload}: expected a JSON object")
        lines, errors = run_event(engine, args.event, payload, conventions, now)
    for line in lines:
        print(line)
    for problem in errors:
        print(f"claim: {problem}", file=sys.stderr)
    if not lines and not errors:
        print("claim: nothing to do")
    return 1 if errors else 0


def held_cards(remote: Remote, conventions: Conventions) -> list[Card]:
    """The held claims of the repository, oldest issue first."""
    engine = Engine(remote, conventions, _utc_now)
    return [card for _, card in sorted(engine.held_cards().items())]


def _list(remote: Remote, conventions: Conventions, as_json: bool) -> int:
    """Print the held claims."""
    cards = held_cards(remote, conventions)
    rules = conventions.claims
    if as_json:
        rows = [
            {
                "issue": card.issue,
                "claimant": card.claimant,
                "status": card.status,
                "task": card.task,
                "scope": list(card.scope),
                "stage": card.stage,
                "pr": card.pr,
                "claimed_at": iso_time(card.claimed_at),
                "last_activity": iso_time(card.activity),
                "deadline": iso_time(card.deadline(rules)),
                "warned": card.warned_at is not None,
                "adopted": card.adopted,
            }
            for card in cards
        ]
        print(json.dumps(rows, indent=2))
        return 0
    if not cards:
        print("no held claims")
    for card in cards:
        scope = ", ".join(card.scope) or "scope not stated"
        task = f" task {card.task}" if card.task else ""
        print(f"#{card.issue} {card.claimant} {card.status}{task}: {scope}; expires {show_time(card.deadline(rules))}")
    return 0
