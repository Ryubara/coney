# SPDX-License-Identifier: GPL-3.0-or-later
"""The checks a `/claim` passes, in order; the first one that fails is the answer.

The Action (`issue_claim_engine.py`) runs them when it handles a `/claim`, and `task create` runs the same function
first, so a claim that cannot succeed fails in a second with the words the Action would have used. The limits, the
labels and the statuses come from `.github/conventions.yml` (`claims`); GitHub is read through `Remote`, and only as far
as the checks reach (the blockers are looked up last but one, so a refused claim costs few calls).

Runs for different issues can overlap, so the engine asks `overrun` and `critical_first` again after it assigns, on
fresh data. A refusal names another person without an `@`: only the person who wrote the command is mentioned.
"""

from __future__ import annotations

from collections.abc import Iterable
from dataclasses import dataclass
from datetime import datetime

from coney_tools.conventions import Claims
from coney_tools.issue_claim_model import Card, Command, blocked_by_numbers
from coney_tools.issue_claim_remote import IssueView, Remote
from coney_tools.issue_form import section_text


@dataclass(frozen=True)
class Refusal:
    """Why a claim is refused, in words for the person who asked."""

    reason: str


def is_maintainer(permission: str | None, association: str, rules: Claims) -> bool:
    """Whether a commenter is a maintainer: the repository permission (write or above) when GitHub says it, else, only
    when it will not, the comment's own `author_association` (the repository's OWNER)."""
    if permission is not None:
        return permission in rules.maintainer_permissions
    return association in rules.maintainer_associations


def limit_for(maintainer: bool, rules: Claims) -> int:
    """How many open claims one account may hold: 3 for a maintainer, 2 for a contributor."""
    return rules.limits.maintainer if maintainer else rules.limits.contributor


def active_cards(cards: Iterable[Card]) -> list[Card]:
    """The held claims among `cards`, one per issue (the newest of an issue's cards, if it is held)."""
    newest: dict[int, Card] = {}
    for card in cards:
        newest[card.issue] = card
    return [card for card in newest.values() if card.active]


def open_task_keys(cards: Iterable[Card], claimant: str, except_issue: int) -> set[str]:
    """The tasks `claimant` holds open that count toward the limit (issues of one task count once; an exempt claim
    does not count), not counting issue `except_issue`."""
    return {
        card.task_key
        for card in cards
        if card.active and not card.exempt and card.claimant == claimant and card.issue != except_issue
    }


def issues_in_task(cards: Iterable[Card], claimant: str, task: str, except_issue: int) -> int:
    """How many issues `claimant` holds under the task id `task`, not counting issue `except_issue`."""
    return sum(1 for c in cards if c.active and c.claimant == claimant and c.task == task and c.issue != except_issue)


