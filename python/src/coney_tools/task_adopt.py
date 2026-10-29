# SPDX-License-Identifier: GPL-3.0-or-later
"""The git and folder work of `task start --adopt` and `--adopt-branch` (docs/guides/issues.md#adopting).

A branch that was released before `pr open` existed has a pull request but no unit: no unit record and a stamp log that
does not start with `assigned` and `claimed`, and a stamp log cannot be prefixed. Adopting it makes the task's unit on
that branch instead of a new one and starts the task's stamp log afresh, moving the old log aside (never deleting it).
These helpers only look at git and move a folder; `task_start` decides what to refuse and stamps the start.
"""

from __future__ import annotations

import json
import subprocess
from datetime import datetime
from pathlib import Path

from coney_tools.config import ConfigError
from coney_tools.unit_record import UnitRecordError, read_record

#: The prefix of the folder an adopted task's old stamp log is moved to; the time of the move follows.
ARCHIVE_PREFIX = "stamps.legacy-"


def _git(main: Path, *args: str) -> subprocess.CompletedProcess[str]:
    """Run git in `main`; the caller looks at the exit code."""
    return subprocess.run(["git", "-C", str(main), *args], capture_output=True, encoding="utf-8", check=False)


def has_local_branch(main: Path, branch: str) -> bool:
    """Whether `branch` exists as a local branch."""
    return _git(main, "show-ref", "--verify", "--quiet", f"refs/heads/{branch}").returncode == 0


def make_local_branch(main: Path, branch: str, remote: str) -> bool:
    """Make the local `branch` from `<remote>/<branch>` (fetching it first); False when the remote has none."""
    if _git(main, "fetch", "--quiet", remote, branch).returncode != 0:
        return False
    return _git(main, "branch", "--no-track", branch, f"{remote}/{branch}").returncode == 0


def worktree_of(main: Path, branch: str) -> Path | None:
    """The worktree that has `branch` checked out (the main checkout included), or None."""
    done = _git(main, "worktree", "list", "--porcelain")
    if done.returncode != 0:
        raise ConfigError(f"git worktree list failed: {done.stderr.strip() or 'no output'}")
    path: Path | None = None
    for line in done.stdout.splitlines():
        if line.startswith("worktree "):
            path = Path(line[len("worktree ") :])
        elif line == f"branch refs/heads/{branch}" and path is not None:
            return path
    return None


def merge_base(main: Path, branch: str, base: str) -> str | None:
    """The commit the branch left `base` at, or None when they share none."""
    done = _git(main, "merge-base", branch, base)
    return done.stdout.strip() if done.returncode == 0 and done.stdout.strip() else None


def unit_on_branch(scratch: Path, unit_file: str, branch: str) -> str | None:
    """The task whose unit record already names `branch`, or None (a record that cannot be read names nothing)."""
    if not scratch.is_dir():
        return None
    for folder in sorted(scratch.iterdir()):
        path = folder / unit_file
        if not path.is_file():
            continue
        try:
            record = read_record(path)
        except UnitRecordError:
            continue
        if record.branch == branch:
            return record.task
    return None


def archive_name(now: datetime) -> str:
    """The folder name an old stamp log is moved to."""
    return f"{ARCHIVE_PREFIX}{now.strftime('%Y%m%dT%H%M%SZ')}"


def archive_log(folder: Path, now: datetime) -> Path:
    """Move the stamp log `folder` beside itself under `archive_name`; the new log then starts empty. Returns the new
    place. The old stamps stay readable there, and no check reads that folder."""
    target = folder.with_name(archive_name(now))
    if target.exists():
        raise ConfigError(f"{target} exists already; the old stamp log was moved aside a moment ago")
    folder.rename(target)
    return target


def first_step(folder: Path) -> str | None:
    """The `step` of the first stamp of the log in `folder`, read from its first file without verifying the chain; None
    when that cannot be read. Adopting must not reset a workflow log just because it is damaged, so a log whose first
    stamp is `assigned` (or cannot be told) is never moved aside."""
    files = sorted(folder.glob("*.jsonl"))
    if not files:
        return None
    try:
        with files[0].open(encoding="utf-8") as handle:
            first = json.loads(handle.readline())
    except (OSError, ValueError):
        return None
    step = first.get("step") if isinstance(first, dict) else None
    return step if isinstance(step, str) else None
