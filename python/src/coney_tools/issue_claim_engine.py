# SPDX-License-Identifier: GPL-3.0-or-later
"""Claiming an issue, applied: `reconcile` one issue and `sweep` all of them (docs/guides/issues.md#claiming).

`reconcile(issue)` is the one function every trigger runs (a comment, an issue or pull request event, the hourly
sweep). It reads the issue from GitHub, acts on every command comment nobody has handled yet (oldest first, so the
earliest `/claim` wins and a run GitHub dropped is covered by the next one), then rebuilds the claim's state from
GitHub (assignee, labels, linked pull requests) and repairs what drifted, and last applies the deadlines: a warning
at `claims.warn_hours` of idleness and the release at `claims.deadline_hours`. Every step is safe to repeat.

The card keeps only what GitHub cannot tell later (the claim's time, scope, task, last stage and activity, the ids of
handled commands); the claimant, the status and the pull request are recomputed on every pass.
"""

from __future__ import annotations

from collections.abc import Callable
from dataclasses import dataclass, field, replace
from datetime import datetime, timedelta

from coney_tools.config import ConfigError
from coney_tools.conventions import Claims, Conventions
from coney_tools.issue_claim_model import (
    Card,
    Command,
    Rejected,
    handled_with,
    is_card,
    parse_card,
    parse_command,
    render_card,
    reply_first_line,
    reply_target,
    show_time,
)
from coney_tools.issue_claim_remote import Comment, IssueView, Remote
from coney_tools.issue_claim_rules import active_cards, check_claim, is_maintainer
from coney_tools.issue_form import section_heading


@dataclass
class Stored:
    """A card as it stands on GitHub: the comment that holds it (None until it is first posted) and its text."""

    comment_id: int | None
    card: Card
    text: str = ""


@dataclass
class _Pass:
    """What one `reconcile` of one issue holds while it works."""

    issue: IssueView
    comments: list[Comment]
    stored: list[Stored]
    active: Stored | None
    log: list[str] = field(default_factory=list)


@dataclass
class SweepResult:
    """What a sweep did: one line per action and one per issue it could not finish."""

    lines: list[str] = field(default_factory=list)
    errors: list[str] = field(default_factory=list)


