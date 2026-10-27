# SPDX-License-Identifier: GPL-3.0-or-later
"""The text and the plan of the hygiene workflow's comments (`coney-tools repo post-findings`): pure, no network.

The audit (`coney-tools repo audit`) reports where the repository departs from `.github/conventions.yml`. This module
turns the findings of the `github` scope into what the workflow writes back, and decides which writes are needed:

* **A sticky comment per item.** An issue or pull request with findings gets one comment, edited in place whenever its
  findings change. It is never deleted: when the findings are gone it says so, so the item's history stays readable and
  a comment that reappears is the same comment. Its first line is the HTML comment `<!-- marker -->`
  (`hygiene.comments.marker`), and only a comment by `hygiene.comments.author` counts as the workflow's own, so nobody
  can have the workflow edit another comment by pasting the marker.
* **One report comment.** The weekly sweep keeps the whole audit in one comment on the report issue, edited in place.
* **Report-only checks.** A comment is activity, so a finding about idleness (`hygiene.comments.report_only`) would
  hide itself; those findings go in the report only.

Everything GitHub sends (a title, a label, a body quoted in a finding) is untrusted text: `neutralize` stops it from
mentioning people, opening links or images, closing the marker's HTML comment or cross-referencing other items.
"""

from __future__ import annotations

import re
from collections.abc import Iterable, Sequence
from dataclasses import dataclass

from coney_tools.audit import Finding, Report
from coney_tools.conventions import Comments

#: An item a finding is about, when it is an issue or pull request (`#201`); other items (a file) are report-only.
_ITEM = re.compile(r"^#(\d+)$")
#: Stops a mention or a reference from linking (GitHub looks for `@name` and `#12`).
_ZERO_WIDTH = "​"
#: The most a comment may hold is 65536 characters; the report stays well below it.
REPORT_LIMIT = 60000
#: A finding's message is cut to this many characters in a comment.
MESSAGE_LIMIT = 400
#: Findings of one check shown in the report before "and N more", tried largest first until the report fits.
REPORT_SHOWN = (20, 10, 5, 2, 0)


@dataclass(frozen=True)
class Sticky:
    """A comment of the workflow that already exists on GitHub."""

    comment_id: int
    #: The issue or pull request it is on.
    number: int
    body: str


@dataclass(frozen=True)
class Action:
    """One write the workflow needs to make."""

    #: `create` (a new comment), `update` (new findings) or `resolve` (the findings are gone).
    kind: str
    number: int
    #: The comment to edit; None for `create`.
    comment_id: int | None
    body: str


def item_marker(comments: Comments) -> str:
    """The first line of an item's comment."""
    return f"<!-- {comments.marker} -->"


def report_marker(comments: Comments) -> str:
    """The first line of the report comment."""
    return f"<!-- {comments.marker}:report -->"


def neutralize(text: str, references: bool = False) -> str:
    """`text` made safe to quote in a comment: one line, no HTML, no mention, no link text, no image.

    `references` also stops `#12` from linking, for the report, which names many items and must not write a
    cross-reference into each of their timelines.
    """
    text = " ".join(text.split())
    text = text.replace("<", "&lt;").replace("@", "@" + _ZERO_WIDTH)
    text = text.replace("[", "\\[").replace("]", "\\]")
    if references:
        text = re.sub(r"#(?=\d)", "#" + _ZERO_WIDTH, text)
    return text if len(text) <= MESSAGE_LIMIT else text[: MESSAGE_LIMIT - 3] + "..."


def item_number(item: str) -> int | None:
    """The issue or pull request number of a finding's item (`#201`), or None for anything else."""
    found = _ITEM.match(item)
    return int(found.group(1)) if found else None


def group_by_item(findings: Iterable[Finding], comments: Comments) -> dict[int, list[Finding]]:
    """The findings for an item's comment, by item number: about an issue or pull request, and not report-only."""
    groups: dict[int, list[Finding]] = {}
    for finding in findings:
        number = item_number(finding.item)
        if number is not None and finding.check not in comments.report_only:
            groups.setdefault(number, []).append(finding)
    return groups


def _plural(count: int, word: str) -> str:
    """`1 thing`, `2 things`."""
    return f"{count} {word}{'' if count == 1 else 's'}"


def render_item(comments: Comments, findings: Sequence[Finding]) -> str:
    """The comment for an item with findings; the same findings always give the same text (so no edit is needed)."""
    lines = [
        item_marker(comments),
        f"**Repo hygiene** found {_plural(len(findings), 'thing')} to fix here:",
        "",
    ]
    for finding in findings:
        who = " (a maintainer decides)" if finding.fix == "maintainer" else ""
        lines.append(f"- `{finding.check}` ({finding.severity}): {neutralize(finding.message)}{who}")
    lines += [
        "",
        "The `hygiene` workflow updates this comment when the item changes and says so here when everything is fixed. "
        "The rules are in `.github/conventions.yml`; `coney-tools repo audit --github` shows the same findings.",
    ]
    return "\n".join(lines) + "\n"


