# SPDX-License-Identifier: GPL-3.0-or-later
"""The data of claiming an issue: the command grammar, the claim card and its hidden JSON, scopes and deadlines.

Everything here is pure: strings and times in, values out, no GitHub and no clock. The shapes of the commands, the
card's marker and the deadlines come from `.github/conventions.yml` (`claims`); the rules that decide whether a claim
is allowed are in `issue_claim_rules.py`, the GitHub calls in `issue_claim_remote.py` and the loop that applies it all
to an issue in `issue_claim_engine.py` (docs/guides/issues.md#claiming).
"""

from __future__ import annotations

import json
import re
from collections.abc import Iterable
from dataclasses import dataclass, replace
from datetime import UTC, datetime, timedelta
from typing import Any

from coney_tools.conventions import Claims

#: The statuses a card has while the claim is held.
ACTIVE_STATUSES = ("claimed", "pr-open")
#: Every status of a card: held, then the ways a claim ends (`lost`: another claimant won a race).
STATUSES = (*ACTIVE_STATUSES, "merged", "released", "lost")
#: The version of the hidden JSON; a reader that finds another version ignores the card.
CARD_VERSION = 1

#: A folder, file or glob-free path in a scope (an address is a path to the filter: `0x` and eight hex digits).
_SCOPE_TOKEN = re.compile(r"^[A-Za-z0-9_./*+-]{1,120}$")
_ADDRESS = re.compile(r"^0x[0-9A-Fa-f]{8}$")
#: The most tokens a scope may hold.
MAX_SCOPE_TOKENS = 40
#: The words that start a claim command; a comment starting with one that does not match a grammar gets a reply.
COMMAND_WORDS = ("/claim", "/unclaim", "/progress")
_TIME = "%Y-%m-%dT%H:%M:%SZ"


# --- commands -------------------------------------------------------------------------------------------------------


@dataclass(frozen=True)
class Command:
    """A parsed command comment."""

    #: `claim`, `renew`, `unclaim` or `progress`.
    kind: str
    #: The scope tokens of `/claim scope: ...`, or None when the command carries none.
    scope: tuple[str, ...] | None = None
    #: The task id of `/claim ... task: <id>`, or None.
    task: str | None = None
    #: The stage of `/progress <stage>`.
    stage: str | None = None


@dataclass(frozen=True)
class Rejected:
    """A comment that starts like a command but is not one; `reason` says what to write instead."""

    reason: str


def command_shaped(body: str) -> bool:
    """Whether a comment starts with a command word (`/claim`, `/unclaim`, `/progress`), right or wrong."""
    first = body.strip().split(None, 1)
    return bool(first) and first[0] in COMMAND_WORDS


def parse_scope(text: str) -> tuple[str, ...]:
    """The tokens of a scope (`src/ai/, 0x00322b78`): comma separated folders, files and addresses.

    ValueError says what is wrong: a token with a character outside letters, digits and `_ . / * + -`, too many
    tokens, or none. Untrusted text is copied into a card only after this.
    """
    tokens = tuple(token.strip() for token in text.split(",") if token.strip())
    if not tokens:
        raise ValueError("the scope is empty")
    if len(tokens) > MAX_SCOPE_TOKENS:
        raise ValueError(f"the scope has {len(tokens)} entries; {MAX_SCOPE_TOKENS} at most")
    for token in tokens:
        if ".." in token or not _SCOPE_TOKEN.match(token):
            raise ValueError(f"'{token[:40]}' is not a folder, file or address (letters, digits and _ . / * + - only)")
    return tuple(dict.fromkeys(normalize_token(token) for token in tokens))


def normalize_token(token: str) -> str:
    """A scope token in its comparable form: addresses lower case, no leading `./`."""
    if _ADDRESS.match(token):
        return token.lower()
    return token[2:] if token.startswith("./") else token


GRAMMAR = (
    "`/claim` or `/claim scope: <folders, files, addresses> task: <task id>` (scope and task are optional), "
    "`/claim renew`, `/unclaim`, `/progress <stage>`"
)