class Engine:
    """The claim logic over a `Remote`, with an injected clock; nothing else is global."""

    def __init__(self, remote: Remote, conventions: Conventions, now: Callable[[], datetime]) -> None:
        self.remote = remote
        self.rules: Claims = conventions.claims
        self._status_prefix = conventions.labels.status_prefix
        self._blocked_heading = section_heading(conventions, "blocked-by")
        self._now = now
        self._held: dict[int, Card] | None = None

    # -- the held claims of the repository, read once and kept current as cards are written

    def held_cards(self) -> dict[int, Card]:
        """The held claims of the repository by issue number, read from the cards on GitHub on first use."""
        if self._held is None:
            newest: dict[int, Card] = {}
            for comment in sorted(self.remote.all_cards(), key=lambda item: item.id):
                card = parse_card(comment.body, self.rules)
                if card is not None and comment.login == self.rules.bot_login:
                    newest[card.issue] = card
            self._held = {card.issue: card for card in active_cards(newest.values())}
        return self._held

    def _stored_cards(self, comments: list[Comment]) -> list[Stored]:
        """The cards among an issue's comments (the bot's, in comment order)."""
        return stored_cards(comments, self.rules)

    # -- one issue

    def reconcile(self, number: int, push_at: datetime | None = None) -> list[str]:
        """Bring issue `number` in line with its commands, its GitHub state and the deadlines; the log lines.

        `push_at` is the moment of a push to the issue's pull request that the triggering event reports.
        """
        issue = self.remote.issue(number)
        if issue.is_pr:
            return []
        comments = self.remote.comments(number)
        stored = self._stored_cards(comments)
        work = _Pass(issue, comments, stored, latest_held(stored))
        self._refresh_held(work)
        self._commands(work)
        self._sync(work, push_at)
        return work.log

    def _refresh_held(self, work: _Pass) -> None:
        """Make the repository-wide cache agree with what this issue's comments just showed."""
        held = self.held_cards()
        if work.active is None:
            held.pop(work.issue.number, None)
        else:
            held[work.issue.number] = work.active.card

    # -- commands

    def _commands(self, work: _Pass) -> None:
        """Act on every command comment not yet handled, oldest first, and delete each afterwards."""
        done = {item for stored in work.stored for item in stored.card.handled}
        for comment in work.comments:
            target = reply_target(comment.body, self.rules)
            if target is not None and comment.login == self.rules.bot_login:
                done.add(target)
        for comment in sorted(work.comments, key=lambda item: (item.created_at, item.id)):
            if comment.user_type == "Bot" or comment.login == self.rules.bot_login or comment.id in done:
                continue
            command = parse_command(comment.body, self.rules)
            if command is None:
                continue
            if isinstance(command, Rejected):
                self._reply(work, comment, command.reason)
            elif command.kind == "claim":
                self._claim(work, comment, command)
            elif command.kind == "renew":
                self._renew(work, comment)
            elif command.kind == "progress":
                self._progress(work, comment, command)
            else:
                self._unclaim(work, comment)
            if self.remote.delete_comment(comment.id):
                work.log.append(f"#{work.issue.number}: deleted the command comment {comment.id}")
            else:
                work.log.append(f"#{work.issue.number}: could not delete the command comment {comment.id}")

    def _reply(self, work: _Pass, comment: Comment, reason: str) -> None:
        """Answer a refused command with a bot comment that names the command comment's id."""
        text = f"{reply_first_line(self.rules, comment.id)}\nNot done for {comment.login}: {reason}"
        self.remote.create_comment(work.issue.number, text)
        work.log.append(f"#{work.issue.number}: refused {comment.login}: {reason}")

    def _claim(self, work: _Pass, comment: Comment, command: Command) -> None:
        """`/claim`: a scope or task update on the claimant's own claim, or a new claim after the checks."""
        number = work.issue.number
        active = work.active
        if active is not None and active.card.claimant == comment.login:
            if command.scope is None and command.task is None:
                self._reply(work, comment, "that claim is already yours; `/claim renew` starts the clock again")
                return
            card = active.card
            changed = card.changed(
                scope=command.scope if command.scope is not None else card.scope,
                task=command.task or card.task,
                last_activity=comment.created_at,
                last_activity_why="scope",
                warned_at=None,
                warning_comment=None,
                handled=handled_with(card, comment.id),
            )
            self._drop_warning(work, card)
            self._save(work, active, changed)
            work.log.append(f"#{number}: {comment.login} changed the scope or task")
            return
        refusal = check_claim(
            command,
            work.issue,
            comment.login,
            comment.association,
            active.card if active else None,
            self.held_cards().values(),
            self.remote,
            self.rules,
            self._blocked_heading,
        )
        if refusal is not None:
            self._reply(work, comment, refusal.reason)
            return
        assigned = self.remote.assign(number, comment.login)
        if comment.login not in assigned:
            self._reply(
                work,
                comment,
                f"GitHub did not assign @{comment.login} (it only assigns people who can be assigned); "
                "a maintainer can add you as a collaborator",
            )
            return
        self._set_status_label(work, self.rules.working_label)
        card = Card(
            issue=number,
            claimant=comment.login,
            claimed_at=comment.created_at,
            scope=command.scope or (),
            task=command.task,
            handled=(comment.id,),
        )
        fresh = Stored(None, card)
        work.stored.append(fresh)
        work.active = fresh
        self._save(work, fresh, card)
        work.issue = self.remote.issue(number)
        work.log.append(f"#{number}: {comment.login} claimed it")

    def _renew(self, work: _Pass, comment: Comment) -> None:
        """`/claim renew`: the claimant starts the idle clock again."""
        active = work.active
        if active is None or active.card.claimant != comment.login:
            self._reply(work, comment, "only the claimant can renew a claim, and this issue has none for you")
            return
        card = active.card
        self._drop_warning(work, card)
        self._save(
            work,
            active,
            card.changed(
                last_activity=comment.created_at,
                last_activity_why="renew",
                warned_at=None,
                warning_comment=None,
                handled=handled_with(card, comment.id),
            ),
        )
        work.log.append(f"#{work.issue.number}: {comment.login} renewed the claim")

    def _progress(self, work: _Pass, comment: Comment, command: Command) -> None:
        """`/progress <stage>`: the claimant records a stage, which is activity."""
        active = work.active
        if active is None or active.card.claimant != comment.login:
            self._reply(work, comment, "only the claimant can record progress, and this issue has none for you")
            return
        card = active.card
        self._drop_warning(work, card)
        self._save(
            work,
            active,
            card.changed(
                stage=command.stage,
                last_activity=comment.created_at,
                last_activity_why="progress",
                warned_at=None,
                warning_comment=None,
                handled=handled_with(card, comment.id),
            ),
        )
        work.log.append(f"#{work.issue.number}: {comment.login} reached {command.stage}")

    def _unclaim(self, work: _Pass, comment: Comment) -> None:
        """`/unclaim`: the claimant, or a maintainer, gives the claim up."""
        active = work.active
        if active is None:
            self._reply(work, comment, "this issue has no claim to give up")
            return
        if comment.login != active.card.claimant and not is_maintainer(comment.association, self.rules):
            self._reply(work, comment, f"only @{active.card.claimant} or a maintainer can give this claim up")
            return
        card = active.card.changed(handled=handled_with(active.card, comment.id))
        by = "" if comment.login == card.claimant else f" by @{comment.login}"
        self._release(work, active, card, f"unclaimed{by}", "released")

    # -- keeping the card and the issue true

    def _drop_warning(self, work: _Pass, card: Card) -> None:
        """Delete the warning comment of a card, if it has one."""
        if card.warning_comment is not None:
            self.remote.delete_comment(card.warning_comment)
            work.log.append(f"#{work.issue.number}: removed the expiry warning")

    def _set_status_label(self, work: _Pass, want: str) -> None:
        """Leave exactly one status label on the issue: `want`."""
        number = work.issue.number
        for label in work.issue.labels:
            if label.startswith(self._status_prefix) and label != want:
                self.remote.remove_label(number, label)
        if want not in work.issue.labels:
            self.remote.add_label(number, want)
        kept = tuple(label for label in work.issue.labels if not label.startswith(self._status_prefix))
        work.issue = replace(work.issue, labels=(*kept, want))

    def _save(self, work: _Pass, stored: Stored, card: Card) -> None:
        """Write `card` into its comment (post it first if it is new) when its text changed, and keep the cache."""
        held = self.held_cards()
        if card.active:
            held[card.issue] = card
        else:
            held.pop(card.issue, None)
        mine = tuple(sorted(number for number, other in held.items() if other.claimant == card.claimant))
        card = card.changed(open_claims=mine) if card.active else card
        text = render_card(card, self.rules)
        if stored.comment_id is None:
            stored.comment_id = self.remote.create_comment(card.issue, text)
        elif text != stored.text:
            self.remote.edit_comment(stored.comment_id, text)
        stored.card, stored.text = card, text
        if work.active is stored and not card.active:
            work.active = None

    def _release(
        self, work: _Pass, stored: Stored, card: Card, why: str, status: str, keep_assignees: bool = False
    ) -> None:
        """End a claim: unassign, hand the issue back to `claims.released_label`, remove the warning, mark the card.

        `keep_assignees` leaves the assignees and the labels alone (somebody else was assigned by hand).
        """
        number = work.issue.number
        self._drop_warning(work, card)
        if not keep_assignees:
            if work.issue.assignees:
                self.remote.unassign(number, work.issue.assignees)
            if work.issue.state == "open":
                self._set_status_label(work, self.rules.released_label)
        self._save(
            work,
            stored,
            card.changed(
                status=status, released_at=self._now(), released_why=why, warned_at=None, warning_comment=None
            ),
        )
        work.log.append(f"#{number}: claim of {card.claimant} ended ({why})")

    # -- rebuilding the state from GitHub

    def _sync(self, work: _Pass, push_at: datetime | None) -> None:
        """Rebuild the card from the issue's assignees, labels and pull requests, then apply the deadlines."""
        number = work.issue.number
        work.issue = self.remote.issue(number)
        self._settle_race(work)
        if work.issue.state != "open":
            self._close(work)
            return
        active = work.active
        if active is not None:
            card = active.card
            if card.claimant not in work.issue.assignees:
                self._release(
                    work, active, card, "unassigned by hand", "released", keep_assignees=bool(work.issue.assignees)
                )
                active = None
            else:
                extras = tuple(login for login in work.issue.assignees if login != card.claimant)
                if extras:
                    self.remote.unassign(number, extras)
                    work.log.append(f"#{number}: removed {', '.join(extras)}: one claimant per issue")
        if active is None:
            self._adopt(work)
            active = work.active
        if active is None:
            return
        self._follow_pull_request(work, active, push_at)
        self._deadlines(work, active)

    def _settle_race(self, work: _Pass) -> None:
        """Two held claims on one issue (a dropped run, a hand assignment): the earliest `claimed_at` keeps it."""
        held = [stored for stored in work.stored if stored.card.active]
        if len(held) < 2:
            work.active = held[0] if held else None
            return
        held.sort(key=lambda stored: (stored.card.claimed_at, stored.comment_id or 0))
        winner = held[0]
        for loser in held[1:]:
            if loser.card.claimant in work.issue.assignees and loser.card.claimant != winner.card.claimant:
                self.remote.unassign(work.issue.number, (loser.card.claimant,))
            self._save(
                work,
                loser,
                loser.card.changed(
                    status="lost",
                    released_at=self._now(),
                    released_why=f"lost to @{winner.card.claimant}",
                    warned_at=None,
                    warning_comment=None,
                ),
            )
            work.log.append(f"#{work.issue.number}: {loser.card.claimant} lost the claim to {winner.card.claimant}")
        work.active = winner
        work.issue = self.remote.issue(work.issue.number)

    def _close(self, work: _Pass) -> None:
        """A closed issue's held claim ends: merged when a pull request closed it, else released."""
        active = work.active
        if active is None:
            return
        merged = [pr for pr in self.remote.linked_prs(work.issue.number) if pr.state == "merged"]
        card = active.card.changed(pr=merged[0].number if merged else active.card.pr)
        if merged:
            self._drop_warning(work, card)
            self._save(
                work,
                active,
                card.changed(status="merged", released_at=self._now(), warned_at=None, warning_comment=None),
            )
            work.log.append(f"#{work.issue.number}: merged, claim of {card.claimant} done")
        else:
            self._release(work, active, card, "the issue was closed", "released")

    def _adopt(self, work: _Pass) -> None:
        """An open issue assigned by hand gets a card, so the deadline covers it too."""
        assignees = tuple(login for login in work.issue.assignees if login != self.rules.bot_login)
        if not assignees or work.issue.state != "open":
            return
        if len(assignees) > 1:
            self.remote.unassign(work.issue.number, assignees[1:])
        card = Card(
            issue=work.issue.number,
            claimant=assignees[0],
            claimed_at=self._now(),
            adopted=True,
            last_activity_why="assigned by hand",
        )
        fresh = Stored(None, card)
        work.stored.append(fresh)
        work.active = fresh
        self._set_status_label(work, self.rules.working_label)
        self._save(work, fresh, card)
        work.log.append(f"#{work.issue.number}: adopted the hand assignment to {assignees[0]}")

    def _follow_pull_request(self, work: _Pass, stored: Stored, push_at: datetime | None) -> None:
        """Status, label and activity follow the open pull request that closes the issue."""
        card = stored.card
        open_pr = next((pr for pr in self.remote.linked_prs(work.issue.number) if pr.state == "open"), None)
        changes: dict[str, object] = {
            "status": "pr-open" if open_pr else "claimed",
            "pr": open_pr.number if open_pr else None,
        }
        pushed = [moment for moment in (push_at, open_pr.head_date if open_pr else None) if moment is not None]
        if pushed and max(pushed) > card.activity:
            changes.update(last_activity=max(pushed), last_activity_why="push")
        self._set_status_label(work, self.rules.review_label if open_pr else self.rules.working_label)
        self._save(work, stored, card.changed(**changes))

    def _deadlines(self, work: _Pass, stored: Stored) -> None:
        """The warning at `claims.warn_hours` of idleness, the release at `claims.deadline_hours`."""
        card = stored.card
        now = self._now()
        if now >= card.deadline(self.rules):
            self._release(work, stored, card, f"idle {self.rules.deadline_hours} h", "released")
        elif now >= card.warn_at(self.rules):
            if card.warned_at is None:
                left = max(1, -(-int((card.deadline(self.rules) - now).total_seconds()) // 3600))
                comment = self.remote.create_comment(
                    work.issue.number,
                    f"@{card.claimant} your claim of this issue expires in about {left} h "
                    f"({show_time(card.deadline(self.rules))}): record `/progress <stage>`, push to its pull request "
                    "or write `/claim renew` to keep it.",
                )
                self._save(work, stored, card.changed(warned_at=now, warning_comment=comment))
                work.log.append(f"#{work.issue.number}: warned {card.claimant}")
        elif card.warning_comment is not None or card.warned_at is not None:
            self._drop_warning(work, card)
            self._save(work, stored, card.changed(warned_at=None, warning_comment=None))

    # -- the sweep

    def sweep(self) -> SweepResult:
        """Reconcile every issue that may need it, then delete expired refusal replies; never stops on one error."""
        result = SweepResult()
        now = self._now()
        candidates: set[int] = set(self.held_cards())
        since = now - timedelta(hours=self.rules.command_lookback_hours)
        candidates.update(comment.issue for comment in self.remote.recent_commands(since))
        candidates.update(self.remote.open_with_label(self.rules.working_label))
        candidates.update(self.remote.open_with_label(self.rules.review_label))
        candidates.update(self.remote.open_assigned())
        for number in sorted(candidates):
            try:
                result.lines.extend(self.reconcile(number))
            except ConfigError as error:
                result.errors.append(f"#{number}: {error}")
        try:
            result.lines.extend(self._clean_replies(now))
        except ConfigError as error:
            result.errors.append(f"replies: {error}")
        return result

    def _clean_replies(self, now: datetime) -> list[str]:
        """Delete refusal replies older than `claims.reply_keep_hours` whose command comment is gone."""
        keep = timedelta(hours=self.rules.reply_keep_hours)
        by_issue: dict[int, list[Comment]] = {}
        for reply in self.remote.all_replies():
            if reply.login == self.rules.bot_login and now - reply.created_at >= keep:
                by_issue.setdefault(reply.issue, []).append(reply)
        lines: list[str] = []
        for number, replies in sorted(by_issue.items()):
            present = {comment.id for comment in self.remote.comments(number)}
            for reply in replies:
                if reply_target(reply.body, self.rules) not in present and self.remote.delete_comment(reply.id):
                    lines.append(f"#{number}: deleted the old reply {reply.id}")
        return lines


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