def check_claim(
    command: Command,
    issue: IssueView,
    claimant: str,
    maintainer: bool,
    active: Card | None,
    held: Iterable[Card],
    remote: Remote,
    rules: Claims,
    blocked_heading: str | None,
) -> Refusal | None:
    """Run the checks of a `/claim` by `claimant` on `issue`; None when the claim is allowed.

    `active` is the issue's held card (or None) and `held` every held card of the repository, which the limit and the
    `P-critical` rule read. The order is the one the guide lists.
    """
    held = list(held)
    if issue.is_pr:
        return Refusal("that is a pull request; only issues can be claimed")
    if issue.state != "open":
        return Refusal(f"#{issue.number} is closed")
    if active is not None and active.claimant != claimant:
        return Refusal(f"#{issue.number} is already claimed by {active.claimant}")
    if issue.assignees and claimant not in issue.assignees:
        return Refusal(f"#{issue.number} is already assigned to {issue.assignees[0]}")
    if issue.assignees or any(label in issue.labels for label in (rules.working_label, rules.review_label)):
        return Refusal(f"#{issue.number} is in progress already; ask a maintainer if the claim is stale")
    if rules.claimable_status not in issue.labels:
        shown = ", ".join(f"`{label}`" for label in issue.labels if label.startswith("S-")) or "no status label"
        return Refusal(
            f"only an issue labelled `{rules.claimable_status}` can be claimed (this one has {shown}); "
            "a maintainer triages it first"
        )
    if rules.critical_label not in issue.labels:
        unclaimed = unclaimed_critical(remote, rules, {card.issue for card in held}, issue.number)
        if unclaimed:
            names = ", ".join(f"#{number}" for number in unclaimed)
            return Refusal(
                f"`{rules.critical_label}` comes first: {names} has no claim yet, and only it can be claimed"
            )
    if blocked_heading is not None:
        text = section_text(issue.body, blocked_heading)
        blockers = [remote.blocker(number) for number in blocked_by_numbers(text or "")]
        open_ones = [blocker for blocker in blockers if not blocker.done]
        if open_ones:
            names = ", ".join(f"#{blocker.number} ({blocker.why})" for blocker in open_ones)
            return Refusal(f"blocked by {names}: finish them first (a task is never stacked on an unmerged one)")
    own = command.task or f"issue-{issue.number}"
    if command.task is not None:
        in_task = issues_in_task(held, claimant, command.task, issue.number)
        if in_task >= rules.limits.issues_per_task:
            return Refusal(
                f"the task `{command.task}` already holds {in_task} of your issues "
                f"({rules.limits.issues_per_task} at most): a task id is not a way round the limit of open claims"
            )
    if rules.exempt_label in issue.labels:
        return None  # a workflow unit: outside the open-claims limit
    others = open_task_keys(held, claimant, issue.number)
    limit = limit_for(maintainer, rules)
    if own not in others and len(others) >= limit:
        return Refusal(
            f"@{claimant} already holds {len(others)} open claim(s) ({limit} at most for "
            f"{'a maintainer' if maintainer else 'a contributor'}): finish or `/unclaim` one first"
        )
    return None


def unclaimed_critical(remote: Remote, rules: Claims, claimed: set[int], except_issue: int = 0) -> list[int]:
    """The open `P-critical` issues that can be claimed (ready status) and have no card and no assignee.

    A `P-critical` issue that is blocked, waiting for information or already held does not stop anyone.
    """
    found: list[int] = []
    for number in remote.open_with_label(rules.critical_label):
        if number in claimed or number == except_issue:
            continue
        view = remote.issue(number)
        if view.state == "open" and not view.assignees and rules.claimable_status in view.labels:
            found.append(number)
    return found


def overrun(card: Card, held: Iterable[Card], maintainer: bool, rules: Claims) -> str | None:
    """Whether `card`, just made, is over the limit on fresh data: the reason, or None.

    The claimant's tasks are ordered by the time of their first claim; those past the limit are over, and so are the
    issues of a task past `issues_per_task`. Every parallel run applies the same order, so each releases its own claim
    if (and only if) it is among the late ones. Adopted claims (assigned by hand) are not counted, nor are exempt ones
    (`claims.exempt_label`): an exempt claim is never over the limit, but the cap of a task holds for it too, as
    `check_claim`'s `issues_in_task` counts it at claim time.
    """
    mine = [c for c in held if c.active and c.claimant == card.claimant and not c.adopted]
    if card not in mine and card.issue not in {c.issue for c in mine}:
        mine.append(card)
    if not card.exempt:
        first: dict[str, tuple[datetime, int]] = {}
        for c in mine:
            if c.exempt:
                continue
            key = c.task_key
            mark = (c.claimed_at, c.issue)
            if key not in first or mark < first[key]:
                first[key] = mark
        ordered = sorted(first, key=lambda key: first[key])
        limit = limit_for(maintainer, rules)
        if card.task_key not in ordered[:limit]:
            return f"{card.claimant} would hold more than {limit} open claims"
    if card.task is not None:
        same = sorted((c.claimed_at, c.issue) for c in mine if c.task == card.task)
        if (card.claimed_at, card.issue) not in same[: rules.limits.issues_per_task]:
            return f"the task `{card.task}` would hold more than {rules.limits.issues_per_task} issues"
    return None
