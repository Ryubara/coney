# SPDX-License-Identifier: GPL-3.0-or-later
"""What one claim pass holds while it works: the stored cards of an issue and the exceptions and results around them.

Shared by the engine (`issue_claim_engine.py`) and the sweep's limit enforcement (`issue_claim_limits.py`).
"""

from __future__ import annotations

from dataclasses import dataclass, field
from datetime import datetime

from coney_tools.config import ConfigError
from coney_tools.conventions import Claims
from coney_tools.issue_claim_model import Card, is_card, parse_card
from coney_tools.issue_claim_remote import Comment, IssueView


class Stale(ConfigError):
    """Another run wrote a card of this issue after this pass read it: the pass starts over from fresh data.

    When the passes run out, `lines` holds what each attempt did.
    """

    def __init__(self, message: str, lines: list[str] | None = None) -> None:
        super().__init__(message)
        self.lines = lines or []


@dataclass
class Stored:
    """A card as it stands on GitHub: the comment that holds it (None until it is first posted) and its text."""

    comment_id: int | None
    card: Card
    text: str = ""


@dataclass(frozen=True)
class Activity:
    """A sign of life the triggering event reports: a pull request opened, reopened, made ready or pushed to."""

    at: datetime
    #: The login of the pull request's author; only the claimant's (or a maintainer's) pull request counts.
    by: str


@dataclass
class Pass:
    """What one `reconcile` of one issue holds while it works."""

    issue: IssueView
    comments: list[Comment]
    stored: list[Stored]
    active: Stored | None
    log: list[str] = field(default_factory=list)
    #: The ids of the command comments acted on in this pass.
    processed: set[int] = field(default_factory=set)
    #: The claimant's own open pull request that is ready for review (not a draft): the idle clock waits for it.
    in_review: int | None = None


@dataclass
class SweepResult:
    """What a sweep did: one line per action and one per issue it could not finish."""

    lines: list[str] = field(default_factory=list)
    errors: list[str] = field(default_factory=list)


def stored_cards(comments: list[Comment], rules: Claims) -> list[Stored]:
    """The claim cards among comments: written by the bot, starting as a card, with a readable hidden JSON."""
    found: list[Stored] = []
    for comment in comments:
        if comment.login != rules.bot_login or not is_card(comment.body, rules):
            continue
        card = parse_card(comment.body, rules)
        if card is not None:
            found.append(Stored(comment.id, card, comment.body))
    return found


def latest_held(stored: list[Stored]) -> Stored | None:
    """The held card of an issue: the newest card, if it is held."""
    held = [item for item in stored if item.card.active]
    return held[-1] if held else None
