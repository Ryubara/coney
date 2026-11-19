# SPDX-License-Identifier: GPL-3.0-or-later
"""The runs a worktree's own scratch folder says are going on (docs/guides/workspace.md#cleanup).

`workspace clean` judges a build folder idle by its files' times, and a run that is working in it keeps no file busy for
hours: the compile has ended and the tests read the build, or a check is between its steps. The build queue's own claims
and activities name most of those runs (`build_claims.live_worktrees`), and this module is a second, independent
reading of the same fact from the records the runs write about themselves, so one lost activity file (a queue folder
that could not be written, a run started before the activity existed) cannot cost a unit its build:

* `check-progress.json` of `task check`, in the state `waiting` or `running` with a process that lives
  (`check_progress.read_view`);
* every `*-result.json` of a detached run (`build-result.json`, `check-result.json`, `fails-first-result.json`, the
  task check's), in the state `running` with a process that started when the record says (`build_cli._alive`).

A scratch folder is named for its worktree (`<scratch>/<worktree>/`), except a fix task's, which works in its parent's
worktree and says so in its unit record; both name the worktree they protect. A reading is made each time it is asked
for and never kept: a listing that had gone stale once deleted the builds of two running builds.
"""

from __future__ import annotations

import contextlib
import json
from collections.abc import Callable
from datetime import UTC, datetime
from pathlib import Path

from coney_tools import check_progress, pcsx2_proc
from coney_tools.unit_record import UnitRecordError, read_record

#: The suffix of a detached run's result file in a unit's scratch folder.
RESULT_SUFFIX = "-result.json"
#: A result file not written for this long (a day, as `build_claims.ACTIVITY_MAX_AGE`) names no run: a process id can
#: be reused by an unrelated program, and a run that long is a hung one.
MAX_AGE_SECONDS = 24 * 3600.0
#: `read_view`'s stall limit, which a reader of the state alone does not use.
NO_STALL_MINUTES = 10**6


def _worktrees_of(folder: Path) -> set[str]:
    """The worktree names a scratch folder protects: its own name, and the worktree its unit record names (a fix task's
    folder holds its parent's)."""
    names = {folder.name}
    with contextlib.suppress(UnitRecordError, OSError):
        names.add(read_record(folder / "unit.json").worktree.name)
    return names


def _result_running(path: Path, now: float) -> bool:
    """Whether the detached run the result file `path` records is still going."""
    from coney_tools import build_cli  # imported here: build_cli reaches workspace_clean through build_cap

    try:
        age = now - path.stat().st_mtime
    except OSError:
        return False
    if age > MAX_AGE_SECONDS:
        return False
    record = build_cli._read_record(path)
    return record is not None and record.get("state") == "running" and build_cli._alive(record)


def runs_in(folder: Path, now: float, alive: Callable[[int], bool] | None = None) -> list[str]:
    """What runs for the unit whose scratch folder is `folder`: `task check` (a live progress file) and each detached
    run (`build`, `check`, `orig fails-first`: the result file's name before `-result.json`); empty when none does."""
    found: list[str] = []
    view = check_progress.read_view(
        folder, datetime.fromtimestamp(now, UTC), NO_STALL_MINUTES, alive or pcsx2_proc.pid_alive
    )
    if view is not None and view["state"] in check_progress.LIVE_STATES:
        found.append("task check")
    try:
        results = sorted(folder.glob(f"*{RESULT_SUFFIX}"))
    except OSError:
        return found
    for path in results:
        if _result_running(path, now):
            found.append(f"detached {path.name[: -len(RESULT_SUFFIX)]}")
    return found


def running(scratch: Path, now: float) -> dict[str, list[str]]:
    """The runs the scratch folders of `scratch` record as going on, by worktree name. A scratch folder that does not
    exist records none, and neither does a file that cannot be read."""
    found: dict[str, list[str]] = {}
    try:
        folders = sorted(path for path in scratch.iterdir() if path.is_dir())
    except OSError:
        return found
    for folder in folders:
        try:
            what = runs_in(folder, now)
        except (OSError, ValueError, json.JSONDecodeError):
            continue
        for name in _worktrees_of(folder) if what else ():
            found.setdefault(name, []).extend(what)
    return found
