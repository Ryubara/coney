# SPDX-License-Identifier: GPL-3.0-or-later
"""Shared by the claim tests: an in-memory GitHub behind the `Remote` interface, with a clock the test moves.

`FakeGitHub` keeps issues, comments (ids, authors, times), assignees, labels and linked pull requests, and can be made
to misbehave the way the real one may: a delete that is refused, a person who cannot be assigned. Nothing here touches
the network or really waits.
"""

from __future__ import annotations

from dataclasses import dataclass, field, replace
from datetime import UTC, datetime, timedelta

from audit_support import CONVENTIONS

from coney_tools.config import ConfigError
from coney_tools.conventions import Conventions
from coney_tools.issue_claim_engine import Engine
from coney_tools.issue_claim_model import command_shaped
from coney_tools.issue_claim_remote import Blocker, Comment, IssueView, LinkedPr

#: The conventions with adoption and release switched on, as they are from the cutover; the engine tests run on them.
LIVE = replace(CONVENTIONS, claims=replace(CONVENTIONS.claims, adopt=True, release=True))
RULES = LIVE.claims
START = datetime(2026, 10, 22, 12, 0, 0, tzinfo=UTC)
BOT = RULES.bot_login


class Clock:
    """A clock the test moves; `sleep` only moves it."""

    def __init__(self, start: datetime = START) -> None:
        self.current = start

    def now(self) -> datetime:
        """The current moment."""
        return self.current

    def advance(self, **delta: float) -> None:
        """Move the clock forward (hours=..., minutes=..., seconds=...)."""
        self.current += timedelta(**delta)

    def sleep(self, seconds: float) -> None:
        """Pretend to wait."""
        self.current += timedelta(seconds=seconds)


@dataclass
class Item:
    """An issue or pull request held by the fake."""

    view: IssueView
    prs: list[LinkedPr] = field(default_factory=list)


