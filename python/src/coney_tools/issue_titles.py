# SPDX-License-Identifier: GPL-3.0-or-later
"""`coney-tools issue titles`: the open issues whose title breaks `area: Verb the rest`, each with a proposed title.

An issue title reads like a pull request title (`commits.title` of `.github/conventions.yml` is the one definition), so
the old `Bug: ` and `Task: ` prefixes and free-form titles all break the rule once `issue check` enforces it
(docs/guides/issues.md#titles). This module proposes a rewrite for each and says how sure it is: `auto` when only a
prefix was dropped and the area and verb were already in the title or are plain from the labels, `review` when it had to
guess a verb, take the area from the `docs` label or cut the title to length. It only proposes; the command prints the
`gh issue edit` lines and the coordinator applies them (`--apply` applies the `auto` ones, `--include-review` the rest).
"""

from __future__ import annotations

import re
from collections.abc import Iterable, Mapping
from dataclasses import dataclass
from typing import Any

from coney_tools.conventions import Conventions
from coney_tools.repo_checks import check_title

#: A prefix the old forms (and habit) put before a title.
_PREFIX = re.compile(r"^\s*\[?(?:bug|task|parity|enhancement|feature)\]?\s*[:\-]\s*", re.IGNORECASE)
_AREA_FORM = re.compile(r"^(?P<area>[^\s:]+):\s+(?P<rest>\S.*)$")
#: Words that mean an allowed verb, in the forms a title uses them.
_VERB_FORMS = {
    "adds": "Add", "added": "Add", "adding": "Add",
    "fixes": "Fix", "fixed": "Fix", "fixing": "Fix",
    "updates": "Update", "updated": "Update", "updating": "Update",
    "removes": "Remove", "removed": "Remove", "removing": "Remove",
    "rebuilds": "Rebuild", "rebuilt": "Rebuild",
    "reworks": "Rework", "reworked": "Rework",
    "improves": "Improve", "improved": "Improve",
    "documents": "Document", "documented": "Document",
}  # fmt: skip
#: Words close to an allowed verb in meaning.
_VERB_SYNONYMS = {
    "implement": "Add", "create": "Add", "port": "Add", "reimplement": "Add", "write": "Add", "support": "Add",
    "build": "Add", "introduce": "Add",
    "refactor": "Rework", "rewrite": "Rework", "redo": "Rework", "restructure": "Rework", "replace": "Rework",
    "cleanup": "Improve", "tidy": "Improve", "speed": "Improve",
    "delete": "Remove", "drop": "Remove",
    "change": "Update", "bump": "Update", "extend": "Update",
}  # fmt: skip
#: The hygiene report issue's title before the area-and-verb rule; the weekly sweep finds it by title.
OLD_REPORT_TITLE = "Repo hygiene"


@dataclass(frozen=True)
class Retitle:
    """One issue whose title breaks the rule, with the title proposed for it."""

    number: int
    current: str
    proposed: str
    #: `auto` or `review`.
    confidence: str
    #: What was guessed or changed, for the person who reads a `review` line.
    why: str

    def command(self) -> str:
        """The `gh issue edit` command that applies it (the title is double-quoted, `"` and `\\` escaped)."""
        title = self.proposed.replace("\\", "\\\\").replace('"', '\\"')
        return f'gh issue edit {self.number} --title "{title}"'


def _lower_first(text: str) -> str:
    """`text` with its first letter lower case, unless it starts an acronym or an identifier."""
    word = text.split(" ", 1)[0]
    if len(word) > 1 and (word[1].isupper() or any(c in word for c in "_:0123456789")):
        return text
    return text[:1].lower() + text[1:]


def _area_from_labels(labels: Iterable[str], table: Mapping[str, str]) -> tuple[str | None, bool]:
    """The title area the labels give (the first table entry the issue carries) and whether it came from `docs`."""
    names = set(labels)
    for label, area in table.items():
        if label in names:
            return area, label == "docs"
    return None, False


def _cut(title: str, limit: int) -> str:
    """`title` cut at a word to at most `limit` characters, with no trailing punctuation."""
    if len(title) <= limit:
        return title
    return title[:limit].rsplit(" ", 1)[0].rstrip(" ,;:-.")


def propose_title(number: int, title: str, labels: Iterable[str], conventions: Conventions) -> Retitle | None:
    """A proposal for an issue's title, or None when the title already passes."""
    rules = conventions.commits.title
    label_names = tuple(labels)
    if not check_title(title, rules):
        return None
    if title == OLD_REPORT_TITLE:
        fixed = conventions.hygiene.report_issue.title
        return Retitle(
            number, title, fixed, "auto", "the hygiene report issue; the weekly sweep finds it by this title"
        )
    notes: list[str] = []
    confidence = "auto"
    text = " ".join(_PREFIX.sub("", title).split())
    area: str | None = None
    form = _AREA_FORM.match(text)
    if form and form.group("area") in rules.areas:
        area, text = form.group("area"), form.group("rest")
    if area is None:
        area, from_docs = _area_from_labels(label_names, conventions.issues.title_area_by_label)
        if area is None:
            area = "gameplay" if {"bug", "parity"} & set(label_names) else "tools"
            notes.append(f"no area label: guessed {area}")
            confidence = "review"
        elif from_docs:
            notes.append("area from the docs label (docs or research?)")
            confidence = "review"
    first, _, rest = text.partition(" ")
    verb = _VERB_FORMS.get(first.lower()) or (first.capitalize() if first.capitalize() in rules.verbs else None)
    if verb is None and (synonym := _VERB_SYNONYMS.get(first.lower())) is not None:
        verb = synonym
        notes.append(f"'{first}' read as {synonym}")
        confidence = "review"
    if verb is not None and not rest.strip():
        verb = None  # a one-word title has nothing after its verb
    if verb is not None:
        body = rest.strip()
    else:
        verb = "Fix" if {"bug", "parity"} & set(label_names) else "Add"
        body = _lower_first(text)
        notes.append(f"no verb: guessed {verb}")
        confidence = "review"
    proposed = f"{area}: {verb} {body}".rstrip(" .")
    if len(proposed) > rules.max_length:
        proposed = _cut(proposed, rules.max_length)
        notes.append(f"cut to {rules.max_length} characters")
        confidence = "review"
    remaining = check_title(proposed, rules)
    if remaining:
        notes.append("still breaks the rule: " + "; ".join(remaining))
        confidence = "review"
    return Retitle(number, title, proposed, confidence, "; ".join(notes) or "prefix dropped")


def list_retitles(issues: Iterable[Mapping[str, Any]], conventions: Conventions) -> list[Retitle]:
    """The proposals for a list of issues as `gh issue list --json number,title,labels` returns them."""
    found: list[Retitle] = []
    for item in issues:
        labels = [str(label["name"]) for label in item.get("labels", []) if isinstance(label, dict)]
        proposal = propose_title(int(item["number"]), str(item["title"]), labels, conventions)
        if proposal is not None:
            found.append(proposal)
    return sorted(found, key=lambda retitle: retitle.number)
