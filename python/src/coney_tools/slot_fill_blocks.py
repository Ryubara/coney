# SPDX-License-Identifier: GPL-3.0-or-later
"""The Stop hook's block counter for the slot-fill check (docs/guides/slot-fill.md).

The hook blocks the end of a coordinator turn while a place can be filled. A check that cannot be satisfied (a place
that stays free for a reason nobody waived) must never trap the coordinator, so the blocks of one session are counted in
the scratch folder and the turn is let end after `tasks.start.fill.block.max_in_a_row` of them in a row.
"""

from __future__ import annotations

import json
from datetime import datetime, timedelta
from pathlib import Path

from coney_tools.conventions import Conventions
from coney_tools.issue_claim_model import iso_time, parse_time

#: The state of the counter in the scratch folder.
BLOCKS_FILE = "slot-fill-blocks.json"


def _read_blocks(path: Path) -> dict[str, dict[str, object]]:
    """The counter file: session id to `{count, last}`; an unreadable file is an empty counter."""
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, ValueError):
        return {}
    return (
        {k: v for k, v in data.items() if isinstance(k, str) and isinstance(v, dict)} if isinstance(data, dict) else {}
    )


def may_block(scratch: Path, conventions: Conventions, session: str, now: datetime) -> bool:
    """Whether the Stop hook may block this turn end of `session`, and count it.

    At most `tasks.start.fill.block.max_in_a_row` turn ends in a row for one session: a check that cannot be satisfied
    (a slot that stays free for a reason nobody waived) then never traps the coordinator. The count starts again after
    `reset_minutes` without a block, and a pass clears it (`clear_blocks`). False when it cannot be counted (a folder
    that cannot be written), so a broken counter never blocks."""
    fill = conventions.tasks.start.fill
    path = scratch / BLOCKS_FILE
    state = _read_blocks(path)
    entry = state.get(session, {})
    try:
        last = parse_time(str(entry["last"]))
        count = int(entry["count"])  # type: ignore[call-overload]
    except (KeyError, ValueError, TypeError):
        last, count = None, 0
    if last is not None and now - last > timedelta(minutes=fill.block_reset_minutes):
        count = 0
    if count >= fill.block_max:
        return False
    state[session] = {"count": count + 1, "last": iso_time(now)}
    cutoff = timedelta(minutes=fill.block_reset_minutes)
    state = {key: value for key, value in state.items() if _recent(value, now, cutoff)}
    try:
        scratch.mkdir(parents=True, exist_ok=True)
        path.write_text(json.dumps(state, indent=2) + "\n", encoding="utf-8", newline="\n")
    except OSError:
        return False
    return True


def _recent(entry: dict[str, object], now: datetime, span: timedelta) -> bool:
    """Whether a counter entry's last block is within `span` of `now`."""
    try:
        return now - parse_time(str(entry["last"])) <= span
    except (KeyError, ValueError):
        return False


def clear_blocks(scratch: Path, session: str) -> None:
    """Forget the blocks of `session` (the check passed): the next failure counts from one."""
    path = scratch / BLOCKS_FILE
    state = _read_blocks(path)
    if session not in state:
        return
    del state[session]
    try:
        path.write_text(json.dumps(state, indent=2) + "\n", encoding="utf-8", newline="\n")
    except OSError:
        return
