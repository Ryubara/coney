# SPDX-License-Identifier: GPL-3.0-or-later
"""Claiming an issue, applied: `reconcile` one issue and `sweep` all of them (docs/guides/issues.md#claiming).

`reconcile(issue)` is the one function every trigger runs (a comment, an issue or pull request event, the hourly
sweep). It reads the issue from GitHub, acts on every command comment nobody has handled yet (oldest first, so the
earliest `/claim` wins and a run GitHub dropped is covered by the next one), then rebuilds the claim's state from
GitHub (assignee, labels, linked pull requests) and repairs what drifted, and last applies the deadlines: a warning
at `claims.warn_hours` of idleness and the release at `claims.deadline_hours`. Every step is safe to repeat.

`claim.yml` runs one claim job at a time for the whole repository, so two runs never act on one issue together.
A backstop stays for whatever else writes (a person editing a card, a run started by hand): before the first change
of each step and before every card write the pass reads the issue's cards again, and when one changed since it
began, it starts over from what GitHub holds now (`_Stale`), first undoing a release it had begun. Two cards of one
claimant are merged into the first.

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
    command_shaped,
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
from coney_tools.issue_claim_rules import active_cards, check_claim, is_maintainer, overrun, unclaimed_critical
from coney_tools.issue_form import section_heading

#: How many times one `reconcile` starts over when another run changed the issue's cards meanwhile.
ATTEMPTS = 4


class _Stale(ConfigError):
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
class _Pass:
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


class Engine:
    """The claim logic over a `Remote`, with an injected clock; nothing else is global."""

    def __init__(self, remote: Remote, conventions: Conventions, now: Callable[[], datetime]) -> None:
        self.remote = remote
        #: The rules of the repository; `rules` is these, or the trial's for the issue a pass is working on.
        self._base_rules: Claims = conventions.claims
        self.rules: Claims = self._base_rules
        self._status_prefix = conventions.labels.status_prefix
        self._blocked_heading = section_heading(conventions, "blocked-by")
        self._now = now
        self._held: dict[int, Card] | None = None
        self._permissions: dict[str, str | None] = {}
        #: The open issues that carry a working label but have no claim (a sweep reports them).
        self.orphans: set[int] = set()

    def is_maintainer(self, login: str, association: str = "NONE") -> bool:
        """Whether `login` is a maintainer: the repository permission GitHub reports for them (asked once per login),
        else the comment's own association."""
        if login not in self._permissions:
            self._permissions[login] = self.remote.permission(login)
        return is_maintainer(self._permissions[login], association, self.rules)

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

    def reconcile(self, number: int, activity: Activity | None = None) -> list[str]:
        """Bring issue `number` in line with its commands, its GitHub state and the deadlines; the log lines.

        `activity` is the pull request event that triggered the pass, if one did.
        """
        log: list[str] = []
        try:
            for _ in range(ATTEMPTS):
                issue = self.remote.issue(number)
                if issue.is_pr:
                    return log
                self.rules = self._rules_for(issue)
                done = self._attempt(issue, log, activity)
                if done is not None:
                    return done
        finally:
            self.rules = self._base_rules
        raise _Stale(f"#{number}: the claim kept changing under {ATTEMPTS} passes; the next run finishes it", log)

    def _rules_for(self, issue: IssueView) -> Claims:
        """The rules for one issue: the repository's, or for an issue with the trial label adoption and the idle
        release switched on with the trial's hours (`claims.trial`), so they can be tried before the cutover."""
        base = self._base_rules
        if base.trial.label not in issue.labels:
            return base
        return replace(
            base,
            adopt=True,
            release=True,
            warn_hours=base.trial.warn_hours,
            deadline_hours=base.trial.deadline_hours,
        )

    def _attempt(self, issue: IssueView, log: list[str], activity: Activity | None) -> list[str] | None:
        """One pass over the issue: its log lines, or None when another run changed it meanwhile (`log` then holds
        what this pass did, and the caller starts over)."""
        number = issue.number
        comments = self.remote.comments(number)
        stored = self._stored_cards(comments)
        work = _Pass(issue, comments, stored, latest_held(stored))
        self._refresh_held(work)
        try:
            self._commands(work)
            self._sync(work, activity)
        except _Stale:
            log.extend(work.log)
            log.append(f"#{number}: another run changed the claim meanwhile; starting over")
            return None
        return log + work.log

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
            work.processed.add(comment.id)
            if isinstance(command, Rejected):
                # Not a real command: it stays, next to the reply that says what to write instead.
                self._reply(work, comment, command.reason)
                continue
            if command.kind == "claim":
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
        text = f"{reply_first_line(self.rules, comment.id)}\nNot done for @{comment.login}: {reason}"
        self.remote.create_comment(work.issue.number, text)
        work.log.append(f"#{work.issue.number}: refused {comment.login}: {reason}")

    def _claim(self, work: _Pass, comment: Comment, command: Command) -> None:
        """`/claim`: a scope update on the claimant's own claim, or a new claim after the checks."""
        number = work.issue.number
        active = work.active
        if active is not None and active.card.claimant == comment.login:
            self._own_claim(work, comment, command, active)
            return
        # A claim that failed half way (assigned, no card) is finished, not refused: the claimant is the only assignee.
        # It is checked like any claim, as the issue stood before its own assignment and working label.
        resuming = active is None and work.issue.assignees == (comment.login,)
        maintainer = self.is_maintainer(comment.login, comment.association)
        refusal = check_claim(
            command,
            self._before_own_claim(work.issue) if resuming else work.issue,
            comment.login,
            maintainer,
            active.card if active else None,
            self.held_cards().values(),
            self.remote,
            self.rules,
            self._blocked_heading,
        )
        if refusal is not None:
            self._reply(work, comment, refusal.reason)
            return
        self._still_current(work)
        if not resuming:
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
            exempt=self.rules.exempt_label in work.issue.labels,
        )
        fresh = Stored(None, card)
        work.stored.append(fresh)
        work.active = fresh
        self._save(work, fresh, card)
        work.issue = self.remote.issue(number)
        half = " (finished a claim that stopped half way)" if resuming else ""
        # Who counts as what, in the log: the live test reads it to see the permission GitHub gave the Action.
        role = "a maintainer" if maintainer else "a contributor"
        permission = self._permissions.get(comment.login) or f"unknown, association {comment.association}"
        exempt = f", outside the limit (`{self.rules.exempt_label}`)" if card.exempt else ""
        work.log.append(f"#{number}: {comment.login} claimed it{half} as {role} (permission {permission}){exempt}")
        self._audit_new_claim(work, comment, fresh, maintainer)

    def _before_own_claim(self, issue: IssueView) -> IssueView:
        """The issue as it stood before a half-made claim touched it: no assignee, no working or review label, and
        the claimable status back when that claim had already removed every status label."""
        mine = (self.rules.working_label, self.rules.review_label)
        labels = tuple(label for label in issue.labels if label not in mine)
        if not any(label.startswith(self._status_prefix) for label in labels):
            labels = (*labels, self.rules.claimable_status)
        return replace(issue, assignees=(), labels=labels)

    def _own_claim(self, work: _Pass, comment: Comment, command: Command, active: Stored) -> None:
        """`/claim scope: ...` on the claimant's own claim replaces the scope; the task cannot change."""
        card = active.card
        if command.scope is None and command.task is None:
            self._reply(work, comment, "that claim is already yours; `/claim renew` starts the clock again")
            return
        if command.task is not None and command.task != card.task:
            held_task = f"it is `{card.task}`" if card.task else "it has none"
            self._reply(work, comment, f"the task of a claim cannot change ({held_task}); `/unclaim` and claim again")
            return
        self._still_current(work)
        self._drop_warning(work, card)
        self._save(
            work,
            active,
            card.changed(
                scope=command.scope if command.scope is not None else card.scope,
                last_activity=comment.created_at,
                last_activity_why="scope",
                warned_at=None,
                warning_comment=None,
                handled=handled_with(card, comment.id),
            ),
        )
        work.log.append(f"#{work.issue.number}: {comment.login} changed the scope")

    def _audit_new_claim(self, work: _Pass, comment: Comment, stored: Stored, maintainer: bool) -> None:
        """Parallel runs on other issues can overrun the limit or the `P-critical` rule: recount from fresh data and
        release this claim, with a reply, if it is the one that is over."""
        self._held = None
        held = self.held_cards()
        held[stored.card.issue] = stored.card
        why = overrun(stored.card, held.values(), maintainer, self.rules)
        if why is None and self.rules.critical_label not in work.issue.labels:
            first = unclaimed_critical(self.remote, self.rules, set(held), stored.card.issue)
            if first:
                names = ", ".join(f"#{n}" for n in first)
                why = f"`{self.rules.critical_label}` comes first: {names} has no claim"
        if why is None:
            return
        self._reply(work, comment, f"your claim was released again: {why} (another claim landed at the same time)")
        self._release(work, stored, stored.card, f"released: {why}", "released", check_fresh=False)

    def _renew(self, work: _Pass, comment: Comment) -> None:
        """`/claim renew`: the claimant starts the idle clock again."""
        active = work.active
        if active is None or active.card.claimant != comment.login:
            self._reply(work, comment, "only the claimant can renew a claim, and this issue has none for you")
            return
        card = active.card
        self._still_current(work)
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
        self._still_current(work)
        self._drop_warning(work, card)
        self._save(
            work,
            active,
            card.changed(
                stage=command.stage,
                # The unit's slot is free from this stage on (its review passed): the claim stops counting, for good.
                counted=card.counted and command.stage not in self.rules.limit_ends_at,
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
        if comment.login != active.card.claimant and not self.is_maintainer(comment.login, comment.association):
            self._reply(work, comment, f"only {active.card.claimant} or a maintainer can give this claim up")
            return
        card = active.card.changed(handled=handled_with(active.card, comment.id))
        by = "" if comment.login == card.claimant else f" by {comment.login}"
        self._release(work, active, card, f"unclaimed{by}", "released", check_fresh=False)

    # -- keeping the card and the issue true

    def _drop_warning(self, work: _Pass, card: Card) -> None:
        """Delete the warning comment of a card, if it has one."""
        if card.warning_comment is not None:
            self.remote.delete_comment(card.warning_comment)
            work.log.append(f"#{work.issue.number}: removed the expiry warning")

    def _set_status_label(self, work: _Pass, want: str) -> None:
        """Leave exactly one status label on the issue: `want`.

        A paused label (`S-blocked`, `S-needs-info`, `S-on-hold`) a maintainer put on a claimed issue stays: the claim
        keeps and nothing else is set. A hold label (`S-on-hold`) also takes the place of the claim's label, so the
        other status labels go; once it is off, the next pass sets `want` again.
        """
        number = work.issue.number
        if any(label in self.rules.paused_labels for label in work.issue.labels):
            if any(label in self.rules.hold_labels for label in work.issue.labels):
                self._drop_unpaused_status(work)
            return
        for label in work.issue.labels:
            if label.startswith(self._status_prefix) and label != want:
                self.remote.remove_label(number, label)
        if want not in work.issue.labels:
            self.remote.add_label(number, want)
        kept = tuple(label for label in work.issue.labels if not label.startswith(self._status_prefix))
        work.issue = replace(work.issue, labels=(*kept, want))

    def _unpaused_status(self, work: _Pass) -> list[str]:
        """The status labels on the issue that are not paused ones."""
        return [
            label
            for label in work.issue.labels
            if label.startswith(self._status_prefix) and label not in self.rules.paused_labels
        ]

    def _status_label_changes(self, work: _Pass, want: str) -> bool:
        """Whether `_set_status_label(work, want)` would write a label now: no on a paused issue (a block keeps its
        labels; a hold writes only to take the claim's label off, once), otherwise when `want` is missing. One write
        it does not see: with no paused label, a second status label is removed without the guard."""
        labels = work.issue.labels
        if any(label in self.rules.paused_labels for label in labels):
            return any(label in self.rules.hold_labels for label in labels) and bool(self._unpaused_status(work))
        return want not in labels

    def _drop_unpaused_status(self, work: _Pass) -> None:
        """Remove every status label that is not a paused one: a hold leaves the issue with its paused labels only."""
        drop = self._unpaused_status(work)
        for label in drop:
            self.remote.remove_label(work.issue.number, label)
        if drop:
            kept = tuple(label for label in work.issue.labels if label not in drop)
            work.issue = replace(work.issue, labels=kept)
            work.log.append(f"#{work.issue.number}: removed {', '.join(drop)}: the issue is on hold")

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
        if stored.comment_id is None or text != stored.text:
            self._still_current(work)
        if stored.comment_id is None:
            stored.comment_id = self.remote.create_comment(card.issue, text)
        elif text != stored.text:
            self.remote.edit_comment(stored.comment_id, text)
        stored.card, stored.text = card, text
        if work.active is stored and not card.active:
            work.active = None

    def _still_current(self, work: _Pass) -> None:
        """Raise `_Stale` when the issue's cards on GitHub are no longer the ones this pass holds: another run wrote
        one (a `/progress`, a new card) since this pass read them, and writing now would undo it."""
        comments = self.remote.comments(work.issue.number)
        fresh = {item.comment_id: item.card for item in stored_cards(comments, self.rules)}
        mine = {item.comment_id: parse_card(item.text, self.rules) for item in work.stored if item.comment_id}
        if fresh != mine:
            raise _Stale(f"#{work.issue.number}: the claim changed meanwhile")

    def _unchanged(self, work: _Pass, stored: Stored) -> bool:
        """Whether the card on GitHub is still the one this pass read and no new command waits: re-read before a
        release, so a `/claim renew` or a `/progress` that arrived meanwhile is never undone."""
        comments = self.remote.comments(work.issue.number)
        cards = stored_cards(comments, self.rules)
        now = next((item for item in cards if item.comment_id == stored.comment_id), None)
        if now is None:
            return False
        seen, before = now.card, stored.card
        keys = (seen.status, seen.claimant, seen.activity, seen.warned_at, seen.task, seen.waiting)
        if keys != (before.status, before.claimant, before.activity, before.warned_at, before.task, before.waiting):
            return False
        answered = {item for kept in cards for item in kept.card.handled}
        for c in comments:
            target = reply_target(c.body, self.rules)
            if target is not None and c.login == self.rules.bot_login:
                answered.add(target)
        return not any(
            c.user_type != "Bot"
            and c.login != self.rules.bot_login
            and command_shaped(c.body)
            and c.id not in answered
            and c.id not in work.processed
            for c in comments
        )

    def _release(
        self,
        work: _Pass,
        stored: Stored,
        card: Card,
        why: str,
        status: str,
        keep_assignees: bool = False,
        check_fresh: bool = True,
    ) -> bool:
        """End a claim: unassign, hand the issue back to `claims.released_label`, remove the warning, mark the card.

        `keep_assignees` leaves the assignees and the labels alone (somebody else was assigned by hand).
        `check_fresh` re-reads the card first and gives up when it changed or a command is waiting. Returns whether
        the claim ended.
        """
        number = work.issue.number
        if check_fresh and stored.comment_id is not None and not self._unchanged(work, stored):
            work.log.append(f"#{number}: the claim changed while it was being released; left alone")
            return False
        self._still_current(work)
        before = work.issue
        self._drop_warning(work, card)
        if not keep_assignees:
            if work.issue.assignees:
                self.remote.unassign(number, work.issue.assignees)
            if work.issue.state == "open":
                self._set_status_label(work, self.rules.released_label)
        try:
            self._save(
                work,
                stored,
                card.changed(
                    status=status, released_at=self._now(), released_why=why, warned_at=None, warning_comment=None
                ),
            )
        except _Stale:
            self._undo_release(work, stored, before, keep_assignees)
            raise
        work.log.append(f"#{number}: claim of {card.claimant} ended ({why})")
        return True

    def _undo_release(self, work: _Pass, stored: Stored, before: IssueView, keep_assignees: bool) -> None:
        """Put back the assignees and the status label a release removed when the card changed under it and the
        claimant still holds the issue (a `/progress` landed, or a re-claim posted a new card); the pass starts over
        and decides again from fresh data.

        The claim is judged by its claimant, not by the card's comment id: a re-claim is a new card, and its old card
        is ended. When the claimant holds no active card any more (the claim ended elsewhere too), nothing is put
        back.

        This is best effort against writers outside the queue. The cards are read, then the assignee is put back, and
        a hand-run writer can still act between the two; that cannot be closed without a lock. The sweep (or the
        audit) catches such a result on its next pass.
        """
        if keep_assignees:
            return
        number = work.issue.number
        claimant = stored.card.claimant
        fresh = stored_cards(self.remote.comments(number), self.rules)
        if not any(item.card.active and item.card.claimant == claimant for item in fresh):
            work.log.append(f"#{number}: the claim was ended elsewhere meanwhile; the release stands")
            return
        for login in before.assignees:
            self.remote.assign(number, login)
        status = [label for label in before.labels if label.startswith(self._status_prefix)]
        for label in status:
            self.remote.add_label(number, label)
        if self.rules.released_label not in status:
            self.remote.remove_label(number, self.rules.released_label)
        work.log.append(f"#{number}: the card changed during the release; undone")

    # -- rebuilding the state from GitHub

    def _sync(self, work: _Pass, activity: Activity | None) -> None:
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
                    self._still_current(work)
                    self.remote.unassign(number, extras)
                    work.log.append(f"#{number}: removed {', '.join(extras)}: one claimant per issue")
        if active is None and self.rules.adopt:
            self._adopt(work)
            active = work.active
        if active is None:
            self._note_orphan(work)
            return
        self._follow_pull_request(work, active, activity)
        self._deadlines(work, active)

    def _settle_race(self, work: _Pass) -> None:
        """Two held claims on one issue (a dropped run, a hand assignment): the earliest `claimed_at` keeps it."""
        held = [stored for stored in work.stored if stored.card.active]
        if len(held) < 2:
            work.active = held[0] if held else None
            return
        self._still_current(work)
        held.sort(key=lambda stored: (stored.card.claimed_at, stored.comment_id or 0))
        winner = held[0]
        for twin in [stored for stored in held[1:] if stored.card.claimant == winner.card.claimant]:
            self._merge_twin(work, winner, twin)
            held.remove(twin)
        for loser in held[1:]:
            if loser.card.claimant in work.issue.assignees and loser.card.claimant != winner.card.claimant:
                self.remote.unassign(work.issue.number, (loser.card.claimant,))
            self._save(
                work,
                loser,
                loser.card.changed(
                    status="lost",
                    released_at=self._now(),
                    released_why=f"lost to {winner.card.claimant}",
                    warned_at=None,
                    warning_comment=None,
                ),
            )
            work.log.append(f"#{work.issue.number}: {loser.card.claimant} lost the claim to {winner.card.claimant}")
        work.active = winner
        work.issue = self.remote.issue(work.issue.number)

    def _merge_twin(self, work: _Pass, keeper: Stored, twin: Stored) -> None:
        """A second card of the same claim (two runs posted one each): its handled commands join the first card and
        it is deleted, or marked `lost` when it cannot be."""
        merged = keeper.card
        for item in twin.card.handled:
            merged = merged.changed(handled=handled_with(merged, item))
        newer, older = (
            (twin.card, keeper.card) if twin.card.activity > keeper.card.activity else (keeper.card, twin.card)
        )
        merged = merged.changed(
            stage=newer.stage or older.stage,
            scope=newer.scope or older.scope,
            task=keeper.card.task or twin.card.task,
            counted=keeper.card.counted and twin.card.counted,
            last_activity=newer.activity,
            last_activity_why=newer.last_activity_why,
        )
        self._save(work, keeper, merged)
        if twin.comment_id is not None and self.remote.delete_comment(twin.comment_id):
            work.stored.remove(twin)
        else:
            ended = twin.card.changed(
                status="lost", released_at=self._now(), released_why="a second card of the same claim"
            )
            self._save(work, twin, ended)
        work.log.append(f"#{work.issue.number}: merged a second card of the claim of {keeper.card.claimant}")

    def _close(self, work: _Pass) -> None:
        """A closed issue's held claim ends: merged when a pull request closed it, else released."""
        active = work.active
        if active is None:
            return
        merged = [pr for pr in self.remote.linked_prs(work.issue.number) if pr.state == "merged"]
        card = active.card.changed(pr=merged[0].number if merged else active.card.pr)
        if merged:
            self._still_current(work)
            self._drop_warning(work, card)
            self._save(
                work,
                active,
                card.changed(status="merged", released_at=self._now(), warned_at=None, warning_comment=None),
            )
            work.log.append(f"#{work.issue.number}: merged, claim of {card.claimant} done")
        else:
            self._release(work, active, card, "the issue was closed", "released")

    def _note_orphan(self, work: _Pass) -> None:
        """An open issue that says it is being worked on but has no assignee and no claim cannot be released or
        followed: report it (the sweep and the audit's `issue.claim-orphan`), do not touch it."""
        labels = work.issue.labels
        if (
            work.issue.state == "open"
            and not work.issue.assignees
            and (self.rules.working_label in labels or self.rules.review_label in labels)
        ):
            self.orphans.add(work.issue.number)
            work.log.append(
                f"orphan: #{work.issue.number} carries a working label but has no assignee and no claim; "
                f"claim it with `task create` or set it back to `{self.rules.released_label}`"
            )

    def _adopt(self, work: _Pass) -> None:
        """An open issue assigned by hand gets a card, so the deadline covers it too."""
        assignees = tuple(login for login in work.issue.assignees if login != self.rules.bot_login)
        if not assignees or work.issue.state != "open":
            return
        self._still_current(work)
        if len(assignees) > 1:
            self.remote.unassign(work.issue.number, assignees[1:])
        card = Card(
            issue=work.issue.number,
            claimant=assignees[0],
            claimed_at=self._now(),
            adopted=True,
            last_activity_why="assigned by hand",
            exempt=self.rules.exempt_label in work.issue.labels,
        )
        fresh = Stored(None, card)
        work.stored.append(fresh)
        work.active = fresh
        self._set_status_label(work, self.rules.working_label)
        self._save(work, fresh, card)
        work.log.append(f"#{work.issue.number}: adopted the hand assignment to {assignees[0]}")

    def _follow_pull_request(self, work: _Pass, stored: Stored, activity: Activity | None) -> None:
        """Status, label and activity follow the open pull request of the claimant (or a maintainer) that closes the
        issue; someone else's pull request does not count as the claim's."""
        card = stored.card

        def counts(login: str) -> bool:
            return bool(login) and (login == card.claimant or self.is_maintainer(login))

        linked = self.remote.linked_prs(work.issue.number)
        open_pr = next((pr for pr in linked if pr.state == "open" and counts(pr.author)), None)
        changes: dict[str, object] = {
            "status": "pr-open" if open_pr else "claimed",
            "pr": open_pr.number if open_pr else None,
            # The label can come and go: the claim is outside the limit while the issue carries it.
            "exempt": self.rules.exempt_label in work.issue.labels,
            # An open pull request of the claim: the unit is past review, its slot free; it never counts again.
            "counted": card.counted and open_pr is None,
        }
        moments = [open_pr.head_date if open_pr else None, activity.at if activity and counts(activity.by) else None]
        pushed = [moment for moment in moments if moment is not None]
        if pushed and max(pushed) > card.activity:
            changes.update(last_activity=max(pushed), last_activity_why="pull request")
        mine = [pr for pr in linked if pr.state == "open" and pr.author == card.claimant and not pr.draft]
        work.in_review = mine[0].number if mine else None
        want = self.rules.review_label if open_pr else self.rules.working_label
        # Only a label write needs the stale-read guard. A held issue writes once, to take the claim's label off, and
        # nothing after that, though `want` stays missing: no guard on those passes.
        if self._status_label_changes(work, want):
            self._still_current(work)
        self._set_status_label(work, want)
        self._save(work, stored, card.changed(**changes))

    def _deadlines(self, work: _Pass, stored: Stored) -> None:
        """The warning at `claims.warn_hours` of idleness, the release at `claims.deadline_hours`."""
        if not self.rules.release:
            return
        card = stored.card
        now = self._now()
        reason = self._waiting_for(work, card)
        if reason is not None:
            # Waiting on someone else (a review, a maintainer's label): no warning, no release, one card write.
            if card.waiting != reason or card.warned_at is not None or card.warning_comment is not None:
                self._still_current(work)
                self._drop_warning(work, card)
                self._save(work, stored, card.changed(waiting=reason, warned_at=None, warning_comment=None))
                work.log.append(f"#{work.issue.number}: the clock waits while {reason}")
            return
        if card.waiting is not None:
            # The wait ended: the clock starts again from now.
            ended = card.changed(
                waiting=None, last_activity=max(now, card.activity), last_activity_why=f"{card.waiting} ended"
            )
            self._save(work, stored, ended)
            card = stored.card
        # Released only after a warning and its full notice (`Card.deadline`): never unwarned.
        if card.warned_at is not None and now >= card.deadline(self.rules):
            self._release(work, stored, card, f"idle {self.rules.deadline_hours} h", "released")
        elif now >= card.warn_at(self.rules):
            if card.warned_at is None:
                self._still_current(work)
                deadline = card.changed(warned_at=now).deadline(self.rules)
                left = max(1, -(-int((deadline - now).total_seconds()) // 3600))
                comment = self.remote.create_comment(
                    work.issue.number,
                    f"@{card.claimant} your claim of this issue expires in about {left} h "
                    f"({show_time(deadline)}): record `/progress <stage>`, push to its pull request "
                    "or write `/claim renew` to keep it.",
                )
                self._save(work, stored, card.changed(warned_at=now, warning_comment=comment))
                work.log.append(f"#{work.issue.number}: warned {card.claimant}")
        elif card.warning_comment is not None or card.warned_at is not None:
            self._still_current(work)
            self._drop_warning(work, card)
            self._save(work, stored, card.changed(warned_at=None, warning_comment=None))

    def _waiting_for(self, work: _Pass, card: Card) -> str | None:
        """Why the idle clock waits: a paused label a maintainer set, or the claim's open pull request in review."""
        paused = [label for label in work.issue.labels if label in self.rules.paused_labels]
        if paused:
            return paused[0]
        if work.in_review is not None:
            return f"PR #{work.in_review} open"
        return None

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
                result.lines.extend(getattr(error, "lines", []))
                result.errors.append(f"#{number}: {error}")
        try:
            result.lines.extend(self._enforce_limits())
        except ConfigError as error:
            result.errors.append(f"limits: {error}")
        try:
            result.lines.extend(self._clean_replies(now))
        except ConfigError as error:
            result.errors.append(f"replies: {error}")
        return result

    def catch_up(self, done: list[int]) -> SweepResult:
        """Handle the commands of other issues posted in the last `claims.catch_up_minutes`: one run at a time means
        GitHub cancels a pending run when a newer one queues, and this is how its command is still answered soon.

        `done` are the issues this run already reconciled. The sweep covers anything older.
        """
        result = SweepResult()
        since = self._now() - timedelta(minutes=self.rules.catch_up_minutes)
        try:
            recent = self.remote.recent_commands(since)
        except ConfigError as error:
            result.errors.append(f"catch-up: {error}")
            return result
        waiting = sorted({comment.issue for comment in recent} - set(done))
        for number in waiting:
            try:
                result.lines.extend(self.reconcile(number))
            except ConfigError as error:
                result.lines.extend(getattr(error, "lines", []))
                result.errors.append(f"#{number}: {error}")
        return result

    def _enforce_limits(self) -> list[str]:
        """Release the claims that are over the limit of open claims or of a task, on fresh data (the late ones only).

        A claim assigned by hand and adopted is not counted: nobody was asked to check it against a limit; nor is an
        exempt one (`claims.exempt_label`), which `overrun` never finds over.
        """
        self._held = None
        held = list(self.held_cards().values())
        late: list[tuple[Card, str]] = []
        for card in held:
            if card.adopted:
                continue
            why = overrun(card, held, self.is_maintainer(card.claimant), self.rules)
            if why is not None:
                late.append((card, why))
        lines: list[str] = []
        for card, why in late:
            issue = self.remote.issue(card.issue)
            comments = self.remote.comments(card.issue)
            stored = stored_cards(comments, self.rules)
            work = _Pass(issue, comments, stored, latest_held(stored))
            if work.active is None or work.active.card.claimant != card.claimant:
                continue
            try:
                ended = self._release(work, work.active, work.active.card, f"released: {why}", "released")
            except _Stale:
                ended = False
                work.log.append(f"#{card.issue}: the claim changed meanwhile; the next sweep looks again")
            if ended:
                notice = f"{reply_first_line(self.rules, 0)}\nClaim released: {why}; finish or give up another first."
                self.remote.create_comment(card.issue, notice)
            lines.extend(work.log)
        return lines

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
