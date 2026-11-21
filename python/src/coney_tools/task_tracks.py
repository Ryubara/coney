# SPDX-License-Identifier: GPL-3.0-or-later
"""The track rules of the engineer slots: which track a unit counts in, whether one more unit of a track fits on the
machine, and which model a Studio unit may start on (docs/guides/agents.md#slots). `task_slots` holds the table of what
is in work, the commands and the admission flow, and applies these rules to it.

A unit works in one of five tracks (maintainer decisions 2026-10-28 and 2026-10-29, `tasks.start.slots.tracks`): `fix`
for a `kind: fix` task, `networking` for a task whose issue carries `claims.networking_label`, `renderer` for one whose
issue carries `claims.renderer_label` (the SDL3 GPU renderer), `studio` for one whose issue carries
`claims.studio_label` (Coney Studio), `reimplementation` for every other (`track_of`). The labels are read from the
issue's held claim card on GitHub (`unit_labels`), never from a file here, so nobody changes a track by editing the
scratch folder.

A machine holds at most `build_slots` (coney.local.toml, the build slots `coney-tools build` keeps) times
`tasks.start.slots.per_build_slot` units of all tracks together (`machine_limit`); a track may have its own `max`, and
the `min` places of a track that runs fewer are kept from the others, so Studio never takes the last place a
reimplementation unit or a fix needs (a machine too small to keep every place sets aside all but the starting unit's):
`room`. A Studio unit must also start on one of the model and effort pairs of `tasks.start.slots.tracks.studio.models`
(`model_problem`).

A unit whose issue carries `claims.exempt_label` now (the units that build the new workflow) holds no place while
`tasks.start.slots.exempt_workflow` is on, even when the issue also carries the Studio, networking or renderer label;
the cutover turns it off. A unit whose cards cannot be read (and none that was read says workflow, Studio, networking
or renderer) is counted where it was before those labels existed: a fix as a fix, any other as reimplementation, so a
GitHub that cannot be read costs no start; its Studio model is then not checked.
"""

from __future__ import annotations

from collections.abc import Collection, Iterable
from dataclasses import dataclass
from typing import TYPE_CHECKING, Protocol

from coney_tools.config import ConfigError
from coney_tools.conventions import Conventions
from coney_tools.issue_claim_model import Card

if TYPE_CHECKING:
    from coney_tools.task_slots import Holder, Scan


class CardSource(Protocol):
    """Where the held claim cards of issues come from (`task_slots.Cards`)."""

    def held(self, number: int) -> Card | None:
        """The held card of issue `number`, or None; ConfigError when it cannot be read."""
        ...


#: The tracks, in the order reports list them (`tasks.start.slots.tracks` has one entry for each).
TRACKS = ("reimplementation", "fix", "studio", "networking", "renderer")


@dataclass(frozen=True)
class Labels:
    """What the held claim cards of a unit's issues say about it."""

    #: One of them carries `claims.exempt_label` (a workflow unit), with or without the Studio label: the workflow
    #: exemption comes first until the cutover.
    workflow: bool
    #: One of them carries `claims.studio_label`.
    studio: bool
    #: One of them carries `claims.networking_label`.
    networking: bool = False
    #: One of them carries `claims.renderer_label`.
    renderer: bool = False


def track_of(kind: str, studio: bool, networking: bool = False, renderer: bool = False) -> str:
    """The track a task works in: `fix` for a fix task, else `networking` for an issue labelled for networking, else
    `renderer` for one labelled for the renderer (both before Studio, for an issue with several), else `studio` for an
    issue labelled Studio, else `reimplementation`."""
    if kind == "fix":
        return "fix"
    if networking:
        return "networking"
    if renderer:
        return "renderer"
    return "studio" if studio else "reimplementation"


def machine_limit(conventions: Conventions, build_slots: int) -> int:
    """How many units of all tracks one machine holds: its build slots times `tasks.start.slots.per_build_slot`."""
    return max(build_slots, 1) * conventions.tasks.start.slots.per_build_slot


def unit_labels(issues: Iterable[int], cards: CardSource, conventions: Conventions) -> Labels | None:
    """The labels of a unit on these issues, from their held claim cards (`studio`, `networking`, `renderer`, and
    `in_workflow` for the workflow label; an issue with more is a workflow unit, which the callers check first).
    `workflow` is false while `tasks.start.slots.exempt_workflow` is off, so the unit counts like any other. A
    workflow, Studio, networking or renderer card that was read decides even when another card cannot be; otherwise
    None when a card cannot be read: the caller counts the unit, a fix as a fix and any other as reimplementation."""
    found: list[Card] = []
    unreadable = False
    for number in issues:
        try:
            card = cards.held(number)
        except ConfigError:
            unreadable = True
            continue
        if card is not None:
            found.append(card)
    studio = any(card.studio for card in found)
    networking = any(card.networking for card in found)
    renderer = any(card.renderer for card in found)
    workflow = conventions.tasks.start.slots.exempt_workflow and any(card.in_workflow for card in found)
    if unreadable and not workflow and not studio and not networking and not renderer:
        return None
    return Labels(workflow=workflow, studio=studio, networking=networking, renderer=renderer)


