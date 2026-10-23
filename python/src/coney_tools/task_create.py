# SPDX-License-Identifier: GPL-3.0-or-later
"""`task create`: claim the issues, wait until the claim is certain, then write the task file.

The order is the point (docs/guides/issues.md#task-files): the claim comes first, through the same `/claim` comment and
the same checks as everyone's; the task file is written only after the claim card names the claimant and has stayed
true for the settle time, so a task file exists only for a claim that held. All waiting is bounded and goes through an
injected clock and sleep, so the tests never wait.

The flow: read the issues, run the claim checks locally (the Action's own function, the same words), warn about scope
overlaps, search the leads, post `/claim scope: ... task: <id>` on each issue and wait for its card (a refusal reply or
the time limit ends it with nothing written), settle, write `TASK.md`, post `/progress assigned`. A fix task
(`fix_of`) reuses a claim already held and posts `/progress fix`.
"""

from __future__ import annotations

import re
from collections.abc import Callable
from dataclasses import dataclass
from datetime import datetime, timedelta
from pathlib import Path

from coney_tools.config import ConfigError
from coney_tools.conventions import Conventions
from coney_tools.issue_claim_engine import Engine, latest_held, stored_cards
from coney_tools.issue_claim_model import Card, Command, parse_command, reply_target, scopes_overlap
from coney_tools.issue_claim_remote import IssueView, Remote
from coney_tools.issue_claim_rules import check_claim
from coney_tools.issue_form import section_heading, section_text
from coney_tools.task_file import NO_FAILING_TEST, TaskFile, quote_block, render_task_file, slug
from coney_tools.task_leads import LeadsResult, Match

_TASK_ID = re.compile(r"^[a-z0-9][a-z0-9-]{0,59}$")


@dataclass(frozen=True)
class CreateRequest:
    """What `task create` was asked to do."""

    issues: tuple[int, ...]
    terms: tuple[str, ...] = ()
    scope: tuple[str, ...] = ()
    kind: str | None = None
    name: str | None = None
    #: The issue whose existing claim a fix task reuses (no `/claim`).
    fix_of: int | None = None
    #: Do not post `/claim` for an issue you already hold.
    resume: bool = False
    dry_run: bool = False


@dataclass
class CreateEnv:
    """The world `create_task` works in; every part can be replaced in a test."""

    remote: Remote
    conventions: Conventions
    #: The scratch folder; the task goes in `<scratch>/<task id>/`.
    scratch: Path
    now: Callable[[], datetime]
    sleep: Callable[[float], None]
    #: `(terms, issue text) -> leads`: the leads search over the prepared index.
    leads: Callable[[list[str], str], LeadsResult]
    out: Callable[[str], None] = print


def default_kind(issue: IssueView) -> str:
    """The task kind an issue suggests: `docs` for the docs label, `tooling` for `A-tools`, else `feature`."""
    if "docs" in issue.labels:
        return "docs"
    if "A-tools" in issue.labels:
        return "tooling"
    return "feature"


def claim_comment(scope: tuple[str, ...], task: str) -> str:
    """The `/claim` comment of a task, in the one exact shape the grammar allows."""
    return "/claim" + (f" scope: {', '.join(scope)}" if scope else "") + f" task: {task}"


# --- the sections of the file ---------------------------------------------------------------------------------------


def _cell(text: str) -> str:
    """Text safe inside a Markdown table cell."""
    return " ".join(text.replace("|", "\\|").split())


def leads_table(result: LeadsResult) -> str:
    """The "Where to look" section: one row per match with the reason it matched."""
    matches = result.matches()
    if not matches:
        return "No match in the indexes."
    rows = ["| Source | Match | Why |", "| --- | --- | --- |"]
    for match in matches:
        entry = match.entry
        note = f" ({entry.note})" if entry.note else ""
        rows.append(
            f"| {entry.source} | `{_cell(entry.title)}` at {_cell(entry.where)}{_cell(note)} | {_cell(match.reason)} |"
        )
    return "\n".join(rows)


def searched_line(result: LeadsResult, terms: tuple[str, ...]) -> str:
    """The "Searched, not related" section: the terms nothing matched."""
    if not terms:
        return "No terms were searched."
    if not result.unmatched:
        return "Every term matched something; prune the table above of what is not related."
    shown = (" ".join(term.replace("`", "'").split()) for term in result.unmatched)
    return "No match for: " + ", ".join(f"`{term}`" for term in shown) + "."


