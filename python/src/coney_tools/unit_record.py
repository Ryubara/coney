# SPDX-License-Identifier: GPL-3.0-or-later
"""The unit record: what `task start` made for a task, kept beside its task file (docs/guides/issues.md#starting).

`<scratch>/<task>/unit.json` says where the task's work is: the branch, the worktree and the commit it started from,
and which task's stamp log it writes to. `task check`, `task review`, `pr open` and the mod read it, so none of them
guesses a worktree from a branch name. A file exists only for a task that was started; it holds names, paths and
times, never game data.

A fix task has a record of its own that copies its parent's branch and worktree and names the parent: the fix works in
the parent's unit and writes to the parent's stamp log (`UnitRecord.log_task`).
"""

from __future__ import annotations

import json
from dataclasses import dataclass
from datetime import datetime
from pathlib import Path
from typing import Any

import yaml

from coney_tools.config import ConfigError
from coney_tools.issue_claim_model import iso_time, parse_time

#: The format version of the file.
VERSION = 1


class UnitRecordError(ConfigError):
    """A unit record that cannot be read."""


@dataclass(frozen=True)
class UnitRecord:
    """Where a started task's work is."""

    task: str
    #: The task kind (`tasks.kinds`).
    kind: str
    issues: tuple[int, ...]
    claimant: str
    #: The branch of the unit (the task id for an ordinary task, the parent's for a fix task).
    branch: str
    #: The unit's worktree, an absolute path.
    worktree: Path
    #: What the branch started from, and the commit that was.
    base_ref: str
    base_commit: str
    started: datetime
    #: For a fix task, the task whose unit it works in; empty otherwise.
    parent: str = ""
    #: Whether `/progress claimed` reached the issues (a rerun of `task start` posts it again when not).
    progress_posted: bool = False

    @property
    def log_task(self) -> str:
        """The task whose stamp log this unit writes to: the parent for a fix task, else itself."""
        return self.parent or self.task


def _data(record: UnitRecord) -> dict[str, Any]:
    """The record as the JSON object that is written."""
    return {
        "v": VERSION,
        "task": record.task,
        "kind": record.kind,
        "issues": list(record.issues),
        "claimant": record.claimant,
        "branch": record.branch,
        "worktree": record.worktree.as_posix(),
        "base_ref": record.base_ref,
        "base_commit": record.base_commit,
        "started": iso_time(record.started),
        "parent": record.parent,
        "progress_posted": record.progress_posted,
    }


def write_record(path: Path, record: UnitRecord) -> None:
    """Write `record` to `path`, replacing the file whole (a temporary file renamed over it), LF endings."""
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(path.name + ".tmp")
    temporary.write_text(json.dumps(_data(record), indent=2) + "\n", encoding="utf-8", newline="\n")
    temporary.replace(path)


def front_of(path: Path) -> dict[str, Any] | None:
    """The --- block of the task file at `path` as a mapping, or None when the file is missing, is not a task file or
    holds invalid YAML. The one parse of that block that the tools which read a task folder without its record share."""
    try:
        text = path.read_text(encoding="utf-8").replace("\r\n", "\n")
    except (OSError, UnicodeDecodeError):
        return None
    if not text.startswith("---\n") or "\n---\n" not in text:
        return None
    try:
        front = yaml.safe_load(text[4:].split("\n---\n", 1)[0])
    except yaml.YAMLError:
        return None
    return front if isinstance(front, dict) else None


def front_parent(path: Path) -> str:
    """The `parent` in the --- block of the task file at `path`, "" when there is none or the file cannot be read.

    A fix task made without `task start` (a hand-made folder, an older fix) has a task file but no record; the parent is
    all that file says about where its work and its proof are."""
    front = front_of(path)
    return str(front.get("parent") or "") if front is not None else ""


def log_task_of(task_dir: Path, unit_file: str, task_file: str = "") -> str | None:
    """The task whose stamp log and scratch folder the task in `task_dir` works in (`UnitRecord.log_task`: the parent
    for a fix task), or None when the task has no record and, given `task_file`, no parent either. A task with no
    record but a task file that names a parent is a fix made without `task start`: it stamps in the parent's log, the
    one `pr open` reads. A record that cannot be read is a UnitRecordError: stamping a guessed log would put the proof
    where nothing reads it."""
    path = task_dir / unit_file
    if path.is_file():
        return read_record(path).log_task
    return (front_parent(task_dir / task_file) or None) if task_file else None


def read_record(path: Path) -> UnitRecord:
    """The record in `path`; UnitRecordError names the file and the first problem."""

    def fail(why: str) -> UnitRecordError:
        return UnitRecordError(f"{path}: {why}")

    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except OSError as error:
        raise fail(f"cannot be read ({error})") from error
    except ValueError as error:
        raise fail(f"not valid JSON ({error})") from error
    if not isinstance(data, dict):
        raise fail("not a JSON object")
    if data.get("v") != VERSION:
        raise fail(f"version {data.get('v')!r}, this tool reads {VERSION}")
    for key in ("task", "kind", "issues", "claimant", "branch", "worktree", "base_ref", "base_commit", "started"):
        if key not in data:
            raise fail(f"has no '{key}'")
    try:
        return UnitRecord(
            task=str(data["task"]),
            kind=str(data["kind"]),
            issues=tuple(int(item) for item in data["issues"]),
            claimant=str(data["claimant"]),
            branch=str(data["branch"]),
            worktree=Path(str(data["worktree"])),
            base_ref=str(data["base_ref"]),
            base_commit=str(data["base_commit"]),
            started=parse_time(str(data["started"])),
            parent=str(data.get("parent") or ""),
            progress_posted=bool(data.get("progress_posted", False)),
        )
    except (ValueError, TypeError) as error:
        raise fail(f"holds a wrong value ({error})") from error
