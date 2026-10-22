# SPDX-License-Identifier: GPL-3.0-or-later
"""The checks a `/claim` passes, in order; the first one that fails is the answer.

The Action (`issue_claim_engine.py`) runs them when it handles a `/claim`, and `task create` runs the same function
first, so a claim that cannot succeed fails in a second with the words the Action would have used. The limits, the
labels and the statuses come from `.github/conventions.yml` (`claims`); GitHub is read through `Remote`, and only as far
as the checks reach (the blockers are looked up last but one, so a refused claim costs few calls).
"""

from __future__ import annotations

from collections.abc import Iterable
from dataclasses import dataclass

from coney_tools.conventions import Claims
from coney_tools.issue_claim_model import Card, Command, blocked_by_numbers
from coney_tools.issue_claim_remote import IssueView, Remote
from coney_tools.issue_form import section_text


@dataclass(frozen=True)
class Refusal:
    """Why a claim is refused, in words for the person who asked."""

    reason: str


def is_maintainer(association: str, rules: Claims) -> bool:
    """Whether a commenter's GitHub `author_association` makes them a maintainer."""
    return association in rules.maintainer_associations


def limit_for(association: str, rules: Claims) -> int:
    """How many open claims the commenter may hold: 3 for a maintainer, 2 for a contributor."""
    return rules.limits.maintainer if is_maintainer(association, rules) else rules.limits.contributor


def active_cards(cards: Iterable[Card]) -> list[Card]:
    """The held claims among `cards`, one per issue (the newest of an issue's cards, if it is held)."""
    newest: dict[int, Card] = {}
    for card in cards:
        newest[card.issue] = card
    return [card for card in newest.values() if card.active]


def open_task_keys(cards: Iterable[Card], claimant: str, except_issue: int) -> set[str]:
    """The tasks `claimant` holds open (issues of one task count once), not counting issue `except_issue`."""
    return {card.task_key for card in cards if card.active and card.claimant == claimant and card.issue != except_issue}


def check_claim(
    command: Command,
    issue: IssueView,
    claimant: str,
    association: str,
    active: Card | None,
    held: Iterable[Card],
    remote: Remote,
    rules: Claims,
    blocked_heading: str | None,
) -> Refusal | None:
    """Run the checks of a `/claim` by `claimant` on `issue`; None when the claim is allowed.

    `active` is the issue's held card (or None) and `held` every held card of the repository, in oldest-first order of
    their issues, which the limit and the `P-critical` rule read. The order is the one the guide lists.
    """
    held = list(held)
    if issue.is_pr:
        return Refusal("that is a pull request; only issues can be claimed")
    if issue.state != "open":
        return Refusal(f"#{issue.number} is closed")
    if active is not None and active.claimant != claimant:
        return Refusal(f"#{issue.number} is already claimed by @{active.claimant}")
    if issue.assignees and claimant not in issue.assignees:
        return Refusal(f"#{issue.number} is already assigned to @{issue.assignees[0]}")
    if issue.assignees or any(label in issue.labels for label in (rules.working_label, rules.review_label)):
        return Refusal(f"#{issue.number} is in progress already; ask a maintainer if the claim is stale")
    if rules.claimable_status not in issue.labels:
        shown = ", ".join(f"`{label}`" for label in issue.labels if label.startswith("S-")) or "no status label"
        return Refusal(
            f"only an issue labelled `{rules.claimable_status}` can be claimed (this one has {shown}); "
            "a maintainer triages it first"
        )
    if rules.critical_label not in issue.labels:
        unclaimed = _unclaimed_critical(remote, rules, {card.issue for card in held})
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
    others = open_task_keys(held, claimant, issue.number)
    limit = limit_for(association, rules)
    if own not in others and len(others) >= limit:
        return Refusal(
            f"@{claimant} already holds {len(others)} open claim(s) ({limit} at most for "
            f"{'a maintainer' if is_maintainer(association, rules) else 'a contributor'}): "
            "finish or `/unclaim` one first"
        )
    return None


def _unclaimed_critical(remote: Remote, rules: Claims, claimed: set[int]) -> list[int]:
    """The open `P-critical` issues with no card and no assignee."""
    found: list[int] = []
    for number in remote.open_with_label(rules.critical_label):
        if number in claimed:
            continue
        if not remote.issue(number).assignees:
            found.append(number)
    return found