def parse_command(body: str, rules: Claims) -> Command | Rejected | None:
    """The command a comment is, a `Rejected` when it starts like one but does not match, None when it is no command.

    The whole comment, trimmed, must match one of `claims.commands`; a command in the middle of a sentence or a quote
    is never one.
    """
    text = body.replace("\r\n", "\n").strip()
    if not command_shaped(text):
        return None
    if "\n" in text:
        return Rejected(f"a command is the whole comment on one line: {GRAMMAR}")
    commands = rules.commands
    if re.fullmatch(commands.renew, text):
        return Command("renew")
    if re.fullmatch(commands.unclaim, text):
        return Command("unclaim")
    found = re.fullmatch(commands.progress, text)
    if found:
        stage = found.group("stage")
        if stage not in rules.stages:
            return Rejected(f"'{stage}' is not a stage; the stages are {', '.join(rules.stages)}")
        return Command("progress", stage=stage)
    found = re.fullmatch(commands.claim, text)
    if found:
        scope_text = found.group("scope")
        if scope_text is not None and len(scope_text.strip()) > rules.scope_max_characters:
            return Rejected(
                f"the scope is {len(scope_text.strip())} characters; {rules.scope_max_characters} at most "
                "(name the folders, not every file)"
            )
        try:
            scope = parse_scope(scope_text) if scope_text is not None else None
        except ValueError as error:
            return Rejected(str(error))
        return Command("claim", scope=scope, task=found.group("task"))
    return Rejected(f"that is not a command: {GRAMMAR}")


# --- scopes ---------------------------------------------------------------------------------------------------------


def scopes_overlap(first: Iterable[str], second: Iterable[str]) -> list[str]:
    """The entries of `first` that overlap an entry of `second`: the same address or path, or a folder (ending `/`)
    holding the other's path. A warning for `task create`, never a refusal."""
    others = tuple(second)
    found: list[str] = []
    for token in first:
        for other in others:
            if (
                token == other
                or (token.endswith("/") and other.startswith(token))
                or (other.endswith("/") and token.startswith(other))
            ):
                found.append(token)
                break
    return found


# --- the card -------------------------------------------------------------------------------------------------------