@dataclass(frozen=True)
class Room:
    """Whether one more unit of a track fits on the machine, and the numbers behind the answer."""

    track: str
    #: Units of the track in work, and its own cap (None: only the machine's).
    count: int
    maximum: int | None
    #: Units of every track in work, and the machine limit.
    total: int
    machine: int
    #: The places kept for tracks that run fewer than their `min` of filling units, the asking track's own too when
    #: the unit asking would not fill it (a tooling unit in reimplementation): track to places.
    kept: dict[str, int]

    @property
    def track_full(self) -> bool:
        """The track's own cap is reached."""
        return self.maximum is not None and self.count + 1 > self.maximum

    @property
    def machine_full(self) -> bool:
        """The machine has no place left once the kept places are set aside. A machine smaller than the places the
        tracks keep sets aside at most all but the starting unit's place, so it still starts a unit while it is empty
        rather than refusing every start; on such a machine any track's unit, Studio's too, may take that place."""
        return self.total + 1 + min(sum(self.kept.values()), self.machine - 1) > self.machine

    @property
    def fits(self) -> bool:
        """One more unit of the track may start."""
        return not (self.track_full or self.machine_full)

    def note(self, task: str) -> str:
        """The line of a start that is let through (or would be, unenforced)."""
        text = f"{self.total + 1} of {self.machine} places on this machine in use with {task} ({self.track} track"
        text += f", {self.count + 1} of {self.maximum})" if self.maximum is not None else ")"
        return text


def filling_count(holders: Iterable[Holder], track: str, conventions: Conventions) -> int:
    """How many of the holders fill the `min` of `track`: its units of a kind that counts toward it."""
    rule = conventions.tasks.start.slots.tracks.of(track)
    return sum(1 for holder in holders if holder.track == track and rule.fills_min(holder.kind))


def room(
    current: Scan,
    track: str,
    task: str,
    conventions: Conventions,
    build_slots: int,
    kind: str = "",
    lapsed: Collection[str] = (),
) -> Room:
    """Whether a unit of `track` (the task `task` of this `kind`, which is left out of the count) fits beside what
    `current` holds.

    It fits while its track is under its own `max` and the machine (`machine_limit`) has a place once the `min`
    places of every track that runs fewer than its `min` are set aside (`tasks.start.slots.tracks`), except the
    tracks in `lapsed` (`read_lapses`: the fill check found nothing of theirs to start), whose places are open. A
    track's own place is set aside from a unit that would not fill it (a tooling unit in the reimplementation track),
    so such units cannot take the last place game work keeps."""
    tracks = conventions.tasks.start.slots.tracks
    others = [holder for holder in current.holders if holder.task != task]
    counts = {name: sum(1 for holder in others if holder.track == name) for name in TRACKS}
    filling = {name: filling_count(others, name, conventions) for name in TRACKS}
    kept = {
        name: tracks.of(name).min - filling[name]
        for name in TRACKS
        if filling[name] < tracks.of(name).min
        and name not in lapsed
        and (name != track or not tracks.of(name).fills_min(kind))
    }
    return Room(
        track=track,
        count=counts[track],
        maximum=tracks.of(track).max,
        total=len(others),
        machine=machine_limit(conventions, build_slots),
        kept=kept,
    )


def model_problem(model: str, effort: str, conventions: Conventions) -> str | None:
    """Why a Studio unit may not start on this model and effort, or None: the pair (any letter case) must be one of
    `tasks.start.slots.tracks.studio.models`. An empty list allows any."""
    allowed = conventions.tasks.start.slots.tracks.studio.models
    if not allowed:
        return None
    pair = (model.strip().casefold(), effort.strip().casefold())
    if any(pair == (entry.model, entry.effort) for entry in allowed):
        return None
    named = f"model '{model or 'unset'}' at effort '{effort or 'unset'}'"
    choices = "; ".join(f"{entry.model} at {entry.effort} ({entry.use})" for entry in allowed)
    return (
        f"a Studio unit starts on one of these, named in the Agent call (`model` and `effort`): {choices}, not {named} "
        "(tasks.start.slots.tracks.studio.models, docs/guides/agents.md#models)"
    )
