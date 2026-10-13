# SPDX-License-Identifier: GPL-3.0-or-later
"""`coney-tools repo pr-health --labels`: keep a label on each open pull request that conflicts or fails a check.

`repo pr-health` finds the stuck pull requests; this module turns the findings into labels so that the pull request
list shows them without running anything (`hygiene.pr_health.labels` in `.github/conventions.yml` maps each cause to
its label, `S-conflict` and `S-failing`). A pull request with a cause gets that cause's label, one without it loses
the label, and no other label is ever added or removed. Without `--apply` the changes are only printed.
"""

from __future__ import annotations

from collections.abc import Iterable, Sequence
from dataclasses import dataclass
from typing import TYPE_CHECKING, Any
from urllib.parse import quote

from coney_tools.config import ConfigError

if TYPE_CHECKING:
    from coney_tools.audit_github import Runner
    from coney_tools.conventions import PrHealth
    from coney_tools.pr_health import Health

#: The cause whose answer GitHub computes on demand: while it is unknown the label is left as it is.
CONFLICT = "conflict"


@dataclass(frozen=True)
class LabelChange:
    """One label to add to, or remove from, a pull request."""

    number: int
    label: str
    #: True to add the label, False to remove it.
    add: bool
    #: Why: the finding (`conflict with main`) for an addition, `cleared` for a removal.
    why: str

    def line(self, apply: bool) -> str:
        """The text line: `add S-conflict to #12 (conflict with main)`, with `would ` in front when not written."""
        verb = "add" if self.add else "remove"
        place = "to" if self.add else "from"
        return f"{'' if apply else 'would '}{verb} {self.label} {place} #{self.number} ({self.why})"

    def as_json(self) -> dict[str, object]:
        """The change as a JSON object."""
        return {"number": self.number, "label": self.label, "action": "add" if self.add else "remove", "why": self.why}


def unknown_numbers(health: Health) -> list[int]:
    """The pull requests whose mergeability GitHub has not computed yet."""
    return [note.number for note in health.notes if note.what == "mergeable-unknown"]


def plan(pulls: Sequence[dict[str, Any]], health: Health, rules: PrHealth) -> list[LabelChange]:
    """The label changes that make every open pull request carry the labels of its causes and no other managed label.

    `pulls` is gh's JSON (it holds each pull request's `labels`); `health` is the judgement of the same read. A pull
    request whose mergeability is unknown keeps the label of `conflict` as it is, since "not known" is neither
    "conflicts" nor "does not". A draft is never stuck, so it carries none of the labels.
    """
    undecided = set(unknown_numbers(health))
    reasons: dict[int, dict[str, str]] = {}
    for item in health.stuck:
        label = rules.labels.get(item.cause)
        if label:
            reasons.setdefault(item.number, {})[label] = f"{item.cause} {item.detail}"
    managed = set(rules.labels.values())
    kept = rules.labels.get(CONFLICT, "")
    changes: list[LabelChange] = []
    for pull in sorted(pulls, key=lambda item: int(item["number"])):
        number = int(pull["number"])
        have = {str(entry.get("name")) for entry in pull.get("labels") or [] if isinstance(entry, dict)} & managed
        want = set(reasons.get(number, {}))
        if number in undecided:
            have.discard(kept)
            want.discard(kept)
        changes += [LabelChange(number, label, True, reasons[number][label]) for label in sorted(want - have)]
        changes += [LabelChange(number, label, False, "cleared") for label in sorted(have - want)]
    return changes


def write(changes: Iterable[LabelChange], run: Runner) -> None:
    """Carry out the changes through the GitHub REST labels endpoints (`run` is `gh`); ConfigError when one fails."""
    for change in changes:
        path = f"repos/{{owner}}/{{repo}}/issues/{change.number}/labels"
        try:
            if change.add:
                run(["api", "--method", "POST", path, "-f", f"labels[]={change.label}", "--silent"])
            else:
                run(["api", "--method", "DELETE", f"{path}/{quote(change.label, safe='')}", "--silent"])
        except OSError as error:
            raise ConfigError(f"cannot change the labels of #{change.number} with gh ({error})") from error