@dataclass(frozen=True)
class Card:
    """One claim of one issue: what GitHub cannot tell later, because the command comments are deleted.

    The claimant, the status and the pull request are recomputed from GitHub on every pass; the card stores them only
    to show them and to find them again (the label of an issue and its assignee are the truth).
    """

    issue: int
    claimant: str
    #: The time of the winning `/claim` comment.
    claimed_at: datetime
    status: str = "claimed"
    scope: tuple[str, ...] = ()
    #: The task id the claim belongs to; issues whose cards share one count as one open claim.
    task: str | None = None
    stage: str | None = None
    last_activity: datetime | None = None
    #: What the last activity was: `claim`, `progress`, `renew`, `scope`, `push`.
    last_activity_why: str = "claim"
    warned_at: datetime | None = None
    #: The id of the comment that mentions the claimant at the warning, so it can be deleted.
    warning_comment: int | None = None
    pr: int | None = None
    released_at: datetime | None = None
    released_why: str | None = None
    #: Assigned by hand and adopted by the sweep, not claimed with a command.
    adopted: bool = False
    #: The ids of the command comments already acted on (the most recent `HANDLED_KEPT`).
    handled: tuple[int, ...] = ()
    #: The issues the claimant holds open, for the card's text (not used for the limit).
    open_claims: tuple[int, ...] = ()
    #: Why the idle clock waits (`S-blocked`, `PR #12 open`), or None while it runs; it starts again when this ends.
    waiting: str | None = None
    #: The issue carries `claims.exempt_label`, `claims.studio_label` or `claims.networking_label`: the claim is
    #: outside the open-claims limit (recomputed every pass).
    exempt: bool = False
    #: The issue carries `claims.studio_label`: Coney Studio work, in its own engineer track (recomputed every pass).
    studio: bool = False
    #: The issue carries `claims.networking_label`: networking work, in its own engineer track (recomputed every pass).
    #: A card written before this field existed reads False.
    networking: bool = False
    #: The issue carries `claims.exempt_label`: a unit that builds the new workflow (recomputed every pass). A card
    #: written before this field existed reads `in_workflow` from `exempt` and `studio` instead.
    workflow: bool = False
    #: Whether the claim counts toward the limit now: false once `freed` is set or while the claim has a pull request
    #: that is open and not a draft (the unit's slot is free), recomputed every pass.
    counted: bool = True
    #: A maintainer recorded a stage of `claims.limit_ends_at` (their tools do, when the review passes): the slot
    #: stays free from then on. Only a maintainer's stage counts; anyone else's comment is free to write.
    freed: bool = False
    #: The claim counted again after it had stopped (its ready pull request was closed unmerged or made a draft). The
    #: claimant may then hold more claims than the limit allows; the sweep warns the newest ones past the limit and
    #: releases them after the notice, instead of at once (work in progress), and no new claim is taken meanwhile.
    regained: bool = False
    #: When the claimant was told this claim is over the limit and will be released (only for a claimant with a
    #: `regained` claim), and the id of that warning comment so it can be deleted.
    over_warned_at: datetime | None = None
    over_warning_comment: int | None = None
    #: When the first warning of this over-the-limit episode was given; a fresh notice (after the claimant's count
    #: changed meanwhile) never reaches past twice the notice after it. None before a warning.
    over_first_at: datetime | None = None
    #: The claimant's `flips` (all their held claims) when the warning was given: a different total at the next sweep
    #: means a claim stopped or started counting in between, even if the claimant is over the limit again by now.
    over_flips: int = 0
    #: How many times this claim's `counted` has changed, for the check above.
    flips: int = 0

    def __post_init__(self) -> None:
        """A card with no last activity has had none since the claim."""
        if self.last_activity is None:
            object.__setattr__(self, "last_activity", self.claimed_at)

    @property
    def active(self) -> bool:
        """Whether the claim is held now."""
        return self.status in ACTIVE_STATUSES

    @property
    def in_workflow(self) -> bool:
        """Whether the issue carries `claims.exempt_label`. A card written before `workflow` existed has only `exempt`,
        which either label sets; such a card is a workflow one when it is exempt and neither Studio nor networking (an
        issue with both labels was not told apart then)."""
        return self.workflow or (self.exempt and not self.studio and not self.networking)

    def limit_label(self, rules: Claims) -> str:
        """The label that takes this card out of the open-claims limit, for the card's text and the log: the
        networking one, else the Studio one, else `claims.exempt_label`."""
        if self.networking and rules.networking_label:
            return rules.networking_label
        if self.studio and rules.studio_label:
            return rules.studio_label
        return rules.exempt_label

    @property
    def task_key(self) -> str:
        """What counts toward the claimant's limit: the task id, or this issue alone."""
        return self.task or f"issue-{self.issue}"

    @property
    def activity(self) -> datetime:
        """The last moment the claim showed life: the later of the claim and its last activity."""
        return max(self.claimed_at, self.last_activity or self.claimed_at)

    def deadline(self, rules: Claims) -> datetime:
        """When the claim is released if nothing happens: `claims.deadline_hours` after the last activity, but never
        sooner than the warning's notice (`deadline_hours - warn_hours`) after the warning was given.

        So a claim is never released unwarned: one already idle past the deadline when the release is switched on,
        or when the sweep ran late, is warned first and keeps the full notice.
        """
        idle = self.activity + timedelta(hours=rules.deadline_hours)
        if self.warned_at is None:
            return idle
        return max(idle, self.warned_at + timedelta(hours=rules.deadline_hours - rules.warn_hours))

    def over_notice(self, rules: Claims) -> timedelta:
        """The notice before a release for being over the limit: the one an idle claim gets (`deadline_hours` less
        `warn_hours`)."""
        return timedelta(hours=rules.deadline_hours - rules.warn_hours)

    def over_cap(self, rules: Claims) -> datetime | None:
        """The latest moment a release for being over the limit can be put off by fresh notices: twice the notice
        after the first warning of the episode. None while it has no warning."""
        if self.over_warned_at is None:
            return None
        return (self.over_first_at or self.over_warned_at) + 2 * self.over_notice(rules)

    def over_deadline(self, rules: Claims) -> datetime | None:
        """When the claim is released for being over the open-claims limit: the notice after the warning, but never
        past `over_cap`; None while it has no warning."""
        cap = self.over_cap(rules)
        if self.over_warned_at is None or cap is None:
            return None
        return min(self.over_warned_at + self.over_notice(rules), cap)

    def warn_at(self, rules: Claims) -> datetime:
        """When the claimant is warned: `claims.warn_hours` after the last activity."""
        return self.activity + timedelta(hours=rules.warn_hours)

    def changed(self, **changes: Any) -> Card:
        """A copy with some fields changed."""
        return replace(self, **changes)


#: How many handled command ids a card keeps.
HANDLED_KEPT = 30


def handled_with(card: Card, comment_id: int) -> tuple[int, ...]:
    """`card.handled` with `comment_id` added, the oldest dropped past `HANDLED_KEPT`."""
    if comment_id in card.handled:
        return card.handled
    return (*card.handled, comment_id)[-HANDLED_KEPT:]


def show_time(moment: datetime) -> str:
    """A UTC moment as people read it: `2026-10-25 14:03 UTC`."""
    return moment.astimezone(UTC).strftime("%Y-%m-%d %H:%M UTC")


def iso_time(moment: datetime) -> str:
    """A UTC moment as ISO text with a `Z`, to the second."""
    return moment.astimezone(UTC).strftime(_TIME)


