# SPDX-License-Identifier: GPL-3.0-or-later
"""`workspace clean` archives the scratch folders of fix tasks whose pull request merged.

A fix task (`kind: fix`, a `parent` in its task file) exists to repair its parent's pull request. Once that pull request
is merged the fix can never be released, but its folder still holds a task file, so `task slots` and the slot-fill check
went on listing it as work to start. `task slots` and the check now drop such a fix by themselves (`task_slots.scan`);
this module moves its folder out of the way, to `<scratch>/archive/<task>`, so nothing reads it any more and nothing is
lost: the stamps, logs and notes stay in the archive.

Only the merged state is read here (`workspace_clean.MergedHeads`, the one `gh` call `clean` makes already); a closed
pull request may be reopened, so its fix is left to the scan, which only hides it.
"""

from __future__ import annotations

import shutil
from collections.abc import Callable
from dataclasses import dataclass
from pathlib import Path

import yaml

from coney_tools.unit_record import UnitRecordError, read_record

#: The folder of the scratch directory that archived fix tasks move into.
ARCHIVE_DIR = "archive"
#: The file names of a task and its unit record when the conventions file cannot be read
#: (`tasks.file`, `tasks.start.unit_file`).
TASK_FILE = "TASK.md"
UNIT_FILE = "unit.json"
#: Looks up the head commits of the merged pull requests from a branch; None when GitHub cannot be asked
#: (`workspace_clean.MergedHeads`).
MergedHeads = Callable[[str], list[str] | None]


@dataclass(frozen=True)
class DeadFix:
    """A fix task whose parent's pull request merged."""

    task: str
    folder: Path
    #: The parent task and the branch its pull request came from.
    parent: str
    branch: str


def _parent_of(folder: Path, task_file: str) -> str:
    """The `parent` of a fix task's folder; empty when its task file is not a fix's or cannot be read."""
    try:
        text = (folder / task_file).read_text(encoding="utf-8").replace("\r\n", "\n")
        front = yaml.safe_load(text[4:].split("\n---\n", 1)[0]) if text.startswith("---\n") else None
    except (OSError, UnicodeDecodeError, yaml.YAMLError):
        return ""
    if not isinstance(front, dict) or front.get("kind") != "fix":
        return ""
    return str(front.get("parent") or "")


def _branch_of(scratch: Path, parent: str, unit_file: str) -> str:
    """The branch of the parent's unit (its record), else the parent's id, which is the branch's name by default."""
    try:
        return read_record(scratch / parent / unit_file).branch
    except UnitRecordError:
        return parent


def dead_fixes(
    scratch: Path, merged_heads: MergedHeads, task_file: str = TASK_FILE, unit_file: str = UNIT_FILE
) -> list[DeadFix]:
    """The fix task folders of `scratch` whose parent's branch has a merged pull request, by name.

    A folder with no readable task file, one that is not a fix's, and a fix whose branch has no merged pull request (or
    for which GitHub could not say) are left out."""
    found: list[DeadFix] = []
    if not scratch.is_dir():
        return found
    for folder in sorted(scratch.iterdir()):
        if not folder.is_dir() or folder.name == ARCHIVE_DIR:
            continue
        parent = _parent_of(folder, task_file)
        if not parent:
            continue
        branch = _branch_of(scratch, parent, unit_file)
        if merged_heads(branch):
            found.append(DeadFix(folder.name, folder, parent, branch))
    return found


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
