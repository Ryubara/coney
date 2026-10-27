# SPDX-License-Identifier: GPL-3.0-or-later
"""`claim check-pr`: the issues a pull request closes must be claimed by its author (docs/guides/issues.md#claiming).

Work starts with a claim, for everyone. The tools hold agents to it (`task start` and `pr open` refuse a task whose
claim is not the signed-in account's); this is the same rule for a pull request opened any other way, run by the `claim`
job of `.github/workflows/pr.yml`. Every issue the description closes (`Fixes #N`, at most `claims.closing_per_event`)
must have a held claim card, and the card must name the pull request's author, unless the author is a maintainer
(write permission or above), whose pull request may carry anyone's claimed issue, as the claim Action counts it.

`claims.require_on_pr` decides what a problem does: true fails the check, false (until the cutover) prints each as a
GitHub warning annotation and passes. A bot's pull request (Dependabot) is not checked. Only the event file is read
for the pull request; the cards are read from GitHub.
"""

from __future__ import annotations

from dataclasses import dataclass, field
from typing import Any

from coney_tools.conventions import Conventions
from coney_tools.issue_claim_engine import latest_held, stored_cards
from coney_tools.issue_claim_remote import Remote
from coney_tools.issue_claim_rules import is_maintainer


@dataclass
class PrCheck:
    """What the check found: the problems (each an issue not claimed by the author) and plain notes."""

    problems: list[str] = field(default_factory=list)
    notes: list[str] = field(default_factory=list)


def check_pull_request(payload: dict[str, Any], remote: Remote, conventions: Conventions) -> PrCheck:
    """Check the `pull_request` of an event payload against the claim cards on GitHub."""
    # The `Fixes #N` reading is the claim Action's own; imported here because the command module imports this one.
    from coney_tools.issue_claim_cli import closing_numbers

    result = PrCheck()
    pull = payload.get("pull_request")
    if not isinstance(pull, dict):
        result.notes.append("no pull request in the event: nothing to check")
        return result
    user = pull.get("user") if isinstance(pull.get("user"), dict) else {}
    author = str(user.get("login", "")) if isinstance(user, dict) else ""
    if isinstance(user, dict) and user.get("type") == "Bot":
        result.notes.append(f"{author} is a bot: its pull request closes no claimed work")
        return result
    numbers, ignored = closing_numbers(str(pull.get("body") or ""), conventions)
    if not numbers:
        result.notes.append("the description closes no issue (no `Fixes #N` line): nothing to check")
        return result
    if ignored:
        result.notes.append(
            f"{ignored} more `Fixes #N` line(s) not checked: {conventions.claims.closing_per_event} at most"
        )
    rules = conventions.claims
    association = str(pull.get("author_association") or "NONE")
    maintainer = is_maintainer(remote.permission(author), association, rules)
    for number in numbers:
        held = latest_held(stored_cards(remote.comments(number), rules))
        if held is None:
            result.problems.append(
                f"#{number} has no claim: work starts with a claim (`task create`, or a `/claim` comment on the "
                "issue), then the pull request"
            )
        elif held.card.claimant != author and not maintainer:
            result.problems.append(
                f"#{number} is claimed by {held.card.claimant}, not by the author {author}: only the claimant (or a "
                "maintainer) opens the pull request that closes it"
            )
        else:
            result.notes.append(f"#{number}: claimed by {held.card.claimant}")
    return result


def report(result: PrCheck, conventions: Conventions, out: Any = print) -> int:
    """Print the result for the Actions log; 1 when a problem fails the check (`claims.require_on_pr`), else 0."""
    for note in result.notes:
        out(f"claim check: {note}")
    enforced = conventions.claims.require_on_pr
    for problem in result.problems:
        out(f"::error::claim check: {problem}" if enforced else f"::warning::claim check: {problem}")
    if result.problems and not enforced:
        out("claim check: not enforced until the cutover (`claims.require_on_pr` is false): warnings only")
    return 1 if result.problems and enforced else 0
