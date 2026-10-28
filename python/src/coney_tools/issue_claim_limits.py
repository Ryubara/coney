# SPDX-License-Identifier: GPL-3.0-or-later
"""The sweep's enforcement of the open-claims limit (docs/guides/issues.md#the-limit-follows-the-engineer-slots).

A mixin of the claim `Engine`: it releases the claims that went over the limit through a late write, and warns first
(then releases after the notice) the claims of a claimant whose count came back, because a ready pull request was
closed unmerged. It reaches the engine's own state and writes through the declarations below, which exist for the type
checker only: the engine defines them.
"""

from __future__ import annotations

from collections.abc import Callable
from datetime import datetime
from typing import TYPE_CHECKING

from coney_tools.conventions import Claims
from coney_tools.issue_claim_model import Card, reply_first_line, show_time
from coney_tools.issue_claim_pass import Pass, Stale, Stored, latest_held, stored_cards
from coney_tools.issue_claim_remote import IssueView, Remote
from coney_tools.issue_claim_rules import overrun


class LimitEnforcement:
    """The sweep's limit checks, mixed into `Engine`."""

    if TYPE_CHECKING:
        remote: Remote
        rules: Claims
        _base_rules: Claims
        _now: Callable[[], datetime]
        _held: dict[int, Card] | None
        _permissions: dict[str, str | None]

        def held_cards(self) -> dict[int, Card]: ...
        def is_maintainer(self, login: str, association: str = "NONE") -> bool: ...
        def _rules_for(self, issue: IssueView) -> Claims: ...
        def _still_current(self, work: Pass) -> None: ...
        def _save(self, work: Pass, stored: Stored, card: Card) -> None: ...
        def _release(
            self,
            work: Pass,
            stored: Stored,
            card: Card,
            why: str,
            status: str,
            keep_assignees: bool = False,
            check_fresh: bool = True,
        ) -> bool: ...

    def _enforce_limits(self) -> list[str]:
        """Release the claims that are over the limit of open claims or of a task, on fresh data (the late ones only).

        A claim assigned by hand and adopted is not counted: nobody was asked to check it against a limit; nor is an
        exempt one (`claims.exempt_label`), which `overrun` never finds over.
        """
        self._held = None
        held = list(self.held_cards().values())
        late: list[tuple[Card, str]] = []
        lines_unread: list[str] = []
        unread: set[str] = set()
        for card in held:
            if card.adopted:
                continue
            trusted = self.is_maintainer(card.claimant)
            if not trusted and self.permission_unknown(card.claimant):
                # Fail closed: a claimant whose permission could not be read may be a maintainer, whose claims are never
                # released for the limit, so none is until GitHub says otherwise.
                lines_unread.append(self._unread_line(card.issue, card.claimant))
                unread.add(card.claimant)
                trusted = True
            why = overrun(card, held, trusted, self.rules)
            if why is not None:
                late.append((card, why))
        lines: list[str] = list(lines_unread)
        # A claimant whose count came back (a ready pull request closed unmerged) is over the limit through no new
        # claim, so the newest claims past the limit get a warning and their notice first, as an idle claim does.
        regained = {card.claimant for card in held if card.regained}
        for card, why in late:
            if card.claimant in regained:
                lines.extend(self._warn_or_release_over(card, why, held))
                continue
            issue = self.remote.issue(card.issue)
            comments = self.remote.comments(card.issue)
            stored = stored_cards(comments, self.rules)
            work = Pass(issue, comments, stored, latest_held(stored))
            if work.active is None or work.active.card.claimant != card.claimant:
                continue
            try:
                ended = self._release(work, work.active, work.active.card, f"released: {why}", "released")
            except Stale:
                ended = False
                work.log.append(f"#{card.issue}: the claim changed meanwhile; the next sweep looks again")
            if ended:
                notice = f"{reply_first_line(self.rules, 0)}\nClaim released: {why}; finish or give up another first."
                self.remote.create_comment(card.issue, notice)
            lines.extend(work.log)
        flagged = {card.issue for card, _ in late}
        over = {card.claimant for card, _ in late} | unread
        for card in held:
            # The episode is over for a claim that is not over the limit any more: its warning goes, and so does the
            # claimant's `regained` mark when none of their claims is over, so a later overrun found by a sweep (a
            # race between two `/claim`s) is released at once again, not warned first for ever.
            warning = card.over_warned_at is not None and card.issue not in flagged
            stale = card.regained and card.claimant not in over
            if warning or stale:
                lines.extend(self._clear_over_warning(card, warning, stale))
        return lines

    def _over_pass(self, card: Card) -> Pass | None:
        """A pass over the issue of `card` for a write of the sweep's limit check; None when the claim is not the
        claimant's any more."""
        issue = self.remote.issue(card.issue)
        comments = self.remote.comments(card.issue)
        stored = stored_cards(comments, self.rules)
        work = Pass(issue, comments, stored, latest_held(stored))
        if work.active is None or work.active.card.claimant != card.claimant:
            return None
        return work

    def _warn_or_release_over(self, card: Card, why: str, held: list[Card]) -> list[str]:
        """A claim past the limit of a claimant whose count came back: warn the claimant and start the notice (the
        one an idle claim gets); release the claim only once the notice has run out and it is still over. Never
        sooner, so work in progress is not lost to a pull request that was closed.

        It follows the idle release: off until the cutover (`claims.release`, or the trial label on this issue), when
        a sweep only says what it would do, and never for a maintainer's claim (the limit of 3 is theirs to keep).
        A claim whose claimant's count changed since the warning (`flips`: a ready pull request toggled between two
        sweeps) gets a fresh notice, but never later than twice the notice after the first warning.
        """
        work = self._over_pass(card)
        if work is None or work.active is None:
            return []
        number = card.issue
        if self.is_maintainer(card.claimant):
            return [
                f"#{number}: {card.claimant} is over the limit ({why}); a maintainer's claim is never released for it"
            ]
        if self.permission_unknown(card.claimant):
            return [self._unread_line(number, card.claimant)]
        rules = self._rules_for(work.issue)
        notice = card.over_notice(rules)
        if not rules.release:
            hours = int(notice.total_seconds() // 3600)
            return [
                f"#{number}: would warn {card.claimant}: over the limit ({why}), and release the claim after "
                f"{hours} h (release is off until the cutover)"
            ]
        active = work.active
        current = active.card
        now = self._now()
        flips = sum(c.flips for c in held if c.claimant == current.claimant)
        due = current.over_deadline(rules)
        cap = current.over_cap(rules)
        self.rules = rules
        try:
            if due is None or cap is None or (current.over_flips != flips and now < cap):
                # A first warning, or a fresh notice: the claimant's count changed since the last one.
                self._still_current(work)
                self._drop_over_warning(work, current)
                first = current.over_first_at or now if current.over_warned_at is not None else now
                started = current.changed(over_warned_at=now, over_first_at=first, over_flips=flips)
                until = started.over_deadline(rules) or now + notice
                comment = self.remote.create_comment(
                    number,
                    f"@{current.claimant} you hold more open claims than the limit allows ({why}): this claim is "
                    f"released at {show_time(until)} unless you finish or `/unclaim` another claim first.",
                )
                self._save(work, active, started.changed(over_warning_comment=comment))
                work.log.append(f"#{number}: warned {current.claimant}: over the limit ({why})")
            elif now >= due:
                if self._release(work, active, current, f"released: {why}", "released"):
                    first_line = reply_first_line(rules, 0)
                    self.remote.create_comment(
                        number, f"{first_line}\nClaim released: {why}; finish or give up another first."
                    )
        except Stale:
            work.log.append(f"#{number}: the claim changed meanwhile; the next sweep looks again")
        finally:
            self.rules = self._base_rules
        return work.log

    def _clear_over_warning(self, card: Card, warning: bool = True, regained: bool = False) -> list[str]:
        """The claimant is under the limit again: take the over-limit warning of this claim back (`warning`) and the
        `regained` mark of the count that came back (`regained`), and start the next episode's notice from nothing."""
        work = self._over_pass(card)
        if work is None or work.active is None:
            return []
        try:
            self._still_current(work)
            current = work.active.card
            if warning:
                self._drop_over_warning(work, current)
            changes: dict[str, object] = {"regained": False} if regained else {}
            if warning:
                changes.update(over_warned_at=None, over_warning_comment=None, over_first_at=None)
            self._save(work, work.active, current.changed(**changes))
            if warning:
                work.log.append(f"#{card.issue}: no longer over the limit")
        except Stale:
            work.log.append(f"#{card.issue}: the claim changed meanwhile; the next sweep looks again")
        return work.log

    def permission_unknown(self, login: str) -> bool:
        """Whether GitHub could not say what `login` may do here (asked once per login and kept), as opposed to
        saying they have no permission."""
        self.is_maintainer(login)
        return self._permissions[login] is None

    def _unread_line(self, number: int, login: str) -> str:
        """The log line for a claim the limit would release if only the claimant's permission could be read."""
        return (
            f"#{number}: could not read {login}'s permission, so the claim is treated as a maintainer's and is not "
            "released for the limit this run"
        )

    def _drop_over_warning(self, work: Pass, card: Card) -> None:
        """Delete the over-limit warning comment of a card, if it has one."""
        if card.over_warning_comment is not None:
            self.remote.delete_comment(card.over_warning_comment)
            work.log.append(f"#{work.issue.number}: removed the over-the-limit warning")