def parse_time(text: str) -> datetime:
    """A moment written by `iso_time` (or any ISO 8601 text), as an aware UTC time; ValueError otherwise."""
    return datetime.fromisoformat(text.replace("Z", "+00:00")).astimezone(UTC)


def _data(card: Card) -> dict[str, Any]:
    """The card's hidden JSON, with times as ISO text."""

    def when(moment: datetime | None) -> str | None:
        return iso_time(moment) if moment is not None else None

    return {
        "v": CARD_VERSION,
        "issue": card.issue,
        "claimant": card.claimant,
        "claimed_at": iso_time(card.claimed_at),
        "status": card.status,
        "scope": list(card.scope),
        "task": card.task,
        "stage": card.stage,
        "last_activity": when(card.activity),
        "last_activity_why": card.last_activity_why,
        "warned_at": when(card.warned_at),
        "warning_comment": card.warning_comment,
        "pr": card.pr,
        "released_at": when(card.released_at),
        "released_why": card.released_why,
        "adopted": card.adopted,
        "handled": list(card.handled),
        "open_claims": list(card.open_claims),
        "waiting": card.waiting,
        "exempt": card.exempt,
        "studio": card.studio,
        "networking": card.networking,
        "workflow": card.workflow,
        "counted": card.counted,
        "freed": card.freed,
        "regained": card.regained,
        "over_warned_at": when(card.over_warned_at),
        "over_warning_comment": card.over_warning_comment,
        "over_first_at": when(card.over_first_at),
        "over_flips": card.over_flips,
        "flips": card.flips,
    }


def _hidden_json(data: dict[str, Any]) -> str:
    """`data` as one line of JSON that cannot close the HTML comment around it (`<` and `>` are escaped)."""
    return json.dumps(data, separators=(",", ":"), sort_keys=False).replace("<", "\\u003c").replace(">", "\\u003e")


def _status_line(card: Card) -> str:
    """The Status line of a card."""
    if card.status == "claimed":
        return "claimed" + (f" (stage: {card.stage})" if card.stage else "")
    if card.status == "pr-open":
        return f"PR #{card.pr} open" + (f" (stage: {card.stage})" if card.stage else "")
    if card.status == "merged":
        return "merged" + (f" in PR #{card.pr}" if card.pr else "")
    why = f" ({card.released_why})" if card.released_why else ""
    return ("lost" if card.status == "lost" else "released") + why


def render_card(card: Card, rules: Claims) -> str:
    """The text of a claim card: the visible lines, then the hidden JSON as the last line.

    The Expiration line is GitHub's `<relative-time datetime="...">` (it keeps only `datetime` and `lang`), which shows
    "in 3 days" and counts down; its text is the exact UTC deadline for where the element is not rendered.
    """
    if card.active:
        title = f"### Claimed by @{card.claimant}" + (" (assigned by hand)" if card.adopted else "")
    else:
        title = f"### Claim of {card.claimant}"
    lines = [f"<!-- {rules.card_marker} -->", title, "", f"- **Status:** {_status_line(card)}"]
    lines.append(f"- **Claimed:** {show_time(card.claimed_at)}")
    if card.task:
        lines.append(f"- **Task:** `{card.task}`")
    lines.append("- **Scope:** " + (", ".join(f"`{token}`" for token in card.scope) if card.scope else "not stated"))
    if card.active and not rules.release:
        lines.append(
            "- **Expiration:** not enforced yet; it starts with the new workflow "
            f"(it would be {show_time(card.deadline(rules))})"
        )
    elif card.active and card.waiting:
        lines.append(
            f"- **Expiration:** waits while {card.waiting}; the {rules.deadline_hours} h idle clock starts again "
            "when that ends"
        )
    elif card.active:
        deadline = card.deadline(rules)
        lines.append(
            f'- **Expiration:** <relative-time datetime="{iso_time(deadline)}">{show_time(deadline)}</relative-time>'
            f" (idle deadline {show_time(deadline)}; any /progress, a push to the pull request or `/claim renew` "
            "starts it again)"
        )
    elif card.released_at is not None:
        lines.append(f"- **Ended:** {show_time(card.released_at)}")
    lines.append(f"- **Last activity:** {show_time(card.activity)} ({card.last_activity_why})")
    if card.active and card.exempt:
        lines.append(f"- **Limit:** not counted toward the open-claims limit (`{card.limit_label(rules)}`)")
    elif card.active and not card.counted:
        lines.append(
            "- **Limit:** no longer counted toward the open-claims limit (its review passed, or its pull request is "
            "open and ready)"
        )
    over = card.over_deadline(rules)
    if card.active and over is not None:
        lines.append(
            f"- **Over the limit:** the claimant holds more open claims than the limit allows; this one is released "
            f"at {show_time(over)} unless another is finished or given up first"
        )
    if card.open_claims:
        lines.append(f"- **Open claims of {card.claimant}:** " + ", ".join(f"#{number}" for number in card.open_claims))
    lines += ["", f"<!-- {rules.card_marker}-data {_hidden_json(_data(card))} -->"]
    return "\n".join(lines)


