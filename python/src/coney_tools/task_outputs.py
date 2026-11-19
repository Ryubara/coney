# SPDX-License-Identifier: GPL-3.0-or-later
"""The background task outputs of Claude Code sessions, which `workspace clean` reports and removes when stale
(docs/guides/workspace.md#task-outputs).

Claude Code writes the output of every background command to `<temp>/claude/<hash>/<session>/tasks/<id>.output`
(`%TEMP%\\claude` on Windows) and never removes it: outputs of one build or test run grew to 5 GB each, 20 GB in one
session, and filled the system drive. A session is **live** when anything in its folder (an output or its scratchpad)
was written within `workspace.task_outputs.stale_hours`; only a session idle for longer is **stale**, and only a stale
session's outputs are removed, never a live one's (its running task is still writing, and the session may read an older
output back). A live session whose outputs hold more than `workspace.task_outputs.warn_gb` is reported, with the advice
to stop the task or delete the output that ended: the tool never judges which of a live session's outputs is spent.
The scratchpad of a session is never removed here. This module only reads and removes; the caller prints and decides.
"""

from __future__ import annotations

import os
import shutil
import tempfile
from dataclasses import dataclass
from pathlib import Path

#: The folder in a session's folder that holds the outputs of its background commands.
TASKS_FOLDER = "tasks"
#: How many sessions a report lists before it counts the rest.
LISTED = 15
GIB = 1024**3


@dataclass(frozen=True)
class Session:
    """One session folder `<root>/<hash>/<session>` that holds background task outputs."""

    path: Path
    #: The bytes and the number of files in its `tasks/` folder.
    bytes: int
    files: int
    #: The newest modification time of anything in the session folder (outputs and scratchpad).
    newest: float
    #: Whether nothing in the session was written within the stale limit.
    stale: bool

    @property
    def name(self) -> str:
        """The session as `<hash>/<session>`."""
        return f"{self.path.parent.name}/{self.path.name}"


def default_root() -> Path:
    """Where Claude Code keeps session folders: `claude` in the machine's temp folder (`%TEMP%` on Windows)."""
    return Path(tempfile.gettempdir()) / "claude"


def _walk(folder: Path) -> tuple[int, int, float]:
    """The bytes, the files and the newest modification time (the folders' own included) under `folder`."""
    size = files = 0
    newest = 0.0
    stack = [folder]
    while stack:
        try:
            entries = list(os.scandir(stack.pop()))
        except OSError:
            continue
        for entry in entries:
            try:
                info = entry.stat(follow_symlinks=False)
            except OSError:
                continue
            newest = max(newest, info.st_mtime)
            if entry.is_dir(follow_symlinks=False):
                stack.append(Path(entry.path))
            else:
                size += info.st_size
                files += 1
    return size, files, newest


def _newest(session: Path) -> float:
    """The newest modification time of anything in the session folder, the folder included."""
    try:
        own = session.stat().st_mtime
    except OSError:
        return 0.0
    return max(own, _walk(session)[2])


def survey(root: Path, now: float, stale_hours: float) -> list[Session]:
    """Every session under `root` that has a `tasks/` folder, biggest outputs first. A root that does not exist, a
    folder that cannot be read and a session without outputs name nothing."""
    found: list[Session] = []
    try:
        hashes = sorted(path for path in root.iterdir() if path.is_dir())
    except OSError:
        return found
    for group in hashes:
        try:
            sessions = sorted(path for path in group.iterdir() if (path / TASKS_FOLDER).is_dir())
        except OSError:
            continue
        for path in sessions:
            size, files, _ = _walk(path / TASKS_FOLDER)
            newest = _newest(path)
            found.append(Session(path, size, files, newest, stale=now - newest > stale_hours * 3600.0))
    return sorted(found, key=lambda session: session.bytes, reverse=True)


def remove_stale(sessions: list[Session], now: float, stale_hours: float) -> tuple[int, list[str]]:
    """Remove the contents of the `tasks/` folder of each stale session, never a live one's: each is judged again just
    before it is removed, because a session that wrote since the survey is live. Returns the bytes freed and what
    could not be removed."""
    freed = 0
    failures: list[str] = []
    for session in sessions:
        if not session.stale or now - _newest(session.path) <= stale_hours * 3600.0:
            continue
        tasks = session.path / TASKS_FOLDER
        before = _walk(tasks)[0]
        try:
            children = list(tasks.iterdir())
        except OSError as error:
            failures.append(f"{session.name}: {error}")
            continue
        for child in children:
            try:
                if child.is_dir() and not child.is_symlink():
                    shutil.rmtree(child)
                else:
                    child.unlink()
            except OSError as error:
                failures.append(f"{child}: {error}")
        freed += max(0, before - _walk(tasks)[0])
    return freed, failures


def _size(size: float) -> str:
    """Bytes as megabytes or gigabytes."""
    return f"{size / GIB:.1f} GB" if size >= GIB else f"{size / 1024**2:.1f} MB"


def lines(sessions: list[Session], apply: bool, warn_bytes: int) -> list[str]:
    """The report of `workspace clean`: the biggest sessions with what is done to each, and the totals. Empty when no
    session holds an output."""
    held = [session for session in sessions if session.bytes > 0]
    if not held:
        return []
    out: list[str] = []
    for session in held[:LISTED]:
        what = f"{_size(session.bytes)} in {session.files} output(s)"
        if session.stale:
            out.append(f"  {'remove' if apply else 'would remove':<13}{session.name}: {what}, idle: stale")
        elif session.bytes > warn_bytes:
            out.append(
                f"  {'keep':<13}{session.name}: {what}, live, kept; over {_size(warn_bytes)}: stop the task or delete "
                "the output of one that ended"
            )
        else:
            out.append(f"  {'keep':<13}{session.name}: {what}, live, kept")
    if len(held) > LISTED:
        out.append(f"  ... and {len(held) - LISTED} more session(s) with outputs")
    stale = [session for session in held if session.stale]
    out.append(
        f"{len(stale)} stale session(s) hold {_size(sum(s.bytes for s in stale))} of background task output; "
        f"{len(held) - len(stale)} live one(s) hold {_size(sum(s.bytes for s in held if not s.stale))}"
    )
    return out


def summary(sessions: list[Session], warn_bytes: int) -> str | None:
    """One line for `workspace check-space`: what the stale outputs hold and that `workspace clean --apply` removes
    them, and any live session over the warning size; None when there is nothing to say."""
    stale = [session for session in sessions if session.stale and session.bytes > 0]
    big = [session for session in sessions if not session.stale and session.bytes > warn_bytes]
    parts: list[str] = []
    if stale:
        parts.append(
            f"{_size(sum(s.bytes for s in stale))} of stale background task output in {len(stale)} session(s): "
            "`workspace clean --apply` removes it"
        )
    if big:
        parts.append(
            f"live session(s) over {_size(warn_bytes)}: "
            + ", ".join(f"{s.name} {_size(s.bytes)}" for s in big)
            + " (stop the task or delete its output once it ended)"
        )
    return "; ".join(parts) if parts else None
