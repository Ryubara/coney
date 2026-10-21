# SPDX-License-Identifier: GPL-3.0-or-later
"""The folder mechanics every claim in Coney shares: the PCSX2 copies (pcsx2_claims.py) and the build slots
(build_claims.py).

A claim is a folder holding one owner file, built complete in a private folder and renamed into place, which
succeeds for exactly one caller and never shows a half-made claim. A stale claim is removed under a takeover lock
(a folder that `mkdir` creates for one racer only), and looked at again under it, so a fresh claim a racer made
meanwhile is never moved or deleted. Nothing here knows what a claim means; the callers decide who owns what and
when a claim is stale.
"""

from __future__ import annotations

import errno
import shutil
import time
import uuid
from collections.abc import Callable
from pathlib import Path

#: The file inside a claim folder that says who holds it.
OWNER_FILE = "owner.json"

#: How long a takeover lock may stand before it counts as abandoned.
TAKEOVER_LOCK_SECONDS = 30.0

#: How often a takeover looks again after losing a step to a racer.
TAKEOVER_ATTEMPTS = 200


def publish(folder: Path, owner_text: str) -> bool:
    """Create the claim folder complete and atomically: the owner file is written in a private folder that is then
    renamed to the claim's name. A claim is thus never visible half-made (an owner-less folder reads as an abandoned
    one, which a racer may take over; that once let two agents win). The rename cannot replace an existing claim,
    which always holds an owner file. False when the name is already claimed. The parent folder must exist."""
    staging = folder.with_name(f".{folder.name}.new-{uuid.uuid4().hex[:8]}")
    staging.mkdir()
    try:
        (staging / OWNER_FILE).write_text(owner_text, encoding="utf-8")
        # Linux and macOS rename over an empty folder, so an owner-less claim (left by a crash) would be replaced
        # without the takeover checks. publish never makes an empty claim folder, so this check races with nothing.
        if folder.exists():
            return False
        staging.rename(folder)
        return True
    except OSError as error:
        # The name is taken (Windows says FileExistsError or PermissionError, Linux and macOS ENOTEMPTY since a
        # claim always holds its owner file), or is being moved away for a takeover; the caller looks again.
        if isinstance(error, (FileExistsError, PermissionError)) or error.errno == errno.ENOTEMPTY:
            return False
        raise
    finally:
        shutil.rmtree(staging, ignore_errors=True)


def break_old_lock(lock: Path) -> None:
    """Remove a takeover lock its owner abandoned (a crash between making and removing it, seconds apart)."""
    try:
        if time.time() - lock.stat().st_mtime > TAKEOVER_LOCK_SECONDS:
            shutil.rmtree(lock, ignore_errors=True)
    except OSError:
        pass


def remove(folder: Path) -> None:
    """Remove a claim folder: renamed away first, so it vanishes from its name in one step."""
    grave = folder.with_name(f"{folder.name}.gone-{uuid.uuid4().hex[:8]}")
    try:
        folder.rename(grave)
    except FileNotFoundError:
        return
    shutil.rmtree(grave, ignore_errors=True)


def remove_under_lock(folder: Path, unchanged: Callable[[], bool]) -> str:
    """Remove a claim folder while holding its takeover lock, if `unchanged()` (called under the lock) says it still
    holds the claim the caller decided to remove. Returns "removed", "changed" (the claim is not the one the caller
    saw: someone else won, leave it) or "busy" (the lock is held, or Windows refused the move while someone reads the
    folder: wait a moment and look again)."""
    lock = folder.with_name(f".{folder.name}.takeover")
    try:
        lock.mkdir()
    except OSError:
        # Another racer takes over (or Windows is still deleting its lock): wait, then look again, so a holder that
        # fails does not leave every racer empty-handed.
        break_old_lock(lock)
        time.sleep(0.01)
        return "busy"
    try:
        if not unchanged():
            return "changed"
        grave = folder.with_name(f"{folder.name}.stale-{uuid.uuid4().hex[:8]}")
        folder.rename(grave)
        shutil.rmtree(grave, ignore_errors=True)
        return "removed"
    except OSError:
        time.sleep(0.01)  # Windows refuses to move a folder someone is reading; try again
        return "busy"
    finally:
        shutil.rmtree(lock, ignore_errors=True)