class FakeGitHub:
    """An in-memory GitHub; `actor` and `association` say who `post_command` posts as."""

    def __init__(self, clock: Clock) -> None:
        self.clock = clock
        self.items: dict[int, Item] = {}
        self.comment_list: dict[int, list[Comment]] = {}
        self.next_id = 1000
        self.actor = "Ryubara"
        self.association_of_actor = "OWNER"
        self.refuse_delete = False
        #: login -> repository permission; unlisted people have none, and `None` values mean GitHub will not say.
        self.permissions: dict[str, str | None] = {"Ryubara": "admin"}
        #: Raise on the next `add_label` (a claim that fails half way).
        self.fail_add_label = 0
        self.unassignable: set[str] = set()
        self.blockers: dict[int, Blocker] = {}
        self.writes: list[str] = []
        self.on_poll: list[object] = []

    # -- building the scene

    def add_issue(
        self,
        number: int,
        labels: tuple[str, ...] = ("task", "A-ai", "P-medium", "S-ready-to-start"),
        body: str = "",
        state: str = "open",
        assignees: tuple[str, ...] = (),
        reason: str | None = None,
        title: str = "gameplay: Fix the fight goal",
    ) -> None:
        """Add an open issue (ready to start by default)."""
        self.items[number] = Item(IssueView(number, title, state, reason, False, labels, assignees, body))
        self.comment_list.setdefault(number, [])

    def add_pr(self, number: int) -> None:
        """Add a pull request."""
        self.items[number] = Item(IssueView(number, "pr", "open", None, True, (), (), ""))

    def link_pr(
        self,
        issue: int,
        number: int,
        state: str = "open",
        head_date: datetime | None = None,
        author: str = "alice",
        draft: bool = False,
    ) -> None:
        """Make pull request `number` close `issue`."""
        self.items[issue].prs = [pr for pr in self.items[issue].prs if pr.number != number]
        self.items[issue].prs.append(LinkedPr(number, state, head_date, author, draft))

    def say(self, number: int, login: str, body: str, association: str = "NONE") -> int:
        """A person comments; returns the comment id."""
        self.next_id += 1
        self.comment_list[number].append(
            Comment(self.next_id, number, login, "User", association, body, self.clock.now())
        )
        return self.next_id

    def view(self, number: int) -> IssueView:
        """The issue as it stands."""
        return self.items[number].view

    def cards(self, number: int) -> list[Comment]:
        """The card comments of an issue."""
        return [c for c in self.comment_list[number] if c.login == BOT and c.body.startswith("<!-- coney-claim -->")]

    def replies(self, number: int) -> list[Comment]:
        """The refusal replies of an issue."""
        return [c for c in self.comment_list[number] if c.login == BOT and c.body.startswith("<!-- coney-claim-reply:")]

    def others(self, number: int) -> list[Comment]:
        """The bot comments that are neither card nor reply (the warnings)."""
        taken = {c.id for c in self.cards(number) + self.replies(number)}
        return [c for c in self.comment_list[number] if c.login == BOT and c.id not in taken]

    # -- the Remote interface

    def issue(self, number: int) -> IssueView:
        """The issue; a missing one is what gh reports as a failed read."""
        if number not in self.items:
            raise ConfigError(f"cannot read from GitHub with gh (gh api failed: Not Found #{number})")
        return self.items[number].view

    def comments(self, number: int) -> list[Comment]:
        """The comments of an issue."""
        return list(self.comment_list.get(number, []))

    def all_cards(self) -> list[Comment]:
        """Every card."""
        return [c for n in self.comment_list for c in self.cards(n)]

    def all_replies(self) -> list[Comment]:
        """Every refusal reply."""
        return [c for n in self.comment_list for c in self.replies(n)]

    def recent_commands(self, since: datetime) -> list[Comment]:
        """Command-shaped comments by people since `since`."""
        return [
            c
            for n in self.comment_list
            for c in self.comment_list[n]
            if c.user_type != "Bot" and c.created_at >= since and command_shaped(c.body)
        ]

    def open_with_label(self, label: str) -> list[int]:
        """Open issues with the label."""
        return [
            n for n, i in self.items.items() if not i.view.is_pr and i.view.state == "open" and label in i.view.labels
        ]

    def open_assigned(self) -> list[int]:
        """Open issues with an assignee."""
        return [n for n, i in self.items.items() if not i.view.is_pr and i.view.state == "open" and i.view.assignees]

    def blocker(self, number: int) -> Blocker:
        """Whether a blocker is done: set with `blockers`, else read from the fake issue."""
        if number in self.blockers:
            return self.blockers[number]
        view = self.items[number].view
        done = view.state == "closed"
        return Blocker(number, done, "closed by a merged pull request" if done else "open")

    def linked_prs(self, number: int) -> list[LinkedPr]:
        """The pull requests that close the issue."""
        return list(self.items[number].prs)

    def assign(self, number: int, login: str) -> tuple[str, ...]:
        """Assign unless the person cannot be assigned (GitHub then drops them silently)."""
        view = self.items[number].view
        if login not in self.unassignable and login not in view.assignees:
            self.items[number].view = replace(view, assignees=(*view.assignees, login))
        self.writes.append(f"assign {number} {login}")
        return self.items[number].view.assignees

    def unassign(self, number: int, logins: tuple[str, ...]) -> None:
        """Remove assignees."""
        view = self.items[number].view
        self.items[number].view = replace(view, assignees=tuple(a for a in view.assignees if a not in logins))
        self.writes.append(f"unassign {number} {','.join(logins)}")

    def add_label(self, number: int, label: str) -> None:
        """Add a label (or fail, `fail_add_label` times)."""
        if self.fail_add_label:
            self.fail_add_label -= 1
            raise ConfigError("cannot write to GitHub with gh (gh api failed: boom)")
        view = self.items[number].view
        if label not in view.labels:
            self.items[number].view = replace(view, labels=(*view.labels, label))
        self.writes.append(f"label+ {number} {label}")

    def remove_label(self, number: int, label: str) -> None:
        """Remove a label."""
        view = self.items[number].view
        self.items[number].view = replace(view, labels=tuple(x for x in view.labels if x != label))
        self.writes.append(f"label- {number} {label}")

    def create_comment(self, number: int, body: str) -> int:
        """The bot comments."""
        self.next_id += 1
        self.comment_list[number].append(Comment(self.next_id, number, BOT, "Bot", "NONE", body, self.clock.now()))
        self.writes.append(f"comment {number}")
        return self.next_id

    def edit_comment(self, comment_id: int, body: str) -> None:
        """Edit a comment."""
        for number, items in self.comment_list.items():
            for index, item in enumerate(items):
                if item.id == comment_id:
                    self.comment_list[number][index] = replace(item, body=body)
        self.writes.append(f"edit {comment_id}")

    def delete_comment(self, comment_id: int) -> bool:
        """Delete a comment unless `refuse_delete`."""
        if self.refuse_delete:
            return False
        for number in self.comment_list:
            self.comment_list[number] = [c for c in self.comment_list[number] if c.id != comment_id]
        self.writes.append(f"delete {comment_id}")
        return True

    def post_command(self, number: int, body: str) -> int:
        """The signed-in person comments."""
        return self.say(number, self.actor, body, self.association_of_actor)

    def current_user(self) -> str:
        """The signed-in account."""
        return self.actor

    def permission(self, login: str) -> str | None:
        """The repository permission of a person: `none` unless listed in `permissions`."""
        return self.permissions.get(login, "none")


def make_engine(github: FakeGitHub, clock: Clock, conventions: Conventions = LIVE) -> Engine:
    """An engine over the fake with the fake clock; by default with adoption and release on."""
    return Engine(github, conventions, clock.now)


def plant_claim(
    github: FakeGitHub, number: int, login: str, at: datetime | None = None, task: str | None = None
) -> None:
    """A claim another run made: the assignee, the working label and a card, without going through the engine."""
    from coney_tools.issue_claim_model import Card, render_card

    view = github.items[number].view
    kept = tuple(label for label in view.labels if not label.startswith("S-"))
    github.items[number].view = replace(view, assignees=(login,), labels=(*kept, RULES.working_label))
    card = Card(issue=number, claimant=login, claimed_at=at or github.clock.now(), task=task)
    github.create_comment(number, render_card(card, RULES))
