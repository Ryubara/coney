# SPDX-License-Identifier: GPL-3.0-or-later
"""The places of a machine for the slot-fill check (docs/guides/slot-fill.md): which are free, which a track keeps and
the work that may take one. Split from `slot_fill.py`, which decides from them."""

from __future__ import annotations

from dataclasses import dataclass

from coney_tools import task_slots
from coney_tools.conventions import Conventions


@dataclass(frozen=True)
class Free:
    """The free places of a machine: how many nothing holds, and which of those a track keeps."""

    #: Places nothing holds (the machine's limit less the units in work, never below 0).
    total: int
    #: The free places kept for the reimplementation track's game work, for a fix, for networking and for the
    #: renderer; Studio may not take them.
    game: int
    fix: int
    networking: int
    renderer: int
    #: The rest: places Coney Studio, or game work beyond the kept one, may take.
    open: int


def free_places(current: task_slots.Scan, conventions: Conventions, build_slots: int) -> Free:
    """Which places of the machine are free now, by the rule of `task_slots.room`: a track that runs fewer than its
    `min` units that fill it keeps the difference, and the rest are open."""
    tracks = conventions.tasks.start.slots.tracks
    total = max(task_slots.machine_limit(conventions, build_slots) - len(current.holders), 0)
    kept = {
        name: max(tracks.of(name).min - task_slots.filling_count(current.holders, name, conventions), 0)
        for name in ("reimplementation", "fix", "networking", "renderer")
    }
    game = min(kept["reimplementation"], total)
    fix = min(kept["fix"], total - game)
    networking = min(kept["networking"], total - game - fix)
    renderer = min(kept["renderer"], total - game - fix - networking)
    return Free(total, game, fix, networking, renderer, total - game - fix - networking - renderer)


def summary(current: task_slots.Scan, conventions: Conventions, build_slots: int) -> str:
    """The places in one line: units in work of the machine's limit, by track."""
    limit = task_slots.machine_limit(conventions, build_slots)
    parts = [
        f"{track}: {', '.join(h.task for h in current.held(track))}"
        for track in task_slots.TRACKS
        if current.held(track)
    ]
    return f"{len(current.holders)} of {limit} places in use" + (f", {'; '.join(parts)}" if parts else "")


@dataclass(frozen=True)
class Work:
    """One thing the coordinator could start now: the track it would work in (`task_slots.TRACKS`; Coney Studio work
    may not take a place another track keeps, and networking or renderer work only the one its own track keeps) and
    its task kind (only a kind in the reimplementation track's `min_kinds` fills the place kept for game work; a
    tooling or docs unit may take an open place only)."""

    text: str
    track: str = "reimplementation"
    kind: str = "feature"


def fills_game_place(kind: str, track: str, conventions: Conventions) -> bool:
    """Whether work of this task kind in this track may take the place kept for game work: the reimplementation
    track's own, of a kind that fills its minimum (`task_slots.room` refuses the others there)."""
    return track == "reimplementation" and conventions.tasks.start.slots.tracks.of("reimplementation").fills_min(kind)


def track_full(current: task_slots.Scan, conventions: Conventions, track: str, waiting: int) -> str:
    """Why work of a track with a cap (networking, the renderer) waits: the track holds at most `max` units
    (`tasks.start.slots.tracks.<track>`)."""
    maximum = conventions.tasks.start.slots.tracks.of(track).max
    running = ", ".join(h.task for h in current.held(track)) or "none"
    return (
        f"the {track} track is full ({running}; at most {maximum}), so {waiting} {track} issue(s) or task(s) "
        "wait until it passes Review"
    )
