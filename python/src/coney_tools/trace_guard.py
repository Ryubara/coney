# SPDX-License-Identifier: GPL-3.0-or-later
"""The standing-player guard of `trace coney` (#1680).

A level can open with a scene or script that holds the pad (level99 checkpoint 3's `l99_c3` scene holds it for about 750
updates, overriding `--start`). A scenario that records Coney through such an opening plays its stick into a player the
game is not listening to, and the compare then passes or fails on a standing player: it proves nothing about the
behaviour the scenario names. This check refuses that trace: when the input script holds the left stick past the dead
zone, the player's position must change somewhere from that step to the end of the trace.

The trace is Coney's `--trace` CSV (columns `step`, `x`, `y`, `z`, ...); the script is the scenario's, read as Coney
reads it (coney_tools.input_script). The way to fix a scenario the guard refuses is `wait_for_control = true` in its
`[coney]` table (docs/guides/research-traces.md#comparing-with-coney).
"""

from __future__ import annotations

import math

from coney_tools.input_script import InputEvent
from coney_tools.trace_diff import Trace

#: A left stick at or past this many percent on either axis is meant to move the player: well past the dead zone
#: (a deflection of 1 to 33 percent reads as rest), under the walk at 60 percent the scenarios use.
MOVING_STICK_PERCENT = 40

#: How far, in metres, the player must go from his first position of the stretch for him to have moved. A standing
#: player's position is exact to the 4 digits of the trace, so this is far above any rounding and far below a step.
MOVED_METRES = 0.05

#: The position columns of a trace.
POSITION = ("x", "y", "z")


def first_moving_step(events: list[InputEvent]) -> int | None:
    """The first step on which player 1's left stick is held past the dead zone, or None when no line does.

    Frame N of a script takes effect on step N + 1 (Coney's `--trace` numbering). Only port 0's left stick moves the
    player; a right stick turns the camera and a button presses nothing a walk needs.
    """
    for event in events:
        left_stick = event.action == "stick" and event.port == 0 and event.stick == 0
        if left_stick and max(abs(event.x), abs(event.y)) >= MOVING_STICK_PERCENT:
            return event.frame + 1
    return None


def standing_player(events: list[InputEvent], trace: Trace) -> str | None:
    """What is wrong with `trace` when the input script should have moved the player and did not, else None.

    The stretch judged runs from the step of the first stick past the dead zone to the trace's last step; the player
    has moved when any of x, y or z leaves its first value of the stretch by more than MOVED_METRES, so a fall or a
    slide counts as well as a walk. A script with no such stick has nothing to check.
    """
    start = first_moving_step(events)
    if start is None:
        return None
    if any(name not in trace.header for name in POSITION):
        return "the trace has no x, y and z columns, so a player who never moves cannot be told from one who does"
    steps = sorted(step for step in trace.rows if step >= start)
    if not steps:
        return f"the trace ends before the stick at step {start}: nothing was recorded for the input to move"
    first = trace.rows[steps[0]]
    farthest = 0.0
    for step in steps:
        row = trace.rows[step]
        if any(row[name] is None or first[name] is None for name in POSITION):
            continue
        farthest = max(
            farthest, math.dist([row[name] or 0.0 for name in POSITION], [first[name] or 0.0 for name in POSITION])
        )
        if farthest > MOVED_METRES:
            return None
    return (
        f"the player never moves: from the stick at step {start} to step {steps[-1]} he goes {farthest:.3f} m from "
        f"where he stood. A scene or script is probably holding the pad (level99 checkpoint 3's l99_c3 scene does, "
        f"for about 750 updates): give the scenario's [coney] table wait_for_control = true so the run starts when "
        f"the player has control (docs/guides/research-traces.md#comparing-with-coney)"
    )
