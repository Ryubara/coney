# SPDX-License-Identifier: GPL-3.0-or-later
"""`workspace clean` archives the scratch folders of fix tasks whose pull request merged.

A fix task (`kind: fix`, a `parent` in its task file) exists to repair its parent's pull request. Once that pull request
is merged the fix can never be released, but its folder still holds a task file, so `task slots` and the slot-fill check
went on listing it as work to start. `task slots` and the check now drop such a fix by themselves (`task_slots.scan`);
this module moves its folder out of the way, to `<scratch>/archive/<task>`, so nothing reads it any more and nothing is
lost: the stamps, logs and notes stay in the archive.

Only the merged state is read here (`workspace_clean.MergedHeads`, the one `gh` call `clean` makes already); a closed
pull request may be reopened, so its fix is left to the scan, which only hides it. A fix is left alone while it may
still be worked: an agent was just started for it (a fresh `slot.json`), its unit record names a worktree that exists,
or the parent's worktree holds commits the merged pull request does not cover.
"""

from __future__ import annotations

import shutil
from collections.abc import Callable
from dataclasses import dataclass
from datetime import datetime
from pathlib import Path

from coney_tools import task_slots
from coney_tools.conventions import Conventions
from coney_tools.unit_record import UnitRecordError, front_of, read_record

#: The folder of the scratch directory that archived fix tasks move into.
ARCHIVE_DIR = "archive"
#: Looks up the head commits of the merged pull requests from a branch; None when GitHub cannot be asked
#: (`workspace_clean.MergedHeads`).
MergedHeads = Callable[[str], list[str] | None]
#: Whether the work in a worktree is covered by one of the merged heads (`workspace_clean._covered_by_merged_pr`).
Covered = Callable[[Path, list[str]], bool]


@dataclass(frozen=True)
class DeadFix:
    """A fix task whose parent's pull request merged."""

    task: str
    folder: Path
    #: The parent task and the branch its pull request came from.
    parent: str
    branch: str


def _parent_of(folder: Path, conventions: Conventions) -> str:
    """The `parent` of a fix task's folder; empty when its task file is not a fix's or cannot be read."""
    front = front_of(folder / conventions.tasks.file)
    if front is None or front.get("kind") != "fix":
        return ""
    return str(front.get("parent") or "")


def _record_worktree(folder: Path, conventions: Conventions) -> Path | None:
    """The worktree the unit record in `folder` names; None when there is no record, or it cannot be read."""
    try:
        return read_record(folder / conventions.tasks.start.unit_file).worktree
    except UnitRecordError:
        return None


def dead_fixes(
    scratch: Path,
    merged_heads: MergedHeads,
    conventions: Conventions,
    now: datetime,
    covered: Covered = lambda worktree, heads: True,
) -> list[DeadFix]:
    """The fix task folders of `scratch` whose parent's branch has a merged pull request, by name.

    Left out: a folder with no readable task file, one that is not a fix's, a fix whose branch has no merged pull
    request (or for which GitHub could not say), and a fix that may still be worked: a `slot.json` younger than
    `tasks.start.slots.reserve_minutes`, a unit record whose worktree exists, or a parent's worktree that exists with
    commits `covered` does not find in a merged head."""
    found: list[DeadFix] = []
    if not scratch.is_dir():
        return found
    for folder in sorted(scratch.iterdir()):
        if not folder.is_dir() or folder.name == ARCHIVE_DIR:
            continue
        parent = _parent_of(folder, conventions)
        if not parent:
            continue
        mine = _record_worktree(folder, conventions)
        theirs = _record_worktree(scratch / parent, conventions)
        branch = _branch_of(scratch / parent, parent, conventions)
        heads = merged_heads(branch)
        if not heads or task_slots.fresh_slot(folder, now, conventions):
            continue
        if mine is not None and mine.is_dir():
            continue
        if theirs is not None and theirs.is_dir() and not covered(theirs, heads):
            continue
        found.append(DeadFix(folder.name, folder, parent, branch))
    return found


def _branch_of(parent_folder: Path, parent: str, conventions: Conventions) -> str:
    """The branch of the parent's unit (its record), else the parent's id, which is the branch's name by default."""
    try:
        return read_record(parent_folder / conventions.tasks.start.unit_file).branch
    except UnitRecordError:
        return parent


def archive(fix: DeadFix, scratch: Path) -> str | None:
    """Move the fix's folder to `<scratch>/archive/<task>` (`-2`, `-3`... when that name is taken); None when it
    worked, else why not (a file in it is open)."""
    root = scratch / ARCHIVE_DIR
    target = root / fix.task
    number = 2
    while target.exists():
        target = root / f"{fix.task}-{number}"
        number += 1
    try:
        root.mkdir(parents=True, exist_ok=True)
        shutil.move(str(fix.folder), str(target))
    except OSError as error:
        return str(error)
    return None