def _moment(value: object) -> datetime | None:
    """A moment from the JSON (text or null); ValueError for anything else."""
    if value is None:
        return None
    if not isinstance(value, str):
        raise ValueError("a time is not text")
    return parse_time(value)


def parse_card(body: str, rules: Claims) -> Card | None:
    """The card in a comment's text, read from its hidden JSON; None when it holds none or an unknown version.

    Only the JSON is read, never the visible text, so editing the words of a card changes nothing.
    """
    found = re.search(rf"<!-- {re.escape(rules.card_marker)}-data (\{{.*\}}) -->", body.replace("\r\n", "\n"))
    if not found:
        return None
    try:
        data = json.loads(found.group(1))
        if not isinstance(data, dict) or data.get("v") != CARD_VERSION:
            return None
        status = str(data["status"])
        if status not in STATUSES:
            return None
        claimed_at = _moment(data["claimed_at"])
        if claimed_at is None:
            return None
        pr = data.get("pr")
        warning = data.get("warning_comment")
        return Card(
            issue=int(data["issue"]),
            claimant=str(data["claimant"]),
            claimed_at=claimed_at,
            status=status,
            scope=tuple(str(token) for token in data.get("scope", ())),
            task=str(data["task"]) if data.get("task") else None,
            stage=str(data["stage"]) if data.get("stage") else None,
            last_activity=_moment(data.get("last_activity")),
            last_activity_why=str(data.get("last_activity_why") or "claim"),
            warned_at=_moment(data.get("warned_at")),
            warning_comment=int(warning) if warning is not None else None,
            pr=int(pr) if pr is not None else None,
            released_at=_moment(data.get("released_at")),
            released_why=str(data["released_why"]) if data.get("released_why") else None,
            adopted=bool(data.get("adopted", False)),
            handled=tuple(int(item) for item in data.get("handled", ())),
            open_claims=tuple(int(item) for item in data.get("open_claims", ())),
            waiting=str(data["waiting"]) if data.get("waiting") else None,
            exempt=bool(data.get("exempt", False)),
            studio=bool(data.get("studio", False)),
            networking=bool(data.get("networking", False)),
            workflow=bool(data.get("workflow", False)),
            counted=bool(data.get("counted", True)),
            freed=bool(data.get("freed", False)),
            regained=bool(data.get("regained", False)),
            over_warned_at=_moment(data.get("over_warned_at")),
            over_warning_comment=int(over) if (over := data.get("over_warning_comment")) is not None else None,
            over_first_at=_moment(data.get("over_first_at")),
            over_flips=int(data.get("over_flips", 0)),
            flips=int(data.get("flips", 0)),
        )
    except (ValueError, KeyError, TypeError):
        return None


# --- replies --------------------------------------------------------------------------------------------------------


def reply_first_line(rules: Claims, command_id: int) -> str:
    """The first line of the reply to command comment `command_id`."""
    return f"<!-- {rules.reply_marker}:{command_id} -->"


def reply_target(body: str, rules: Claims) -> int | None:
    """The command comment a reply answers (from its first line), or None when the text is no reply."""
    found = re.match(rf"<!-- {re.escape(rules.reply_marker)}:(\d+) -->", body)
    return int(found.group(1)) if found else None


def is_card(body: str, rules: Claims) -> bool:
    """Whether a comment's text starts as a card does (the author is checked by the caller)."""
    return body.startswith(f"<!-- {rules.card_marker} -->")


#: A line of a "Blocked by" section that says what waits for this issue (`Blocks the release of #N`), not what it
#: waits for: its references are no blockers.
_BLOCKS_LINE = re.compile(r"^\W*blocks\b", re.IGNORECASE)


def blocked_by_numbers(section: str) -> list[int]:
    """The issue and pull request numbers (`#12`) in the text of a "Blocked by" section, in order, once each; a line
    that starts with "Blocks" names what this issue blocks, and is skipped."""
    lines = [line for line in section.splitlines() if not _BLOCKS_LINE.match(line)]
    return list(dict.fromkeys(int(number) for number in re.findall(r"(?<![\w&/])#(\d+)\b", "\n".join(lines))))