def _scope_section(scope: tuple[str, ...], overlaps: list[str]) -> str:
    """The "Scope" section: folders and files, addresses, and the overlaps with other claims."""
    paths = [token for token in scope if not token.startswith("0x")]
    addresses = [token for token in scope if token.startswith("0x")]
    lines = [
        "- Folders and files: " + (", ".join(f"`{token}`" for token in paths) or "not stated"),
        "- Original functions: " + (", ".join(f"`{token}`" for token in addresses) or "not stated"),
    ]
    lines += [f"- Overlap: {line}" for line in overlaps]
    return "\n".join(lines)


def _issues_section(issues: list[IssueView], blocked_heading: str | None) -> str:
    """The "Issues" section: number, title, labels and the "Blocked by" text."""
    lines = []
    for issue in issues:
        labels = ", ".join(f"`{label}`" for label in issue.labels)
        lines.append(f"- #{issue.number}: {issue.title} ({labels})")
        blocked = section_text(issue.body, blocked_heading) if blocked_heading else None
        if blocked:
            lines.append(f"  - Blocked by: {' '.join(blocked.split())}")
    return "\n".join(lines)


def build_task_file(
    request: CreateRequest,
    task_id: str,
    kind: str,
    user: str,
    claimed_at: datetime,
    issues: list[IssueView],
    leads: LeadsResult,
    overlaps: list[str],
    conventions: Conventions,
) -> TaskFile:
    """The task file for a claim that held."""
    first = issues[0]
    summary_heading = section_heading(conventions, "summary")
    done_heading = section_heading(conventions, "done")
    # Issue text is quoted line by line: a line of it that starts with "## " must not become a section of the file.
    goal = quote_block(
        (section_text(first.body, summary_heading) if summary_heading else None) or f"See issue #{first.number}."
    )
    done = quote_block(
        (section_text(first.body, done_heading) if done_heading else None) or f"See issue #{first.number}."
    )
    tests = (
        NO_FAILING_TEST if kind in ("docs", "tooling") else "A test that fails before the change and passes after it."
    )
    sections = {
        "Issues": _issues_section(issues, section_heading(conventions, "blocked-by")),
        "Goal": goal,
        "Done": done,
        "Scope": _scope_section(request.scope, overlaps),
        "Where to look": leads_table(leads),
        "Searched, not related": searched_line(leads, request.terms),
        "Expected tests": tests,
        "Decision points": "None known yet.",
        "Related PRs and open questions": "None yet.",
    }
    return TaskFile(
        task=task_id,
        issues=tuple(issue.number for issue in issues),
        kind=kind,
        claimant=user,
        claimed_at=claimed_at,
        scope=request.scope,
        terms=request.terms,
        sections=sections,
    )


# --- waiting for the claim ------------------------------------------------------------------------------------------


def _held_card(remote: Remote, conventions: Conventions, number: int) -> Card | None:
    """The held card of issue `number`, read from its comments."""
    found = latest_held(stored_cards(remote.comments(number), conventions.claims))
    return found.card if found else None


def _wait_for_card(env: CreateEnv, number: int, command_id: int | None, user: str) -> tuple[Card | None, str]:
    """Wait for the card that names `user` on issue `number`: (the card, "") or (None, why not).

    A reply to the command comment is a refusal. At most `claims.create_wait_seconds`, looking every
    `claims.poll_interval_seconds`.
    """
    rules = env.conventions.claims
    started = env.now()
    while True:
        comments = env.remote.comments(number)
        for comment in comments:
            if (
                command_id is not None
                and comment.login == rules.bot_login
                and reply_target(comment.body, rules) == command_id
            ):
                return None, comment.body.split("\n", 1)[1] if "\n" in comment.body else comment.body
        found = latest_held(stored_cards(comments, rules))
        if found is not None and found.card.claimant == user:
            return found.card, ""
        if env.now() - started >= timedelta(seconds=rules.create_wait_seconds):
            return None, (
                f"no card for @{user} on #{number} after {rules.create_wait_seconds} s; the claim may still land "
                "(rerun with --resume), nothing was written"
            )
        env.sleep(rules.poll_interval_seconds)


