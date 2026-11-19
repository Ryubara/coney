# SPDX-License-Identifier: GPL-3.0-or-later
"""The Done items of a task file and what each says about its pull request (docs/guides/pr.md#done-items).

A task's Done section is the issue's "what done means", quoted by `task create`. The engineer marks in `TASK.md` what
the pull request does with each item:

- a plain bullet, or one ticked `- [x]`, is **met**: the generated Checklist ticks it;
- a bullet that ends `(left for #N)` or `(left for task <id>)` is **left** to that issue or task: the generated
  description lists it under its own label with the note, never as a checkbox (CI refuses an unticked `- [ ]` item,
  `pull_requests.checklist`), and the task must say `finishes: false` in its front block, so the Summary says `Refs #N`
  and merging closes nothing;
- an unticked `- [ ]` bullet with no note is a mistake: it says neither that it is met nor where it goes.

`pr text` writes the Checklist and the closing lines from these items, and `pr verify` refuses a task whose items and
`finishes` contradict each other, so a description never claims a Done item the pull request leaves to a follow-up.
"""

from __future__ import annotations

import re
from dataclasses import dataclass

#: The label of the list of items a pull request leaves to a follow-up, in the generated Checklist.
LEFT_LABEL = "Left for a follow-up (not part of this pull request):"
#: The label of the list of items it finishes.
DONE_LABEL = "Done:"

#: The Done section `task create` writes when the issue has none: it names no criterion.
_PLACEHOLDER = re.compile(r"^See issue #\d+\.$")
_BULLET = re.compile(r"^\s*[-*+]\s+(?:\[(?P<box>[ xX])\]\s+)?(?P<text>\S.*)$")
#: The note that ends an item left to a follow-up: an issue (`#1500`) or a task (`task 1501-add-the-check`).
_NOTE = re.compile(r"\s*\((?i:left)\s+(?i:for|to)\s+(?P<target>#\d+|(?i:task)\s+[a-z0-9][a-z0-9-]{0,59})\)[\s.;]*$")


@dataclass(frozen=True)
class DoneItem:
    """One criterion of a Done section, whole (its wrapped lines joined), with what the engineer marked on it."""

    #: The criterion without its checkbox and without the note.
    text: str
    #: The issue (`#1500`) or task (`task 1501-add-the-check`) that holds the item, when the note says so.
    left_for: str | None = None
    #: What is wrong with how the item is marked (a phrase that follows its text), or None.
    problem: str | None = None

    @property
    def met(self) -> bool:
        """Whether the pull request finishes the item: marked as met (or unmarked) and not left."""
        return self.left_for is None and self.problem is None


def _squash(text: str) -> str:
    """`text` with every run of whitespace made one space."""
    return " ".join(text.split())


def _item(text: str, box: str | None) -> DoneItem:
    """The item written as `text` (wrapped lines joined) with its checkbox `box` (None: it has none)."""
    note = _NOTE.search(text)
    if note is None:
        if box == " ":
            return DoneItem(
                text,
                None,
                "is unticked and names no follow-up: tick it when the pull request finishes it, or end it with "
                "'(left for #N)' or '(left for task <id>)' when another issue or task holds it",
            )
        return DoneItem(text)
    target = _squash(note.group("target")).lower()
    shown = text[: note.start()].rstrip()
    if box is not None and box.lower() == "x":
        return DoneItem(shown, target, f"is ticked and also left for {target}: it is either met or left")
    return DoneItem(shown, target)


def read(section: str) -> list[DoneItem]:
    """The items of a Done section's text, one per bullet (the `> ` marks `task create` quotes it with are read past).

    A bullet is whole: the lines that wrap it (up to a blank line or the next bullet) are part of it, so a criterion is
    never ticked with half of it cut off. A section of prose with no bullet gives one item per paragraph. The
    placeholder `task create` writes for an issue with no Done section (`See issue #N.`) gives none.
    """
    lines = [re.sub(r"^\s*>\s?", "", line) for line in section.splitlines()]
    bullets: list[tuple[str | None, list[str]]] = []
    reading = False  # a bullet is open: the lines after it that are not a bullet or blank wrap it
    for line in lines:
        bullet = _BULLET.match(line)
        if bullet:
            bullets.append((bullet.group("box"), [bullet.group("text")]))
            reading = True
        elif reading and line.strip():
            bullets[-1][1].append(line.strip())
        else:
            reading = False
    pairs = [(box, _squash(" ".join(parts))) for box, parts in bullets]
    if not pairs:
        pairs = [(None, _squash(p)) for p in re.split(r"\n\s*\n", "\n".join(lines)) if p.strip()]
    return [_item(text, box) for box, text in pairs if not _PLACEHOLDER.match(text)]


def _shown(text: str) -> str:
    """An item's text cut for a message."""
    return text if len(text) <= 60 else text[:57] + "..."


def problems(items: list[DoneItem], finishes: bool) -> list[str]:
    """What is wrong with the marks on `items` for a task that closes its issues (`finishes`) or not.

    One message each, with no stage prefix (`pr text` says `text: TASK.md: ...`, `pr verify` says `done: ...`): an item
    marked wrongly, an item left to a follow-up in a task that would still write `Fixes` (the description would close
    its issue with work left), and a task that leaves every item (it finishes nothing).
    """
    found: list[str] = []
    for item in items:
        shown = _shown(item.text)
        if item.problem is not None:
            found.append(f"Done item '{shown}' {item.problem}")
        elif item.left_for is not None and finishes:
            found.append(
                f"Done item '{shown}' is left for {item.left_for} but the task still finishes its issue (the "
                "description would say Fixes and close it with work left); write `finishes: false` in TASK.md's front "
                "block, so the Summary says Refs, or finish the item"
            )
    if items and all(item.left_for is not None for item in items):
        found.append(
            "every Done item is left for a follow-up, so this pull request finishes nothing; take the finished work "
            "out of the left items, or do not open it"
        )
    return found


def checklist(items: list[DoneItem]) -> str:
    """The generated Checklist: the met items ticked, then the left ones under their own label, each with its note.

    The left items are plain bullets, not checkboxes: CI refuses an unticked `- [ ]` item, and a task a pull request
    does not finish is not part of it. Empty when there is no item."""
    met = [item for item in items if item.met]
    left = [item for item in items if item.left_for is not None]
    parts: list[str] = []
    if met:
        parts.append(DONE_LABEL + "\n\n" + "\n".join(f"- [x] {item.text}" for item in met))
    if left:
        parts.append(LEFT_LABEL + "\n\n" + "\n".join(f"- {item.text} (left for {item.left_for})" for item in left))
    return "\n\n".join(parts)