def render_resolved(comments: Comments) -> str:
    """The comment of an item whose findings are all fixed."""
    return (
        f"{item_marker(comments)}\n**Repo hygiene** found nothing to fix here now. "
        "The `hygiene` workflow will edit this comment if something comes up again.\n"
    )


def render_report(comments: Comments, report: Report, taken: str) -> str:
    """The sweep's report comment: the counts per check, then the findings of each check, cut to fit one comment.

    `taken` is when the audit ran (a timestamp text); the report is the only comment that changes every week.
    """
    for shown in REPORT_SHOWN:
        text = _report_text(comments, report, taken, shown)
        if len(text) <= REPORT_LIMIT:
            return text
    return _report_text(comments, report, taken, 0)[:REPORT_LIMIT]


def _report_text(comments: Comments, report: Report, taken: str, shown: int) -> str:
    """The report with at most `shown` findings per check."""
    lines = [
        report_marker(comments),
        "## Repo hygiene report",
        "",
        f"As of {taken}: {_plural(report.count('error'), 'error')}, {_plural(report.count('warn'), 'warning')} "
        f"from {_plural(len(report.ran), 'check')} ({', '.join(report.scopes)}).",
        "",
    ]
    lines += [f"> {neutralize(notice)}" for notice in report.notices]
    if report.notices:
        lines.append("")
    by_check: dict[str, list[Finding]] = {}
    for finding in report.findings:
        by_check.setdefault(finding.check, []).append(finding)
    if not by_check:
        lines.append("Nothing to report.")
    for check_id, found in by_check.items():
        lines.append(f"### `{check_id}`: {len(found)} ({found[0].severity}, fix: {found[0].fix})")
        lines.append("")
        for finding in found[:shown]:
            lines.append(f"- `{finding.item}`: {neutralize(finding.message, references=True)}")
        if len(found) > shown:
            lines.append(f"- ... and {len(found) - shown} more (`coney-tools repo audit --github --json` lists all)")
        lines.append("")
    lines.append(
        "This comment is rewritten by the weekly `hygiene` workflow; fix a finding at its item and it leaves the next "
        "report. Idleness findings appear only here, because a comment on an idle item would end its idleness."
    )
    return "\n".join(lines) + "\n"


def plan_items(
    comments: Comments,
    groups: dict[int, list[Finding]],
    stickies: dict[int, Sticky],
    scope: Iterable[int] | None = None,
    limit: int | None = None,
) -> list[Action]:
    """The writes that make every item's comment match its findings.

    `scope` limits the plan to those items (an event is about one item); None means every item with findings or with a
    comment of ours. Fixes come first (a `resolve` before an `update` before a `create`), then by item number, and
    `limit` keeps only the first N so that a first run cannot flood the repository.
    """
    wanted = set(groups) | set(stickies) if scope is None else set(scope)
    resolved = render_resolved(comments)
    actions: list[Action] = []
    for number in sorted(wanted):
        found = groups.get(number)
        sticky = stickies.get(number)
        if found:
            body = render_item(comments, found)
            if sticky is None:
                actions.append(Action("create", number, None, body))
            elif sticky.body != body:
                actions.append(Action("update", number, sticky.comment_id, body))
        elif sticky is not None and sticky.body != resolved:
            actions.append(Action("resolve", number, sticky.comment_id, resolved))
    order = {"resolve": 0, "update": 1, "create": 2}
    actions.sort(key=lambda action: (order[action.kind], action.number))
    return actions if limit is None else actions[:limit]


def plan_report(sticky: Sticky | None, body: str, issue: int) -> Action | None:
    """The write that makes the report comment on `issue` equal `body`, or None when it already is."""
    if sticky is None:
        return Action("create", issue, None, body)
    if sticky.body == body:
        return None
    return Action("update", issue, sticky.comment_id, body)


def report_issue_body() -> str:
    """The body of the report issue when the workflow must create it: task-form shaped, so the audit passes it."""
    return (
        "### Summary\n\nThe weekly `hygiene` workflow keeps one comment on this issue with everything "
        "`coney-tools repo audit` finds: the counts per check and the findings. Do not edit the comment; fix the "
        "findings at their items.\n\n"
        "### Area\n\nA-tools\n\n"
        "### Research page\n\nNone.\n\n"
        '### What "done" means\n\nThis issue stays open; the report comment is empty of findings.\n\n'
        "## Screenshots\n\nScreenshots: none (repository tooling)\n"
    )
