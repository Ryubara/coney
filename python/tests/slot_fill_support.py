# SPDX-License-Identifier: GPL-3.0-or-later
"""Shared by the slot-fill tests: a stand-in for GitHub (`slot_fill.Sources`) and a scene with units and claims.

Nothing here touches the network, git or the real scratch folder (docs/guides/slot-fill.md).
"""

from __future__ import annotations

from dataclasses import replace
from datetime import datetime
from pathlib import Path

from audit_support import CONVENTIONS
from claim_support import START
from test_task_slots import USER, Scene

from coney_tools import slot_fill
from coney_tools.config import ConfigError
from coney_tools.conventions import Conventions
from coney_tools.issue_claim_model import Card
from coney_tools.pr_hold import Hold
from coney_tools.slot_fill import Check, FailedPull, IssueInfo

RULES = CONVENTIONS.claims
FILL = CONVENTIONS.tasks.start.fill
WORKFLOW = RULES.exempt_label
NETWORKING = RULES.networking_label or "networking"


def issue(number: int, *labels: str, title: str = "", body: str = "", assignees: tuple[str, ...] = ()) -> IssueInfo:
    """A ready issue (`S-ready-to-start` and a task label) with the given extra labels."""
    return IssueInfo(
        number, title or f"tools: Do thing {number}", ("task", RULES.claimable_status, *labels), assignees, body
    )


def claimed(
    number: int,
    *,
    exempt: bool = False,
    studio: bool = False,
    networking: bool = False,
    freed: bool = False,
    paused: str | None = None,
    task: str | None = None,
) -> Card:
    """The held claim card of an issue the maintainer holds: `exempt` (the workflow label), `studio` (the Studio label,
    which also makes it exempt), `networking` (the networking label, exempt too), `freed` (its unit's place is free: the
    card no longer counts) or paused by a label such as `S-on-hold` (the card then waits while it); `task` is the task
    id the card carries."""
    return Card(
        issue=number,
        claimant=USER,
        claimed_at=START,
        task=task,
        waiting=paused,
        exempt=exempt or studio or networking,
        workflow=exempt,
        studio=studio,
        networking=networking,
        counted=not freed,
        freed=freed,
    )


class FakeSources:
    """GitHub as the check reads it: lists the test sets, failures it makes on request, and a record of the calls."""

    def __init__(self) -> None:
        self.ready_issues: list[IssueInfo] = []
        self.claims: list[Card] = []
        self.blocked_by: dict[int, list[int]] = {}
        self.pulls: list[FailedPull] = []
        #: The methods that raise ConfigError, as a GitHub that cannot be read does.
        self.down: set[str] = set()
        self.calls: list[str] = []

    def _read(self, name: str) -> None:
        self.calls.append(name)
        if name in self.down or "all" in self.down:
            raise ConfigError(f"cannot read GitHub with gh ({name} is down)")

    def ready(self) -> list[IssueInfo]:
        self._read("ready")
        return list(self.ready_issues)

    def claimed(self) -> list[Card]:
        self._read("claimed")
        return list(self.claims)

    def blockers(self, found: IssueInfo) -> list[int]:
        self._read("blockers")
        return self.blocked_by.get(found.number, [])

    def failed_pulls(self) -> list[FailedPull]:
        self._read("pulls")
        return list(self.pulls)


def full_places(scene: Scene) -> None:
    """Three reimplementation units in work and one fix unit: the four places of a machine with two build slots."""
    for name in ("a-one", "b-two", "c-three"):
        scene.unit(name)
    scene.unit("d-fix", kind="fix")


def check(
    scene: Scene,
    sources: FakeSources,
    free_gb: float | None = 500.0,
    holds: list[Hold] | None = None,
    now: datetime | None = None,
) -> Check:
    """The fill check on the scene."""
    return slot_fill.fill_check(
        scene.scratch, scene.conventions, now or scene.now(), sources, scene.build_slots, free_gb, holds
    )


def with_fill(scene: Scene, **changes: object) -> None:
    """Change `tasks.start.fill` for the scene."""
    fill = replace(scene.conventions.tasks.start.fill, **changes)  # type: ignore[arg-type]
    start = replace(scene.conventions.tasks.start, fill=fill)
    scene.conventions = replace(scene.conventions, tasks=replace(scene.conventions.tasks, start=start))


def gate_on(scene: Scene, *issues: int) -> None:
    """Turn on the start gate (off in the committed conventions) for `workflow`, the `issues` and `P-critical`."""
    with_fill(scene, gate=replace(FILL.gate, enabled=True, issues=issues))


def the_conventions() -> Conventions:
    """The committed conventions."""
    return CONVENTIONS


def folder_of(scene: Scene, name: str) -> Path:
    """The scratch folder of a task."""
    return scene.scratch / name