def _still_true(env: CreateEnv, number: int, user: str) -> str | None:
    """What changed since the card named `user` on issue `number`, or None while it still holds."""
    rules = env.conventions.claims
    comments = env.remote.comments(number)
    found = latest_held(stored_cards(comments, rules))
    if found is None or found.card.claimant != user:
        return f"#{number}: the card no longer names @{user}"
    assignees = env.remote.issue(number).assignees
    if assignees != (user,):
        return f"#{number}: the assignees are now {', '.join(assignees) or 'nobody'}, not only @{user}"
    answered = {reply_target(c.body, rules) for c in comments if c.login == rules.bot_login}
    for comment in comments:
        if comment.user_type == "Bot" or comment.login == user or comment.id in found.card.handled:
            continue
        command = parse_command(comment.body, rules)
        if (
            isinstance(command, Command)
            and command.kind == "claim"
            and comment.created_at <= found.card.claimed_at
            and comment.id not in answered
        ):
            return f"#{number}: an earlier /claim by @{comment.login} is still waiting"
    return None


def _settle(env: CreateEnv, numbers: list[int], user: str) -> str | None:
    """Look every `claims.poll_interval_seconds` for `claims.settle_seconds` that every claim still holds."""
    rules = env.conventions.claims
    end = env.now() + timedelta(seconds=rules.settle_seconds)
    while True:
        for number in numbers:
            problem = _still_true(env, number, user)
            if problem is not None:
                return problem
        if env.now() >= end:
            return None
        env.sleep(rules.poll_interval_seconds)


# --- the command ----------------------------------------------------------------------------------------------------


def _unique_dir(scratch: Path, name: str) -> str:
    """`name`, or `name-2`, `name-3`, ... when the folder is taken (for the second fix task of an issue)."""
    candidate, count = name, 1
    while (scratch / candidate).exists():
        count += 1
        candidate = f"{name}-{count}"
    return candidate


def _write(env: CreateEnv, task: TaskFile) -> Path:
    """Write the task file; an existing one is never overwritten (ConfigError)."""
    path = env.scratch / task.task / env.conventions.tasks.file
    path.parent.mkdir(parents=True, exist_ok=True)
    try:
        with path.open("x", encoding="utf-8", newline="\n") as handle:
            handle.write(render_task_file(task))
    except FileExistsError:
        raise ConfigError(f"{path} exists already; remove it or pick another --name") from None
    return path


