# SPDX-License-Identifier: GPL-3.0-or-later
"""How long each kind of build step took the last few times (docs/guides/coney-tools-build.md#build).

A run that waits for a build slot must not wait past its caller's time budget, and whether it would still fit depends on
how long the step it queues for takes. This keeps the durations of finished steps, per preset, step and size, in one
small JSON file in the claims folder that every worktree sees, and answers with the median of the recent ones. A lost
update (two runs finishing in the same instant) or a damaged file costs an estimate, never a run, so nothing here
raises.
"""

from __future__ import annotations

import contextlib
import json
import os
import statistics
import uuid
from pathlib import Path

#: How many recent durations are kept per key.
KEEP = 10
#: The file in the claims folder.
FILE_NAME = "durations.json"


def step_key(preset: str, step: str, size: str) -> str:
    """The key of one kind of step: `ci/build/fast`, `dev/test/filtered`. `size` is how big the job was (a build's lane,
    a test run with or without a `-R` or `-L` filter)."""
    return f"{preset}/{step}/{size}"


class History:
    """The durations file at `path`."""

    def __init__(self, path: Path) -> None:
        self.path = path

    def _load(self) -> dict[str, list[float]]:
        """Every key's usable durations; an unreadable or odd file reads as empty."""
        try:
            data = json.loads(self.path.read_text(encoding="utf-8"))
        except (OSError, ValueError):
            return {}
        if not isinstance(data, dict):
            return {}
        found = {}
        for key, values in data.items():
            if isinstance(key, str) and isinstance(values, list):
                found[key] = [
                    float(v) for v in values if isinstance(v, (int, float)) and not isinstance(v, bool) and v >= 0
                ]
        return found

    def expected(self, key: str) -> tuple[float, int] | None:
        """The median of the recent durations of `key` in seconds and how many there were; None without any."""
        values = self._load().get(key) or []
        return (statistics.median(values), len(values)) if values else None

    def record(self, key: str, seconds: float) -> None:
        """Add a finished step's duration, keeping the last KEEP. The file is replaced whole (a temporary file renamed
        over it), so a reader never sees half of one."""
        data = self._load()
        data[key] = [*data.get(key, []), round(seconds, 1)][-KEEP:]
        temporary = self.path.with_name(f".{self.path.name}.{os.getpid()}.{uuid.uuid4().hex[:6]}")
        try:
            self.path.parent.mkdir(parents=True, exist_ok=True)
            temporary.write_text(json.dumps(data, indent=1), encoding="utf-8")
            temporary.replace(self.path)
        except OSError:
            # POSIX says NotADirectoryError, not FileNotFoundError, when the folder is a file.
            with contextlib.suppress(OSError):
                temporary.unlink()
