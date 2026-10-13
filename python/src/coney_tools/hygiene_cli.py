# SPDX-License-Identifier: GPL-3.0-or-later
"""`coney-tools repo hygiene`: what the `hygiene` workflow runs. Writes the audit's findings back as comments.

`--event N` is for one issue or pull request that just changed: it audits GitHub, then creates, edits or resolves that
item's one comment. `--sweep` is the weekly run: it audits the checkout and GitHub, keeps the report comment on the
report issue up to date (creating the issue if it is missing) and, only when `hygiene.comments.on_sweep` is true, brings
every item's comment up to date too, at most `hygiene.comments.max_per_sweep` of them. Without `--apply` nothing is
written: the command prints the plan. The audit itself stays read only (`coney-tools repo audit`).

Both download the logs attached to the issues they look at (the event's issue, or every open issue for the sweep) and
scan them for possible game data and personal information (`log_scan`, `hygiene.attachments`); what the scan finds is
two more checks of the same comment.
"""

from __future__ import annotations

import argparse
from collections.abc import Callable
from datetime import UTC, datetime
from pathlib import Path

from coney_tools.audit_cli import build_report
from coney_tools.audit_github import Snapshot
from coney_tools.config import ConfigError, find_repo_root
from coney_tools.conventions import Conventions, load_conventions
from coney_tools.hygiene_comments import (
    Action,
    group_by_item,
    item_marker,
    plan_items,
    plan_report,
    render_report,
    report_issue_body,
    report_marker,
)
from coney_tools.hygiene_github import GhRemote, Remote
from coney_tools.log_scan import Fetch, Scans, download, scan_issues


def _scanner(fetch: Fetch, only: int | None = None) -> Callable[[Snapshot, Conventions], Scans]:
    """The attachment scan of the open issues of a snapshot, or of issue `only` alone (an event)."""

    def scan(snapshot: Snapshot, conventions: Conventions) -> Scans:
        issues = [issue for issue in snapshot.open_issues() if only is None or issue.number == only]
        return scan_issues({issue.number: issue.body for issue in issues}, conventions.hygiene.attachments, fetch)

    return scan


def add_hygiene_parser(repo_commands: argparse._SubParsersAction[argparse.ArgumentParser]) -> None:
    """Add `repo hygiene` and its options to the `repo` command group."""
    hygiene = repo_commands.add_parser(
        "hygiene",
        help="write the audit's findings as comments on issues and pull requests (what the hygiene workflow runs)",
        description=(
            "Audits GitHub and keeps one comment per item with findings, and one report comment on the report issue, "
            "up to date. Prints the plan; with --apply it writes the comments through gh. See "
            "hygiene.comments in .github/conventions.yml."
        ),
    )
    mode = hygiene.add_mutually_exclusive_group(required=True)
    mode.add_argument("--event", type=int, metavar="N", help="issue or pull request N changed: update its comment")
    mode.add_argument(
        "--sweep", action="store_true", help="the weekly run: update the report (and item comments if on)"
    )
    hygiene.add_argument("--apply", action="store_true", help="write the comments (default: print the plan only)")
    hygiene.add_argument("--snapshot", type=Path, metavar="FILE", help="read this saved audit snapshot, not GitHub")


def _describe(action: Action) -> str:
    """One plan line."""
    first = action.body.splitlines()[1] if len(action.body.splitlines()) > 1 else ""
    return f"{action.kind} comment on #{action.number}: {first}"


def _apply(remote: Remote, actions: list[Action], apply: bool) -> None:
    """Print each action and, with `apply`, carry it out."""
    for action in actions:
        print(("" if apply else "would ") + _describe(action))
        if not apply:
            continue
        if action.comment_id is None:
            remote.create_comment(action.number, action.body)
        else:
            remote.edit_comment(action.comment_id, action.body)


def run_event(
    conventions: Conventions, number: int, remote: Remote, args: argparse.Namespace, root: Path, fetch: Fetch
) -> int:
    """Update the comment of item `number` from a fresh audit of GitHub (and of its attached logs)."""
    comments = conventions.hygiene.comments
    if not comments.on_events:
        print("hygiene.comments.on_events is false: nothing to do")
        return 0
    report = build_report(root, conventions, ["github"], [], _audit_args(args), Path.cwd(), _scanner(fetch, number))
    for notice in report.notices:
        print(f"note: {notice}")
    stickies = remote.stickies(item_marker(comments), comments.author, number)
    actions = plan_items(comments, group_by_item(report.findings, comments), stickies, scope=[number])
    _apply(remote, actions, args.apply)
    if not actions:
        print(f"#{number}: its comment is up to date")
    return 0


def run_sweep(
    conventions: Conventions, remote: Remote, args: argparse.Namespace, root: Path, now: datetime, fetch: Fetch
) -> int:
    """Update the report comment, and the item comments when the conventions say the sweep may."""
    comments = conventions.hygiene.comments
    report = build_report(root, conventions, ["files", "github"], [], _audit_args(args), Path.cwd(), _scanner(fetch))
    body = render_report(comments, report, now.strftime("%Y-%m-%d %H:%M UTC"))
    title = conventions.hygiene.report_issue.title
    issue = remote.find_report_issue(title)
    if issue is None and not args.apply:
        print(f"would create the report issue titled {title!r} with a report comment of {len(body)} characters")
    else:
        # The report issue is the one thing the workflow creates besides comments, and only when it is missing.
        if issue is None:
            issue = remote.create_report_issue(title, report_issue_body(), conventions.hygiene.report_issue.labels)
            print(f"created the report issue #{issue}")
        sticky = remote.stickies(report_marker(comments), comments.author, issue).get(issue)
        action = plan_report(sticky, body, issue)
        _apply(remote, [action] if action else [], args.apply)
        if action is None:
            print("the report comment is up to date")
    if comments.on_sweep:
        stickies = remote.stickies(item_marker(comments), comments.author)
        groups = group_by_item(report.findings, comments)
        _apply(remote, plan_items(comments, groups, stickies, None, comments.max_per_sweep), args.apply)
    else:
        print("hygiene.comments.on_sweep is false: item comments come from events only")
    return 0


def _audit_args(args: argparse.Namespace) -> argparse.Namespace:
    """The options `build_report` reads, with only the snapshot set."""
    return argparse.Namespace(snapshot=args.snapshot, save_snapshot=None, workspace=None)


def run_hygiene(
    args: argparse.Namespace, remote: Remote | None = None, now: datetime | None = None, fetch: Fetch = download
) -> int:
    """Run `coney-tools repo hygiene`; `remote`, `now` and `fetch` (the attachment download) are replaced in tests."""
    root = find_repo_root(Path.cwd())
    conventions = load_conventions(root)
    github: Remote = remote if remote is not None else GhRemote()
    if args.event is not None:
        if args.event < 1:
            raise ConfigError("--event needs an issue or pull request number")
        return run_event(conventions, args.event, github, args, root, fetch)
    return run_sweep(conventions, github, args, root, now or datetime.now(UTC), fetch)