def create_task(request: CreateRequest, env: CreateEnv) -> int:
    """Claim the issues and write the task file; 0 when the file was written (or, with --dry-run, would be), else 1."""
    rules = env.conventions.claims
    out = env.out
    user = env.remote.current_user()
    engine = Engine(env.remote, env.conventions, env.now)
    maintainer = engine.is_maintainer(user)
    held = engine.held_cards()
    wanted = (request.fix_of,) if request.fix_of is not None else request.issues
    issues = [env.remote.issue(number) for number in wanted]
    for issue in issues:
        if issue.is_pr or issue.state != "open":
            out(f"refused: #{issue.number} is not an open issue")
            return 1
    kind = request.kind or ("fix" if request.fix_of is not None else default_kind(issues[0]))
    if kind not in env.conventions.tasks.kinds:
        out(f"refused: the kind '{kind}' is not one of {', '.join(env.conventions.tasks.kinds)}")
        return 1
    if (kind == "fix") != (request.fix_of is not None):
        out("refused: a fix task is --fix-of <issue>, and --fix-of makes a fix task")
        return 1
    if request.fix_of is not None:
        return _fix_task(request, env, user, issues[0], held.get(request.fix_of))
    if len(issues) > rules.limits.issues_per_task:
        out(f"refused: a task holds at most {rules.limits.issues_per_task} issues, not {len(issues)}: split it")
        return 1
    # A claim that landed late is resumed under the task id it was made with.
    held_tasks = [held[i.number].task for i in issues if i.number in held and held[i.number].claimant == user]
    resumed_id = next((task for task in held_tasks if task), None) if request.resume else None
    task_id = request.name or resumed_id or f"{issues[0].number}-{slug(issues[0].title)}"
    if not _TASK_ID.match(task_id):
        out(f"refused: '{task_id}' is not a task id (lower case letters, digits and hyphens, 60 at most)")
        return 1
    if (env.scratch / task_id / env.conventions.tasks.file).exists():
        out(f"refused: {env.scratch / task_id / env.conventions.tasks.file} exists already")
        return 1
    blocked_heading = section_heading(env.conventions, "blocked-by")
    to_claim: list[IssueView] = []
    for issue in issues:
        mine = held.get(issue.number)
        if mine is not None and mine.claimant == user:
            if not request.resume:
                out(f"refused: you hold #{issue.number} already (--resume continues a claim that landed late)")
                return 1
            if mine.task != task_id:
                held_as = f"task `{mine.task}`" if mine.task else "no task"
                out(f"refused: you hold #{issue.number} under {held_as}, not `{task_id}`: pass --name to match it")
                return 1
            continue
        refusal = check_claim(
            Command("claim", scope=request.scope or None, task=task_id),
            issue,
            user,
            maintainer,
            mine,
            held.values(),
            env.remote,
            rules,
            blocked_heading,
        )
        if refusal is not None:
            out(f"refused: {refusal.reason}")
            return 1
        to_claim.append(issue)
    overlaps = [
        f"#{card.issue} (@{card.claimant}) shares {', '.join(shared)}"
        for card in held.values()
        if card.task != task_id and card.issue not in request.issues
        for shared in [scopes_overlap(request.scope, card.scope)]
        if shared
    ]
    for line in overlaps:
        out(f"warning: the scope overlaps {line}")
    text = "\n".join(f"{issue.title}\n{issue.body}" for issue in issues)
    leads = env.leads(list(request.terms), text)
    if request.dry_run:
        for issue in to_claim:
            out(f"dry run: would post on #{issue.number}: {claim_comment(request.scope, task_id)}")
        task = build_task_file(request, task_id, kind, user, env.now(), issues, leads, overlaps, env.conventions)
        out(f"dry run: would write {env.scratch / task_id / env.conventions.tasks.file}\n")
        out(render_task_file(task))
        return 0
    numbers = [issue.number for issue in issues]
    claimed: list[int] = []
    for issue in to_claim:
        comment_id = env.remote.post_command(issue.number, claim_comment(request.scope, task_id))
        out(f"posted /claim on #{issue.number}; waiting for the claim card")
        card, why = _wait_for_card(env, issue.number, comment_id, user)
        if card is None:
            out(f"refused: {why}")
            _unclaim_hint(env, claimed)
            return 1
        claimed.append(issue.number)
    mine_now = [n for n in numbers if n in claimed or (n in held and held[n].claimant == user)]
    out(f"claimed; settling for {rules.settle_seconds} s")
    problem = _settle(env, numbers, user)
    if problem is not None:
        out(f"stopped: {problem}; nothing was written")
        _unclaim_hint(env, mine_now)
        return 1
    cards = {number: _held_card(env.remote, env.conventions, number) for number in numbers}
    gone = [number for number, card in cards.items() if card is None or card.claimant != user]
    if gone:
        out(f"stopped: the claim on {', '.join(f'#{n}' for n in gone)} is gone; nothing was written")
        _unclaim_hint(env, [n for n in mine_now if n not in gone])
        return 1
    claimed_at = min(card.claimed_at for card in cards.values() if card is not None)
    task = build_task_file(request, task_id, kind, user, claimed_at, issues, leads, overlaps, env.conventions)
    path = _write(env, task)
    for number in numbers:
        env.remote.post_command(number, "/progress assigned")
    out(f"wrote {path}")
    return 0


def _unclaim_hint(env: CreateEnv, numbers: list[int]) -> None:
    """Say how to give back the claims that were made before the task stopped."""
    if numbers:
        env.out(f"claimed: {', '.join(f'#{n}' for n in numbers)} (write `/unclaim` on each to give them back)")


def _fix_task(request: CreateRequest, env: CreateEnv, user: str, issue: IssueView, card: Card | None) -> int:
    """A fix task reuses the claim the claimant holds: no `/claim`, no limit, `/progress fix`."""
    out = env.out
    if card is None or card.claimant != user:
        out(f"refused: a fix task needs your own held claim on #{issue.number}")
        return 1
    task_id = _unique_dir(env.scratch, request.name or f"{issue.number}-fix")
    if not _TASK_ID.match(task_id):
        out(f"refused: '{task_id}' is not a task id (lower case letters, digits and hyphens, 60 at most)")
        return 1
    leads = env.leads(list(request.terms), f"{issue.title}\n{issue.body}")
    scope = request.scope or card.scope
    fix_request = CreateRequest((issue.number,), request.terms, scope, "fix", task_id, request.fix_of)
    task = build_task_file(fix_request, task_id, "fix", user, card.claimed_at, [issue], leads, [], env.conventions)
    if request.dry_run:
        out(f"dry run: would write {env.scratch / task_id / env.conventions.tasks.file}\n")
        out(render_task_file(task))
        return 0
    path = _write(env, task)
    env.remote.post_command(issue.number, "/progress fix")
    out(f"wrote {path}")
    return 0


def match_lines(matches: list[Match]) -> list[str]:
    """One printable line per match: `source  title  where  (why)`."""
    return [f"{m.entry.source:9} {m.entry.title}  {m.entry.where}  ({m.reason})" for m in matches]
