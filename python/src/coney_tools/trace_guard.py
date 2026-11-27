# SPDX-License-Identifier: GPL-3.0-or-later
"""Stub of the standing-player guard of `trace coney` (#1680): it finds nothing yet."""

from __future__ import annotations

from coney_tools.input_script import InputEvent
from coney_tools.trace_diff import Trace


def first_moving_step(events: list[InputEvent]) -> int | None:
    """Stub: no stick moves the player yet."""
    return None


def standing_player(events: list[InputEvent], trace: Trace) -> str | None:
    """Stub: no trace is refused yet."""
    return None
