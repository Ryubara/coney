# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for the engineer tracks (task_slots, `task slots` in task_cli): which units hold a place in which track,
what a brief must say and when the coordinator's start of an agent is refused.

The scratch folder is a temporary one with task files, unit records and real stamp logs, as `task create` and
`task start` leave them; nothing touches GitHub, git or the network (docs/guides/agents.md#slots).
"""

import argparse
import io
import json
import os
import subprocess
import threading
import time
from collections.abc import Callable
from dataclasses import replace
from datetime import datetime, timedelta
from pathlib import Path

import pytest
from audit_support import CONVENTIONS
from claim_support import Clock, FakeGitHub, plant_claim

from coney_tools import stamps, task_cli, task_slots
from coney_tools.config import ConfigError
from coney_tools.conventions import Conventions
from coney_tools.issue_claim_model import Card, render_card
from coney_tools.issue_claim_remote import Remote
from coney_tools.task_file import TaskFile, render_task_file
from coney_tools.unit_record import UnitRecord, write_record

RULES = CONVENTIONS.stamps
WHO = {"kind": "tool", "tool": "test", "agent": {"id": "a", "type": "engineer", "model": "m"}, "source": "hook"}
USER = "Ryubara"


class Scene:
    """A scratch folder with tasks in it, the claim cards GitHub holds for them, a clock and the committed
    conventions."""

    def __init__(self, tmp_path: Path) -> None:
        self.scratch = tmp_path / "scratch"
        self.scratch.mkdir()
        self.trees = tmp_path / "worktrees"
        self.clock = Clock()
        self.conventions: Conventions = CONVENTIONS
        #: The machine's build slots: with two units per build slot, 4 places, so three reimplementation units and the
        #: place kept for a fix fill it.
        self.build_slots = 2
        #: The held claim cards on GitHub by issue, as the claim Action keeps them (`exempt` from the label).
        self.cards: dict[int, Card] = {}
        #: Whether GitHub cannot be read, and the tasks whose cards were read, in order.
        self.down = False
        self.claim_asked: list[str] = []
        #: Every issue GitHub was asked for, and what to do on each read (a hang, a look at the lock).
        self.reads: list[int] = []
        self.on_read: Callable[[int], None] | None = None
        #: The monotonic clock of the GitHub reads.
        self.seconds = 0.0
        #: The issues of each task (each task gets its own unless given).
        self.issues_of: dict[str, tuple[int, ...]] = {}
        self._next_issue = 100
        #: GitHub for the command tests: the claim cards `task create` would have left.
        self.github = FakeGitHub(self.clock)

    def now(self) -> datetime:
        return self.clock.now()

    def task(
        self,
        name: str,
        kind: str = "feature",
        age_minutes: int = 0,
        workflow: bool = False,
        studio: bool = False,
        **changes: object,
    ) -> Path:
        """A task file as `task create` writes it, claimed `age_minutes` ago, and its held claim cards (labelled
        `workflow` or `studio` when asked)."""
        issues = changes.pop("issues", None)
        if issues is None:
            issues = (self._next_issue,)
            self._next_issue += 1
        self.issues_of[name] = issues  # type: ignore[assignment]
        values: dict[str, object] = {
            "task": name,
            "issues": issues,
            "kind": kind,
            "claimant": USER,
            "claimed_at": self.now() - timedelta(minutes=age_minutes),
            "scope": (),
            "terms": (),
        }
        if kind == "fix":
            values["parent"] = "parent"
        values.update(changes)
        for number in issues:  # type: ignore[attr-defined]
            claimed_for = str(values.get("parent") or name)
            self.cards[number] = Card(issue=number, claimant=USER, claimed_at=self.now(), task=claimed_for)
        if workflow:
            self.label(name, True)
        if studio:
            self.studio(name, True)
        path = self.scratch / name / self.conventions.tasks.file
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(render_task_file(TaskFile(**values)), encoding="utf-8")  # type: ignore[arg-type]
        return path

    def label(self, name: str, on: bool) -> None:
        """Add or remove the `workflow` label of the task's issues: the claim Action's next pass sets the card's
        `workflow`, and `exempt` while either label is on."""
        for number in self.issues_of[name]:
            card = self.cards[number]
            self.cards[number] = replace(card, workflow=on, exempt=on or card.studio)

    def studio(self, name: str, on: bool) -> None:
        """Add or remove the `studio` label of the task's issues: the claim Action's next pass sets the card's `studio`,
        and `exempt` (outside the claim limit) while either label is on."""
        for number in self.issues_of[name]:
            card = self.cards[number]
            self.cards[number] = replace(card, studio=on, exempt=on or card.workflow)

    def release(self, name: str) -> None:
        """The claims of the task were released since `task create` (idle, `/unclaim`)."""
        for number in self.issues_of[name]:
            self.cards.pop(number, None)

    def unit(
        self, name: str, kind: str = "feature", end: str | None = None, tree: bool = True, **changes: object
    ) -> None:
        """A started unit: task file, record, worktree folder and a log that ends at `end` (reviewed, finished)."""
        self.task(name, kind, **changes)  # type: ignore[arg-type]
        tree_path = self.trees / name
        if tree:
            tree_path.mkdir(parents=True, exist_ok=True)
        write_record(
            self.scratch / name / "unit.json",
            UnitRecord(name, kind, self.issues_of[name], USER, name, tree_path, "origin/main", "b" * 40, self.now()),
        )
        for step in ["assigned", "claimed", *([end] if end else [])]:
            stamps.append(
                self.scratch / name / "stamps",
                rules=RULES,
                task=name,
                step=step,
                subject="",
                result="pass",
                commit="b" * 40,
                covers={},
                detail={},
                who=WHO,
                clock=self.now,
            )

    def read_card(self, number: int) -> Card | None:
        """The stand-in for one issue's claim cards on GitHub."""
        self.reads.append(number)
        if self.on_read is not None:
            self.on_read(number)
        if self.down:
            raise ConfigError("cannot read from GitHub with gh (offline)")
        self.claim_asked += [name for name, issues in self.issues_of.items() if number in issues]
        return self.cards.get(number)

    def admit(self, brief: str, agent_type: str = "engineer", **options: object) -> task_slots.Verdict:
        return task_slots.admit(
            self.scratch,
            self.conventions,
            agent_type,
            brief,
            self.now(),
            self.read_card,
            **{"clock": lambda: self.seconds, "build_slots": self.build_slots, **options},  # type: ignore[arg-type]
        )

    def scan(self) -> task_slots.Scan:
        """What holds the places, with each live unit's labels read from its cards."""
        cards = task_slots.Cards(self.read_card)
        return task_slots.scan(
            self.scratch,
            self.conventions,
            self.now(),
            labels=lambda record: task_slots.unit_labels(record.issues, cards, self.conventions),
        )

    def holders(self, track: str = "reimplementation") -> list[str]:
        return [holder.task for holder in self.scan().held(track)]


@pytest.fixture
def scene(tmp_path: Path) -> Scene:
    return Scene(tmp_path)


def brief(task: str) -> str:
    """A brief that names its unit."""
    return f"You are an engineer.\n\nTask id: {task}\nScope: src/ai/\n"


def set_slots(scene: Scene, **changes: object) -> None:
    """Change `tasks.start.slots` for the scene."""
    slots = replace(scene.conventions.tasks.start.slots, **changes)  # type: ignore[arg-type]
    start = replace(scene.conventions.tasks.start, slots=slots)
    scene.conventions = replace(scene.conventions, tasks=replace(scene.conventions.tasks, start=start))


def full(scene: Scene) -> None:
    """Three reimplementation units in work: with the place kept for a fix, every place of the scene's machine."""
    for name in ("a-one", "b-two", "c-three"):
        scene.unit(name)


# --- the numbers ----------------------------------------------------------------------------------------------------


def test_the_committed_tracks_and_machine_limit() -> None:
    slots = CONVENTIONS.tasks.start.slots
    tracks = slots.tracks
    assert (tracks.reimplementation.min, tracks.reimplementation.max) == (1, None)
    assert (tracks.fix.min, tracks.fix.max) == (1, 1)
    assert (tracks.studio.min, tracks.studio.max) == (0, None)
    pairs = [(m.model, m.effort) for m in tracks.studio.models]
    assert pairs == [("opus", "high"), ("opus", "medium"), ("sonnet", "high")]
    assert (slots.per_build_slot, slots.enforce) == (2, True)
    assert slots.exempt_workflow is True
    assert slots.agent_types == ("engineer", "tooling")


@pytest.mark.parametrize(("build_slots", "places"), [(1, 2), (2, 4), (5, 10), (0, 2)])
def test_the_machine_limit_follows_the_build_slots(build_slots: int, places: int) -> None:
    assert task_slots.machine_limit(CONVENTIONS, build_slots) == places


# --- what holds a slot ----------------------------------------------------------------------------------------------


def test_a_started_unit_holds_a_reimplementation_place_and_a_fix_the_fix_place(scene: Scene) -> None:
    scene.unit("a-one")
    scene.unit("b-fix", kind="fix")
    assert scene.holders("reimplementation") == ["a-one"] and scene.holders("fix") == ["b-fix"]


@pytest.mark.parametrize("end", ["reviewed", "finished"])
def test_a_unit_that_passed_review_or_finished_holds_no_slot(scene: Scene, end: str) -> None:
    scene.unit("a-one", end=end)
    assert scene.holders() == []


def test_a_unit_whose_worktree_is_gone_holds_no_slot(scene: Scene) -> None:
    scene.unit("a-one", tree=False)
    assert scene.holders() == []


def test_a_unit_with_a_damaged_log_keeps_its_slot(scene: Scene) -> None:
    scene.unit("a-one")
    segment = scene.scratch / "a-one" / "stamps" / "0001.jsonl"
    segment.write_text(segment.read_text(encoding="utf-8").replace('"seq":1', '"seq":9'), encoding="utf-8")
    assert scene.holders() == ["a-one"]


def test_a_workflow_unit_holds_no_slot_but_is_listed(scene: Scene) -> None:
    scene.unit("a-flow", kind="tooling", workflow=True)
    current = scene.scan()
    assert current.holders == () and current.exempt == ("a-flow",)


def test_without_the_cards_every_unit_counts(scene: Scene) -> None:
    scene.unit("a-flow", kind="tooling", workflow=True)
    assert [h.task for h in task_slots.scan(scene.scratch, scene.conventions, scene.now()).holders] == ["a-flow"]


def test_the_label_added_after_task_create_exempts_the_unit(scene: Scene) -> None:
    scene.unit("a-flow", kind="tooling")
    assert scene.holders() == ["a-flow"]
    scene.label("a-flow", True)
    assert scene.holders() == [] and scene.scan().exempt == ("a-flow",)


def test_the_label_removed_after_task_create_ends_the_exemption(scene: Scene) -> None:
    scene.unit("a-flow", kind="tooling", workflow=True)
    scene.label("a-flow", False)
    assert scene.holders() == ["a-flow"]


def mark_workflow_by_hand(scene: Scene, name: str) -> None:
    """Edit `workflow: true` into the YAML block of a task file, as somebody might."""
    path = scene.scratch / name / scene.conventions.tasks.file
    text = path.read_text(encoding="utf-8").replace("\n---\n# Task", "\nworkflow: true\n---\n# Task", 1)
    path.write_text(text, encoding="utf-8")
    assert "workflow: true" in path.read_text(encoding="utf-8")


def test_a_task_file_that_says_workflow_grants_nothing(scene: Scene) -> None:
    # Only the label (through the claim card) exempts: a hand edit of TASK.md does not.
    full(scene)
    mark_workflow_by_hand(scene, "a-one")
    assert scene.holders() == ["a-one", "b-two", "c-three"]
    scene.task("d-four")
    mark_workflow_by_hand(scene, "d-four")
    assert not scene.admit(brief("d-four")).allowed


def test_a_unit_whose_cards_cannot_be_read_is_counted_and_named(scene: Scene) -> None:
    scene.unit("a-flow", kind="tooling", workflow=True)
    scene.down = True
    current = scene.scan()
    assert [h.task for h in current.holders] == ["a-flow"] and current.unchecked == ("a-flow",)
    assert any("could not be read" in line and "a-flow" in line for line in task_slots.report(current, CONVENTIONS, 2))


def test_tooling_that_is_not_the_workflow_holds_a_slot(scene: Scene) -> None:
    scene.unit("a-tools", kind="tooling")
    assert scene.holders() == ["a-tools"]


def test_the_cutover_turns_the_workflow_exemption_off(scene: Scene) -> None:
    set_slots(scene, exempt_workflow=False)
    scene.unit("a-flow", kind="tooling", workflow=True)
    assert scene.holders() == ["a-flow"]


def test_folders_that_are_not_units_are_ignored(scene: Scene) -> None:
    (scene.scratch / "pcsx2").mkdir()
    (scene.scratch / "notes.txt").write_text("x", encoding="utf-8")
    assert scene.holders() == []


def test_a_unit_record_that_cannot_be_read_keeps_its_slot(scene: Scene) -> None:
    scene.task("a-odd")
    (scene.scratch / "a-odd" / "unit.json").write_text("{", encoding="utf-8")
    current = scene.scan()
    assert [(h.task, h.state) for h in current.holders] == [("a-odd", "record unreadable")]


def test_worktrees_no_record_names_are_listed_as_unknown(scene: Scene) -> None:
    scene.unit("a-one")
    old = scene.trees / "old-branch"
    old.mkdir(parents=True)
    current = task_slots.scan(scene.scratch, scene.conventions, scene.now(), [scene.trees / "a-one", old])
    assert current.unknown_trees == (old,)
    lines = task_slots.report(current, scene.conventions, scene.build_slots)
    assert any("without a unit record" in line and "old-branch" in line for line in lines)


def test_a_fix_task_without_a_unit_is_queued_oldest_first(scene: Scene) -> None:
    scene.task("newer-fix", kind="fix", age_minutes=5)
    scene.task("older-fix", kind="fix", age_minutes=50)
    scene.unit("started-fix", kind="fix")
    current = task_slots.scan(scene.scratch, scene.conventions, scene.now())
    assert current.queued_fixes == ("older-fix", "newer-fix")


# --- the brief ------------------------------------------------------------------------------------------------------


@pytest.mark.parametrize(
    "text",
    [
        "Task id: 733-fix-it",
        "task ID : 733-fix-it  (from task create)",
        "Intro\n\nTask id: 733-fix-it\nmore",
        "  Task id: 733-fix-it",
    ],
)
def test_the_brief_names_its_task(text: str) -> None:
    assert task_slots.brief_task(text) == "733-fix-it"


@pytest.mark.parametrize(
    "text",
    [
        "- Task id: 733-fix-it",
        "**Task id:** 733-fix-it",
        "> `Task id: 733-fix-it`",
        "the Task id: 733-fix-it is the one",
        "Slot: none - a reason",
        "Task id: <id>",
        "Do the Task: it is hard",
    ],
)
def test_a_brief_that_quotes_the_rule_or_marks_it_up_names_nothing(text: str) -> None:
    assert task_slots.brief_task(text) is None


@pytest.mark.parametrize("ending", ["\r\n", "\r"])
def test_a_brief_with_windows_or_old_mac_line_ends_names_its_task(ending: str) -> None:
    assert task_slots.brief_task(ending.join(["Intro", "Task id: 733-fix-it", "more", ""])) == "733-fix-it"
    assert task_slots.brief_task("Task id: 733-fix-it" + ending) == "733-fix-it"


def test_the_first_task_id_line_of_a_brief_wins() -> None:
    assert task_slots.brief_task("Task id: first-one\nTask id: second-one\n") == "first-one"


# --- admit ----------------------------------------------------------------------------------------------------------


def test_a_free_place_lets_the_start_through(scene: Scene) -> None:
    scene.unit("a-one")
    scene.task("d-four")
    verdict = scene.admit(brief("d-four"))
    assert verdict.allowed and "2 of 4 places on this machine" in verdict.message
    assert "reimplementation track" in verdict.message


def test_a_full_slot_refuses_the_start_and_names_the_busy_units(scene: Scene) -> None:
    full(scene)
    scene.task("d-four")
    verdict = scene.admit(brief("d-four"))
    assert not verdict.allowed
    for name in ("a-one", "b-two", "c-three"):
        assert name in verdict.message
    assert "no place on this machine for a reimplementation unit (3 of 4 in use" in verdict.message
    assert "1 kept for fix" in verdict.message and "2 build slot(s) x 2" in verdict.message
    assert "passes Review" in verdict.message and "agents.md#slots" in verdict.message


def test_a_passing_review_frees_the_slot(scene: Scene) -> None:
    full(scene)
    scene.task("d-four")
    assert not scene.admit(brief("d-four")).allowed
    stamps.append(
        scene.scratch / "b-two" / "stamps",
        rules=RULES,
        task="b-two",
        step="reviewed",
        subject="",
        result="pass",
        commit="b" * 40,
        covers={},
        detail={},
        who=WHO,
        clock=scene.now,
    )
    assert scene.admit(brief("d-four")).allowed


def test_a_unit_waiting_for_ci_does_not_block_the_next_task(scene: Scene) -> None:
    for name in ("a-one", "b-two", "c-three"):
        scene.unit(name, end="reviewed")
    scene.task("d-four")
    assert scene.admit(brief("d-four")).allowed


def test_restarting_the_agent_of_a_unit_in_work_is_allowed_when_full(scene: Scene) -> None:
    full(scene)
    verdict = scene.admit(brief("b-two"))
    assert verdict.allowed and "holds a reimplementation place already" in verdict.message


def test_the_place_kept_for_a_fix_lets_it_start_on_a_full_machine(scene: Scene) -> None:
    full(scene)
    scene.task("e-fix", kind="fix")
    assert scene.admit(brief("e-fix")).allowed  # reimplementation fills the rest, the fix's place is kept
    scene.unit("f-fix", kind="fix")
    verdict = scene.admit(brief("e-fix"))
    assert not verdict.allowed and "the fix track is full (1 of 1 in use)" in verdict.message
    assert "f-fix" in verdict.message


def test_a_refused_fix_start_names_the_queue_oldest_first(scene: Scene) -> None:
    scene.unit("f-fix", kind="fix")
    scene.task("g-fix", kind="fix", age_minutes=10)
    scene.task("h-fix", kind="fix", age_minutes=60)
    verdict = scene.admit(brief("g-fix"))
    assert not verdict.allowed and "h-fix, g-fix" in verdict.message


# --- the tracks -----------------------------------------------------------------------------------------------------


def studio_brief(task: str) -> str:
    """A brief for a Studio unit."""
    return f"You are a tooling engineer on Coney Studio.\n\nTask id: {task}\n"


def admit_studio(scene: Scene, task: str, model: str = "opus", effort: str = "medium") -> task_slots.Verdict:
    """The coordinator starts a Studio agent on an allowed model and effort unless told otherwise."""
    return scene.admit(studio_brief(task), agent_type="tooling", model=model, effort=effort)


def test_a_studio_unit_holds_a_studio_place(scene: Scene) -> None:
    scene.unit("s-ui", kind="tooling", studio=True)
    assert scene.holders("studio") == ["s-ui"] and scene.holders("reimplementation") == []


def test_a_studio_start_is_admitted_past_three_reimplementation_units(scene: Scene) -> None:
    # The issue's first test: with the old 3 task slots full, a Studio start still goes ahead on a machine with room.
    scene.build_slots = 5
    full(scene)
    scene.task("s-ui", kind="tooling", studio=True)
    verdict = admit_studio(scene, "s-ui")
    assert verdict.allowed and "4 of 10 places on this machine in use with s-ui (studio track)" in verdict.message


def test_a_second_fix_unit_is_refused_even_with_room_on_the_machine(scene: Scene) -> None:
    # The issue's second test: the fix track holds one unit, however many places the machine has.
    scene.build_slots = 5
    scene.unit("f-fix", kind="fix")
    scene.task("g-fix", kind="fix")
    verdict = scene.admit(brief("g-fix"))
    assert not verdict.allowed and "the fix track is full (1 of 1 in use)" in verdict.message
    assert "f-fix" in verdict.message and "queued: g-fix" in verdict.message


def test_studio_fills_the_machine_but_the_places_kept_for_reimplementation_and_fix(scene: Scene) -> None:
    # 4 places: Studio may take 2, one stays for a reimplementation unit and one for a fix.
    for name in ("s-one", "s-two"):
        scene.unit(name, kind="tooling", studio=True)
    scene.task("s-three", kind="tooling", studio=True)
    verdict = admit_studio(scene, "s-three")
    assert not verdict.allowed and "no place on this machine for a studio unit (2 of 4 in use" in verdict.message
    assert "1 kept for reimplementation" in verdict.message and "1 kept for fix" in verdict.message
    assert "s-one" in verdict.message and "[studio]" in verdict.message
    scene.task("r-one")
    assert scene.admit(brief("r-one")).allowed  # the kept place is reimplementation's own


def test_reimplementation_has_no_cap_but_the_machine(scene: Scene) -> None:
    scene.build_slots = 3  # 6 places
    for name in ("r-one", "r-two", "r-three", "r-four"):
        scene.unit(name)
    scene.task("r-five")
    assert scene.admit(brief("r-five")).allowed  # 4 in work, the fifth fits beside the place kept for a fix
    scene.unit("r-five")
    scene.task("r-six")
    assert not scene.admit(brief("r-six")).allowed  # the sixth place is the fix's


def test_once_reimplementation_runs_studio_may_use_every_place_but_the_fix(scene: Scene) -> None:
    scene.build_slots = 3  # 6 places
    scene.unit("r-one")
    for name in ("s-one", "s-two", "s-three"):
        scene.unit(name, kind="tooling", studio=True)
    scene.task("s-four", kind="tooling", studio=True)
    assert admit_studio(scene, "s-four").allowed  # 4 in work, the fifth fits beside the fix's place
    scene.unit("s-four", kind="tooling", studio=True)
    scene.task("s-five", kind="tooling", studio=True)
    assert not admit_studio(scene, "s-five").allowed


def test_reimplementation_may_not_take_the_place_kept_for_a_fix(scene: Scene) -> None:
    full(scene)
    scene.task("d-four")
    verdict = scene.admit(brief("d-four"))
    assert not verdict.allowed and "1 kept for fix" in verdict.message


def test_a_full_machine_refuses_every_track_and_says_its_limit(scene: Scene) -> None:
    full(scene)
    scene.unit("f-fix", kind="fix")
    scene.task("s-ui", kind="tooling", studio=True)
    verdict = admit_studio(scene, "s-ui")
    assert (
        not verdict.allowed
        and "(4 of 4 in use" in verdict.message
        and "build_slots in coney.local.toml" in verdict.message
    )


def test_a_studio_label_added_after_task_create_moves_the_unit_to_studio(scene: Scene) -> None:
    scene.unit("a-tools", kind="tooling")
    assert scene.holders("reimplementation") == ["a-tools"]
    scene.studio("a-tools", True)
    assert scene.holders("studio") == ["a-tools"] and scene.holders("reimplementation") == []


def test_a_studio_fix_is_a_fix(scene: Scene) -> None:
    scene.unit("s-fix", kind="fix", studio=True)
    assert scene.holders("fix") == ["s-fix"] and scene.holders("studio") == []


def test_an_issue_labelled_workflow_and_studio_is_a_workflow_unit_until_the_cutover(scene: Scene) -> None:
    scene.unit("w-studio", kind="tooling", workflow=True, studio=True)
    (number,) = scene.issues_of["w-studio"]
    assert scene.cards[number].workflow and scene.cards[number].studio  # one issue, both labels
    assert scene.scan().exempt == ("w-studio",) and scene.holders("studio") == []
    scene.task("w-next", kind="tooling", workflow=True, studio=True)
    verdict = scene.admit(brief("w-next"), agent_type="tooling", model="haiku", effort="low")
    assert verdict.allowed and "workflow task" in verdict.message  # no Studio model rule for a workflow unit
    set_slots(scene, exempt_workflow=False)
    assert scene.holders("studio") == ["w-studio"]  # after the cutover the Studio label decides


def test_a_unit_on_a_workflow_issue_and_a_studio_issue_is_a_workflow_unit(scene: Scene) -> None:
    scene.unit("w-two", kind="tooling", issues=(900, 901))
    scene.cards[900] = replace(scene.cards[900], workflow=True, exempt=True)
    scene.cards[901] = replace(scene.cards[901], studio=True, exempt=True)
    assert scene.scan().exempt == ("w-two",)


def test_an_old_card_without_the_workflow_field_reads_from_exempt(scene: Scene) -> None:
    scene.unit("w-old", kind="tooling")
    (number,) = scene.issues_of["w-old"]
    scene.cards[number] = replace(scene.cards[number], exempt=True)  # written before `workflow` existed
    assert scene.scan().exempt == ("w-old",)


def test_a_studio_card_that_was_read_decides_when_another_cannot_be(scene: Scene) -> None:
    scene.unit("s-two", kind="tooling", issues=(910, 911))
    scene.cards[910] = replace(scene.cards[910], studio=True, exempt=True)

    def second_fails(number: int) -> None:
        if number == 911:
            raise ConfigError("gh timed out")

    scene.on_read = second_fails
    current = scene.scan()
    assert [h.task for h in current.held("studio")] == ["s-two"] and current.unchecked == ()


def test_a_unit_whose_labels_cannot_be_read_counts_as_reimplementation(scene: Scene) -> None:
    scene.unit("s-ui", kind="tooling", studio=True)
    scene.unit("b-fix", kind="fix")
    scene.down = True
    current = scene.scan()
    assert [h.task for h in current.held("reimplementation")] == ["s-ui"]
    assert [h.task for h in current.held("fix")] == ["b-fix"] and current.unchecked == ("b-fix", "s-ui")


def test_a_start_whose_labels_cannot_be_read_is_judged_as_reimplementation_without_a_model_check(
    scene: Scene,
) -> None:
    for name in ("s-one", "s-two"):
        scene.unit(name, kind="tooling", studio=True)
    scene.task("s-three", kind="feature", studio=True)
    scene.down = True
    verdict = admit_studio(scene, "s-three", "haiku", "low")
    assert verdict.allowed and "(reimplementation track)" in verdict.message and "not checked" in verdict.warning


def test_a_tooling_unit_does_not_fill_the_place_kept_for_game_work(scene: Scene) -> None:
    # 4 places, one kept for a fix, one for game work: two tooling units fill the rest and a third is refused, but a
    # feature unit still fits in the place kept for it (the review's blocker: tooling must not be "at least one").
    scene.unit("t-one", kind="tooling")
    scene.unit("t-two", kind="docs")
    scene.task("t-three", kind="tooling")
    verdict = scene.admit(brief("t-three"))
    assert not verdict.allowed and "1 kept for reimplementation" in verdict.message
    scene.task("r-one")
    assert scene.admit(brief("r-one")).allowed


def test_studio_may_not_take_the_place_kept_for_game_work_while_only_tooling_runs(scene: Scene) -> None:
    scene.unit("t-one", kind="tooling")
    scene.unit("s-one", kind="tooling", studio=True)
    scene.task("s-two", kind="tooling", studio=True)
    verdict = admit_studio(scene, "s-two")
    assert not verdict.allowed and "1 kept for reimplementation" in verdict.message


def test_the_report_says_when_only_tooling_runs_in_the_reimplementation_track(scene: Scene) -> None:
    warning = "no reimplementation unit is in work"
    scene.unit("t-one", kind="tooling")
    assert any(
        line.startswith(warning) for line in task_slots.report(scene.scan(), scene.conventions, scene.build_slots)
    )
    scene.unit("r-one")
    assert not any(
        line.startswith(warning) for line in task_slots.report(scene.scan(), scene.conventions, scene.build_slots)
    )


def test_the_report_says_when_no_reimplementation_unit_runs(scene: Scene) -> None:
    scene.unit("s-ui", kind="tooling", studio=True)
    lines = task_slots.report(scene.scan(), scene.conventions, scene.build_slots)
    assert "no reimplementation unit is in work: the track keeps at least one going" in lines
    assert "studio: 1 in work (no minimum, no cap but the machine's)" in lines


# --- the Studio models ------------------------------------------------------------------------------------------------


@pytest.mark.parametrize(
    ("model", "effort"), [("opus", "high"), ("opus", "medium"), ("sonnet", "high"), ("Opus", "HIGH")]
)
def test_a_studio_unit_starts_on_an_allowed_model_and_effort(scene: Scene, model: str, effort: str) -> None:
    scene.task("s-ui", kind="tooling", studio=True)
    assert admit_studio(scene, "s-ui", model, effort).allowed


@pytest.mark.parametrize(
    ("model", "effort"), [("sonnet", "medium"), ("haiku", "high"), ("opus", "low"), ("", ""), ("opus", "")]
)
def test_a_studio_unit_on_another_model_or_effort_is_refused(scene: Scene, model: str, effort: str) -> None:
    scene.task("s-ui", kind="tooling", studio=True)
    verdict = admit_studio(scene, "s-ui", model, effort)
    assert not verdict.allowed and "is a Studio task" in verdict.message
    assert "opus at high (research)" in verdict.message and "sonnet at high (simple development)" in verdict.message
    assert not (scene.scratch / "s-ui" / task_slots.RESERVATION_FILE).exists()


def test_the_model_rule_binds_only_studio(scene: Scene) -> None:
    scene.task("r-one")
    assert scene.admit(brief("r-one"), model="haiku", effort="low").allowed
    scene.task("s-fix", kind="fix", studio=True)
    assert scene.admit(brief("s-fix"), model="sonnet", effort="medium").allowed


def test_the_model_rule_is_not_enforced_with_the_limits_off(scene: Scene) -> None:
    set_slots(scene, enforce=False)
    scene.task("s-ui", kind="tooling", studio=True)
    assert admit_studio(scene, "s-ui", "haiku", "low").allowed


def test_the_command_passes_the_model_and_effort(
    scene: Scene, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    scene.task("s-ui", kind="tooling", issues=(12,))
    scene.github.add_issue(12)
    card = Card(issue=12, claimant=USER, claimed_at=scene.now(), task="s-ui", exempt=True, studio=True)
    scene.github.create_comment(12, render_card(card, CONVENTIONS.claims))
    words = ("--admit", "--agent-type=tooling", "--task", "s-ui")
    code, out = run(scene, monkeypatch, capsys, *words, "--model", "haiku", "--effort", "high")
    assert code == task_slots.REFUSED and "is a Studio task" in out
    code, out = run(scene, monkeypatch, capsys, *words, "--model", "opus", "--effort", "high")
    assert code == 0 and "studio track" in out


def test_a_workflow_task_never_waits(scene: Scene) -> None:
    full(scene)
    scene.task("t-flow", kind="tooling", workflow=True)
    verdict = scene.admit(brief("t-flow"), agent_type="tooling")
    assert verdict.allowed and "workflow task" in verdict.message


def test_tooling_that_is_not_the_workflow_waits_like_any_task(scene: Scene) -> None:
    full(scene)
    scene.task("t-tools", kind="tooling")
    assert not scene.admit(brief("t-tools"), agent_type="tooling").allowed


def test_the_exemption_ends_when_the_cutover_turns_it_off(scene: Scene) -> None:
    set_slots(scene, exempt_workflow=False)
    full(scene)
    scene.task("t-flow", kind="tooling", workflow=True)
    assert not scene.admit(brief("t-flow"), agent_type="tooling").allowed


def test_a_brief_can_name_a_workflow_task_to_get_past_the_guard(scene: Scene) -> None:
    # The brief is an honour system: the guard cannot tell what the agent will really work on. `task start` of the
    # agent is the backstop (docs/guides/agents.md#slots).
    full(scene)
    scene.task("t-flow", kind="tooling", workflow=True)
    assert scene.admit("Fix the doors.\nTask id: t-flow\n").allowed


def test_a_start_over_the_limit_only_passes_with_a_note_when_it_is_not_enforced(scene: Scene) -> None:
    slots = replace(scene.conventions.tasks.start.slots, enforce=False)
    start = replace(scene.conventions.tasks.start, slots=slots)
    scene.conventions = replace(scene.conventions, tasks=replace(scene.conventions.tasks, start=start))
    full(scene)
    scene.task("d-four")
    verdict = scene.admit(brief("d-four"))
    assert verdict.allowed and "not enforced" in verdict.message


@pytest.mark.parametrize("agent_type", ["reviewer", "Explore", "Plan", "fork", "claude-code-guide", ""])
def test_a_type_that_holds_no_slot_always_passes(scene: Scene, agent_type: str) -> None:
    full(scene)
    assert scene.admit("no task here", agent_type=agent_type).allowed


def test_a_brief_without_a_task_id_is_refused(scene: Scene) -> None:
    verdict = scene.admit("Fix the doors.")
    assert not verdict.allowed and "Task id: <id>" in verdict.message and "no way to start" in verdict.message
    assert scene.claim_asked == []


@pytest.mark.parametrize("brief_text", ["Slot: none - Tether repository", "Slot: none", "Fix it. Slot: none <reason>"])
def test_slot_none_is_not_accepted_for_an_engineer_or_a_tooling_agent(scene: Scene, brief_text: str) -> None:
    for agent_type in ("engineer", "tooling"):
        verdict = scene.admit(brief_text, agent_type=agent_type)
        assert not verdict.allowed and "names no unit" in verdict.message


@pytest.mark.parametrize("agent_type", ["Engineer", "ENGINEER", " engineer ", "Tooling", "tooling "])
def test_the_type_is_read_without_case_or_blanks(scene: Scene, agent_type: str) -> None:
    assert not scene.admit("Fix the doors.", agent_type=agent_type).allowed


def test_a_task_without_a_task_file_is_refused(scene: Scene) -> None:
    verdict = scene.admit(brief("made-up"))
    assert not verdict.allowed and "no task file for 'made-up'" in verdict.message and "task create" in verdict.message


def test_the_task_argument_beats_the_brief(scene: Scene) -> None:
    scene.task("d-four")
    assert scene.admit("nothing", task="d-four").allowed


# --- reservations ---------------------------------------------------------------------------------------------------


def test_an_agent_let_start_keeps_its_slot_before_task_start(scene: Scene) -> None:
    scene.unit("a-one")
    scene.unit("b-two")
    for name in ("c-three", "d-four"):
        scene.task(name)
    assert scene.admit(brief("c-three"), reserve=True).allowed
    assert scene.holders() == ["a-one", "b-two", "c-three"]
    verdict = scene.admit(brief("d-four"), reserve=True)
    assert not verdict.allowed and "c-three (feature, reserved since" in verdict.message
    assert not (scene.scratch / "d-four" / task_slots.RESERVATION_FILE).exists()


def test_a_check_without_reserve_keeps_nothing(scene: Scene) -> None:
    scene.task("c-three")
    assert scene.admit(brief("c-three")).allowed
    assert scene.holders() == []


def test_a_reservation_expires(scene: Scene) -> None:
    scene.task("c-three")
    assert scene.admit(brief("c-three"), reserve=True).allowed
    scene.clock.advance(minutes=scene.conventions.tasks.start.slots.reserve_minutes + 1)
    assert scene.holders() == []


def test_the_unit_replaces_its_reservation(scene: Scene) -> None:
    scene.task("c-three")
    assert scene.admit(brief("c-three"), reserve=True).allowed
    scene.clock.advance(minutes=2)
    scene.unit("c-three")
    current = task_slots.scan(scene.scratch, scene.conventions, scene.now())
    assert [(h.task, h.state) for h in current.holders] == [("c-three", "in work")]


def test_a_reservation_does_not_outlive_the_review_of_its_unit(scene: Scene) -> None:
    scene.task("c-three")
    assert scene.admit(brief("c-three"), reserve=True).allowed
    scene.clock.advance(minutes=2)
    scene.unit("c-three", end="reviewed")
    assert scene.holders() == []


def test_a_damaged_reservation_is_ignored(scene: Scene) -> None:
    scene.task("c-three")
    (scene.scratch / "c-three" / task_slots.RESERVATION_FILE).write_text("{", encoding="utf-8")
    assert scene.holders() == []


def test_a_reservation_records_its_track(scene: Scene) -> None:
    scene.task("s-ui", kind="tooling", studio=True)
    assert scene.admit(brief("s-ui"), agent_type="tooling", reserve=True, model="sonnet", effort="high").allowed
    data = json.loads((scene.scratch / "s-ui" / task_slots.RESERVATION_FILE).read_text(encoding="utf-8"))
    assert data["track"] == "studio" and scene.holders("studio") == ["s-ui"]


def test_a_reservation_from_before_the_tracks_reads_as_reimplementation(scene: Scene) -> None:
    scene.task("c-three")
    at = scene.now().isoformat().replace("+00:00", "Z")
    old = {"v": 1, "task": "c-three", "kind": "feature", "slot": "task", "agent_type": "engineer", "at": at}
    (scene.scratch / "c-three" / task_slots.RESERVATION_FILE).write_text(json.dumps(old), encoding="utf-8")
    assert scene.holders("reimplementation") == ["c-three"]


def test_a_workflow_start_reserves_nothing(scene: Scene) -> None:
    scene.task("t-flow", kind="tooling", workflow=True)
    assert scene.admit(brief("t-flow"), agent_type="tooling", reserve=True).allowed
    assert not (scene.scratch / "t-flow" / task_slots.RESERVATION_FILE).exists()


# --- the lock -------------------------------------------------------------------------------------------------------


def test_the_lock_is_taken_and_released(scene: Scene) -> None:
    with task_slots.locked(scene.scratch):
        assert (scene.scratch / task_slots.LOCK_FILE).exists()
    assert not (scene.scratch / task_slots.LOCK_FILE).exists()


def test_a_held_lock_makes_the_caller_give_up(scene: Scene) -> None:
    (scene.scratch / task_slots.LOCK_FILE).write_text("", encoding="utf-8")
    ticks = iter(range(0, 1000, 5))
    with (
        pytest.raises(ConfigError),
        task_slots.locked(scene.scratch, clock=lambda: float(next(ticks)), sleep=lambda _: None),
    ):
        pass


def test_a_stale_lock_is_replaced(scene: Scene) -> None:
    lock = scene.scratch / task_slots.LOCK_FILE
    lock.write_text("", encoding="utf-8")
    old = time.time() - task_slots.LOCK_STALE_SECONDS - 5
    os.utime(lock, (old, old))
    with task_slots.locked(scene.scratch):
        pass
    assert not lock.exists()


# --- the command ----------------------------------------------------------------------------------------------------


def run(
    scene: Scene,
    monkeypatch: pytest.MonkeyPatch,
    capsys: pytest.CaptureFixture[str],
    *words: str,
    stdin: str = "",
    remote: Remote | None = None,
) -> tuple[int, str]:
    """`coney-tools task slots ...` against the scene's scratch folder and GitHub; (exit code, what it printed)."""
    monkeypatch.setattr(task_cli, "scratch_folder", lambda root: scene.scratch)
    monkeypatch.setattr(task_cli, "load_conventions", lambda root: scene.conventions)
    monkeypatch.setattr(task_cli, "find_repo_root", lambda start: scene.scratch)
    monkeypatch.setattr(task_cli, "_workspace_trees", lambda root: [])
    monkeypatch.setattr(task_cli, "_build_slots", lambda root: scene.build_slots)
    monkeypatch.setattr("sys.stdin", io.TextIOWrapper(io.BytesIO(stdin.encode("utf-8"))))
    group = argparse.ArgumentParser().add_subparsers(dest="group")
    task_cli.add_task_parser(group)
    args = group.choices["task"].parse_args(["slots", *words])
    code = task_cli.run_task(args, now=scene.now, remote=remote or scene.github)
    return code, capsys.readouterr().out


def claimed(scene: Scene, name: str, issue: int | None = None, login: str = USER) -> None:
    """The claim `task create` made for `name`, held on GitHub now (on the task's own issue unless one is given)."""
    number = issue if issue is not None else scene.issues_of[name][0]
    if number not in scene.github.items:
        scene.github.add_issue(number)
    plant_claim(scene.github, number, login, task=name)


def workflow_card(scene: Scene, name: str) -> None:
    """The held card of a task whose issue carries the `workflow` label, on the command tests' GitHub."""
    number = scene.issues_of[name][0]
    scene.github.add_issue(number)
    card = Card(issue=number, claimant=USER, claimed_at=scene.now(), task=name, exempt=True)
    scene.github.create_comment(number, render_card(card, CONVENTIONS.claims))


def test_the_report_lists_both_slots(
    scene: Scene, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    scene.unit("a-one")
    scene.unit("b-fix", kind="fix")
    scene.unit("t-flow", kind="tooling")
    workflow_card(scene, "t-flow")
    scene.task("c-fix", kind="fix")
    code, out = run(scene, monkeypatch, capsys)
    assert code == 0
    assert "places: 2 of 4 in use (2 build slot(s) x 2, every track together)" in out
    assert "reimplementation: 1 in work (at least 1 kept, no cap but the machine's)" in out
    assert "fix: 1 in work (at least 1 kept, at most 1)" in out and "studio: 0 in work" in out
    assert "a-one" in out and "b-fix" in out and "t-flow" in out and "c-fix" in out


def test_the_json_report(scene: Scene, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]) -> None:
    scene.unit("a-one")
    code, out = run(scene, monkeypatch, capsys, "--json")
    data = json.loads(out)
    assert code == 0 and data["enforce"] is True and data["machine"] == {"limit": 4, "build_slots": 2}
    reimplementation = data["tracks"]["reimplementation"]
    assert (reimplementation["min"], reimplementation["max"]) == (1, None)
    assert reimplementation["holders"][0]["task"] == "a-one" and data["tracks"]["fix"]["max"] == 1


def test_admit_reads_the_brief_from_standard_input(
    scene: Scene, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    full(scene)
    scene.task("d-four", issues=(11,))
    claimed(scene, "d-four", 11)
    code, out = run(
        scene, monkeypatch, capsys, "--admit", "--agent-type=engineer", "--brief", "-", stdin=brief("d-four")
    )
    assert code == 3 and "no place on this machine" in out and "a-one" in out
    claimed(scene, "a-one")
    code, out = run(scene, monkeypatch, capsys, "--admit", "--task", "a-one")
    assert code == 0 and "holds a reimplementation place already" in out


def test_admit_reserves_only_when_asked(
    scene: Scene, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    scene.task("d-four")
    claimed(scene, "d-four")
    assert run(scene, monkeypatch, capsys, "--admit", "--task", "d-four")[0] == 0
    assert not (scene.scratch / "d-four" / task_slots.RESERVATION_FILE).exists()
    assert run(scene, monkeypatch, capsys, "--admit", "--task", "d-four", "--reserve")[0] == 0
    assert (scene.scratch / "d-four" / task_slots.RESERVATION_FILE).is_file()


def test_a_task_file_whose_claim_was_released_is_refused(
    scene: Scene, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    scene.task("d-four", issues=(10,))
    scene.github.add_issue(10)  # the issue is there, the claim card is not
    code, out = run(scene, monkeypatch, capsys, "--admit", "--brief", "-", "--reserve", stdin=brief("d-four"))
    assert code == task_slots.REFUSED and "no claim to work under" in out and "not held now" in out
    assert not (scene.scratch / "d-four" / task_slots.RESERVATION_FILE).exists()


def test_a_claim_held_by_someone_else_or_for_another_task_is_refused(
    scene: Scene, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    scene.task("d-four", issues=(10,))
    claimed(scene, "d-four", login="alice")
    code, out = run(scene, monkeypatch, capsys, "--admit", "--task", "d-four")
    assert code == 3 and "claimed by alice" in out
    claimed(scene, "other-task", 10)
    code, out = run(scene, monkeypatch, capsys, "--admit", "--task", "d-four")
    assert code == 3 and "'other-task', not 'd-four'" in out


def test_a_fix_task_works_under_the_claim_of_its_parent(
    scene: Scene, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    scene.task("e-fix", kind="fix", parent="the-parent")
    claimed(scene, "the-parent", scene.issues_of["e-fix"][0])
    assert run(scene, monkeypatch, capsys, "--admit", "--task", "e-fix")[0] == 0


def test_a_held_claim_with_a_full_slot_is_refused_for_the_slot(
    scene: Scene, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    full(scene)
    scene.task("d-four", issues=(11,))
    claimed(scene, "d-four", 11)
    code, out = run(scene, monkeypatch, capsys, "--admit", "--task", "d-four")
    assert code == 3 and "no place on this machine" in out and "no claim" not in out


class Down:
    """A GitHub that cannot be read."""

    def comments(self, number: int) -> list[object]:
        raise ConfigError("cannot read from GitHub with gh (offline)")


def test_a_github_that_cannot_be_read_skips_the_claim_but_still_counts_and_reserves(
    scene: Scene, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    scene.task("d-four")
    down: Remote = Down()  # type: ignore[assignment]
    code, out = run(scene, monkeypatch, capsys, "--admit", "--task", "d-four", "--reserve", remote=down)
    assert code == 0 and "1 of 4 places" in out
    assert "warning: the claim of 'd-four' was not checked" in out and "offline" in out
    assert (scene.scratch / "d-four" / task_slots.RESERVATION_FILE).is_file()


def test_a_github_that_cannot_be_read_does_not_open_a_full_slot(
    scene: Scene, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    full(scene)
    scene.task("d-four")
    down: Remote = Down()  # type: ignore[assignment]
    code, out = run(scene, monkeypatch, capsys, "--admit", "--task", "d-four", "--reserve", remote=down)
    assert code == task_slots.REFUSED and "no place on this machine" in out
    assert not (scene.scratch / "d-four" / task_slots.RESERVATION_FILE).exists()


def test_a_github_that_cannot_be_read_counts_a_workflow_task(scene: Scene) -> None:
    full(scene)
    scene.task("t-flow", kind="tooling", workflow=True)
    scene.down = True
    verdict = scene.admit(brief("t-flow"), agent_type="tooling")
    assert not verdict.allowed and "no place on this machine" in verdict.message


def test_the_report_counts_every_unit_when_github_cannot_be_read(
    scene: Scene, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    scene.unit("a-one")
    down: Remote = Down()  # type: ignore[assignment]
    code, out = run(scene, monkeypatch, capsys, remote=down)
    assert code == 0 and "places: 1 of 4 in use" in out and "could not be read from GitHub: a-one" in out
    assert "reimplementation: 1 in work" in out  # counted where it was before the Studio label


def test_the_report_reads_the_workflow_label_from_the_claim_card(
    scene: Scene, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    scene.unit("t-flow", kind="tooling")
    workflow_card(scene, "t-flow")
    code, out = run(scene, monkeypatch, capsys)
    assert code == 0 and "places: 0 of 4 in use" in out and "workflow units until the cutover): t-flow" in out


def test_the_bounded_gh_call_fails_with_an_oserror_when_it_takes_too_long(monkeypatch: pytest.MonkeyPatch) -> None:
    def slow(command: list[str], **options: object) -> object:
        assert options["timeout"] == task_cli.GH_CHECK_SECONDS
        raise subprocess.TimeoutExpired(command, task_cli.GH_CHECK_SECONDS)

    monkeypatch.setattr(subprocess, "run", slow)
    with pytest.raises(OSError, match="could not run"):
        task_cli._bounded_gh(["api", "x"])


# --- the claim check, in the order the guard asks ---------------------------------------------------------------------


def test_a_task_id_with_no_task_file_is_refused_before_the_claim_is_asked(scene: Scene) -> None:
    verdict = scene.admit(brief("made-up"))
    assert not verdict.allowed and "no task file for 'made-up'" in verdict.message
    assert scene.claim_asked == []


def test_a_task_file_with_a_released_claim_is_refused_and_keeps_nothing(scene: Scene) -> None:
    scene.task("c-three")
    scene.release("c-three")
    verdict = scene.admit(brief("c-three"), reserve=True)
    assert not verdict.allowed and "no claim to work under" in verdict.message and "task create" in verdict.message
    assert scene.claim_asked == ["c-three"] and scene.holders() == []


def test_a_held_claim_and_a_free_slot_let_the_start_through_and_reserve_it(scene: Scene) -> None:
    scene.task("c-three")
    verdict = scene.admit(brief("c-three"), reserve=True)
    assert verdict.allowed and scene.claim_asked == ["c-three"] and scene.holders() == ["c-three"]


def test_a_held_claim_does_not_open_a_full_slot(scene: Scene) -> None:
    full(scene)
    scene.task("d-four")
    verdict = scene.admit(brief("d-four"))
    assert not verdict.allowed and "no place on this machine" in verdict.message and scene.claim_asked[0] == "d-four"


def test_a_workflow_task_still_needs_its_claim(scene: Scene) -> None:
    scene.task("t-flow", kind="tooling", workflow=True)
    scene.release("t-flow")
    assert not scene.admit(brief("t-flow"), agent_type="tooling").allowed


def hang(scene: Scene, seconds: float = task_slots.GH_READ_SECONDS) -> Callable[[int], None]:
    """A GitHub read that takes `seconds` (the `gh` call's timeout by default) and then fails."""

    def read(number: int) -> None:
        scene.seconds += seconds
        raise ConfigError(f"gh timed out after {seconds:.0f} s")

    return read


def four_live_units(scene: Scene) -> None:
    """Three task units and a fix unit in work: every slot full, five issues to read with a new task's."""
    full(scene)
    scene.unit("f-fix", kind="fix")


def test_a_hanging_github_costs_one_read_not_one_per_issue(scene: Scene) -> None:
    four_live_units(scene)
    scene.task("d-four")
    scene.on_read = hang(scene)
    verdict = scene.admit(brief("d-four"), reserve=True)
    assert scene.reads == [scene.issues_of["d-four"][0]] and scene.seconds == task_slots.GH_READ_SECONDS
    assert not verdict.allowed and "no place on this machine" in verdict.message  # still counted


def test_a_hanging_github_with_a_free_slot_still_reserves_it(scene: Scene) -> None:
    scene.unit("a-one")
    scene.unit("b-two")
    scene.task("d-four")
    scene.on_read = hang(scene)
    verdict = scene.admit(brief("d-four"), reserve=True)
    assert len(scene.reads) == 1 and verdict.allowed and "not checked" in verdict.warning
    assert scene.holders() == ["a-one", "b-two", "d-four"]


def test_no_read_starts_once_the_budget_is_spent(scene: Scene) -> None:
    four_live_units(scene)
    scene.task("d-four")

    def slow(number: int) -> None:
        scene.seconds += 15.0  # each read succeeds, slowly

    scene.on_read = slow
    verdict = scene.admit(brief("d-four"))
    assert len(scene.reads) == 2  # at 0 s and at 15 s; the third would start at 30 s, over the 20 s budget
    assert scene.seconds <= task_slots.GH_BUDGET_SECONDS + task_slots.GH_READ_SECONDS
    assert not verdict.allowed and "Counted because GitHub could not say" in verdict.message


def test_the_admit_budget_adds_up() -> None:
    assert task_slots.ADMIT_MAX_SECONDS == (
        task_slots.GH_BUDGET_SECONDS + task_slots.GH_READ_SECONDS + task_slots.LOCK_WAIT_SECONDS
    )
    assert task_cli.GH_CHECK_SECONDS == task_slots.GH_READ_SECONDS


def test_github_is_never_read_while_the_slot_lock_is_held(scene: Scene) -> None:
    four_live_units(scene)
    scene.task("d-four")
    lock = scene.scratch / task_slots.LOCK_FILE

    def outside_the_lock(number: int) -> None:
        assert not lock.exists(), f"#{number} was read under the slot lock"

    scene.on_read = outside_the_lock
    scene.admit(brief("d-four"), reserve=True)
    assert len(scene.reads) == 5  # the task's issue and the four live units'


def test_a_unit_started_between_the_two_counts_is_counted_not_read(scene: Scene) -> None:
    scene.unit("a-one")
    scene.task("d-four")
    late: list[int] = []

    def start_one_late(number: int) -> None:
        if number == scene.issues_of["a-one"][0] and not late:  # the first count is reading the live units
            scene.unit("zz-late")
            late.extend(scene.issues_of["zz-late"])

    scene.on_read = start_one_late
    verdict = scene.admit(brief("d-four"), reserve=True)
    assert late and late[0] not in scene.reads
    assert verdict.allowed and "3 of 4 places on this machine in use" in verdict.message


def test_a_workflow_task_of_several_issues_keeps_the_claim_warning(scene: Scene) -> None:
    scene.task("t-flow", kind="tooling", workflow=True, issues=(40, 41))
    second = 41

    def second_fails(number: int) -> None:
        if number == second:
            raise ConfigError("gh timed out")

    scene.on_read = second_fails
    scene.cards[40] = replace(scene.cards[40], exempt=True)
    verdict = scene.admit(brief("t-flow"), agent_type="tooling")
    assert verdict.allowed and "workflow task" in verdict.message and "not checked" in verdict.warning


def test_a_second_agent_for_a_unit_that_has_not_started_yet_is_refused(scene: Scene) -> None:
    scene.task("c-three")
    assert scene.admit(brief("c-three"), reserve=True).allowed
    scene.clock.advance(minutes=3)
    verdict = scene.admit(brief("c-three"), reserve=True)
    assert not verdict.allowed and "has not run `task start` yet" in verdict.message
    assert "slot.json" in verdict.message
    assert scene.admit(brief("c-three")).allowed  # only a start is refused, a check is not


def test_the_second_agent_may_start_once_the_reservation_lapses(scene: Scene) -> None:
    scene.task("c-three")
    assert scene.admit(brief("c-three"), reserve=True).allowed
    scene.clock.advance(minutes=scene.conventions.tasks.start.slots.reserve_minutes + 1)
    assert scene.admit(brief("c-three"), reserve=True).allowed


def test_a_full_refusal_says_how_an_abandoned_unit_is_freed(scene: Scene) -> None:
    full(scene)
    scene.task("d-four")
    message = scene.admit(brief("d-four")).message
    assert "removing its worktree" in message and "workflow label" in message


# --- the lock under contention ----------------------------------------------------------------------------------------


def test_a_permission_error_from_a_lock_being_deleted_is_contention_not_a_crash(
    scene: Scene, monkeypatch: pytest.MonkeyPatch
) -> None:
    real_open = os.open
    failures = [PermissionError(13, "delete pending"), PermissionError(13, "delete pending")]

    def flaky(path: object, flags: int, *rest: int) -> int:
        if failures:
            raise failures.pop()
        return real_open(path, flags, *rest)  # type: ignore[arg-type]

    monkeypatch.setattr(os, "open", flaky)
    with task_slots.locked(scene.scratch, sleep=lambda _: None):
        assert (scene.scratch / task_slots.LOCK_FILE).exists()
    assert failures == []


def test_a_folder_that_cannot_be_written_fails_at_once_and_says_so(
    scene: Scene, monkeypatch: pytest.MonkeyPatch
) -> None:
    def denied(path: object, flags: int, *rest: int) -> int:
        raise PermissionError(13, "denied")

    monkeypatch.setattr(os, "open", denied)
    ticks = iter(float(tick) for tick in range(1000))  # one second a tick
    with (
        pytest.raises(ConfigError, match="permission denied") as raised,
        task_slots.locked(scene.scratch, clock=lambda: next(ticks), sleep=lambda _: None),
    ):
        pass
    assert "holds the slot lock" not in str(raised.value)
    assert next(ticks) < task_slots.LOCK_DENIED_SECONDS + 5  # it did not wait out LOCK_WAIT_SECONDS


def test_a_permission_error_while_a_lock_file_stands_is_a_held_lock(
    scene: Scene, monkeypatch: pytest.MonkeyPatch
) -> None:
    (scene.scratch / task_slots.LOCK_FILE).write_text("someone", encoding="utf-8")

    def denied(path: object, flags: int, *rest: int) -> int:
        raise PermissionError(13, "denied")

    monkeypatch.setattr(os, "open", denied)
    ticks = iter(float(tick) for tick in range(1000))
    with (
        pytest.raises(ConfigError, match="has held the slot lock"),
        task_slots.locked(scene.scratch, clock=lambda: next(ticks), sleep=lambda _: None),
    ):
        pass


def test_callers_that_lock_at_once_never_overlap(scene: Scene) -> None:
    state = {"inside": 0, "overlaps": 0}
    counter_lock = threading.Lock()

    def work() -> None:
        for _ in range(25):
            with task_slots.locked(scene.scratch):
                with counter_lock:
                    state["inside"] += 1
                    state["overlaps"] += state["inside"] > 1
                time.sleep(0.001)
                with counter_lock:
                    state["inside"] -= 1

    threads = [threading.Thread(target=work) for _ in range(6)]
    for thread in threads:
        thread.start()
    for thread in threads:
        thread.join()
    assert state["overlaps"] == 0 and not (scene.scratch / task_slots.LOCK_FILE).exists()


def test_admits_made_together_fill_the_slots_exactly(scene: Scene) -> None:
    names = [f"u-{number}" for number in range(8)]
    for name in names:
        scene.task(name)
    verdicts: dict[str, bool] = {}

    def start(name: str) -> None:
        verdicts[name] = scene.admit(brief(name), reserve=True).allowed

    threads = [threading.Thread(target=start, args=(name,)) for name in names]
    for thread in threads:
        thread.start()
    for thread in threads:
        thread.join()
    assert sum(verdicts.values()) == 3


def test_a_waiter_outlasts_the_stale_age_so_a_crashed_holder_costs_a_wait_not_a_refusal() -> None:
    assert task_slots.LOCK_WAIT_SECONDS > task_slots.LOCK_STALE_SECONDS


def test_a_fresh_lock_is_not_broken_as_stale(scene: Scene) -> None:
    lock = scene.scratch / task_slots.LOCK_FILE
    lock.write_text("owner", encoding="utf-8")
    task_slots._break_stale(lock)
    assert lock.read_text(encoding="utf-8") == "owner"


def test_a_stale_lock_is_broken(scene: Scene) -> None:
    lock = scene.scratch / task_slots.LOCK_FILE
    lock.write_text("crashed", encoding="utf-8")
    old = time.time() - task_slots.LOCK_STALE_SECONDS - 5
    os.utime(lock, (old, old))
    task_slots._break_stale(lock)
    assert not lock.exists() and list(scene.scratch.glob(task_slots.LOCK_FILE + "*")) == []


def test_a_lock_that_turned_fresh_before_the_rename_is_put_back(scene: Scene, monkeypatch: pytest.MonkeyPatch) -> None:
    lock = scene.scratch / task_slots.LOCK_FILE
    lock.write_text("new holder", encoding="utf-8")
    old = time.time() - task_slots.LOCK_STALE_SECONDS - 5
    os.utime(lock, (old, old))
    real_replace = Path.replace

    def renewed_first(self: Path, target: Path) -> Path:
        # Between the waiter's look and its rename another process broke the stale lock and took a fresh one.
        os.utime(self, None)
        return real_replace(self, target)

    monkeypatch.setattr(Path, "replace", renewed_first)
    task_slots._break_stale(lock)
    assert lock.read_text(encoding="utf-8") == "new holder" and list(scene.scratch.glob("*.stale-*")) == []


def test_a_put_back_that_loses_to_a_new_holder_leaves_no_file_and_two_holders(
    scene: Scene, monkeypatch: pytest.MonkeyPatch
) -> None:
    # The window `_break_stale` documents: the waiter renamed a fresh lock aside (its holder renewed it between the look
    # and the rename) and, before it could link it back, a third caller made a new lock. The put-back fails; the aside
    # file is removed all the same, and both holders keep going until each releases only its own nonce.
    lock = scene.scratch / task_slots.LOCK_FILE
    lock.write_text("displaced", encoding="utf-8")
    old = time.time() - task_slots.LOCK_STALE_SECONDS - 5
    os.utime(lock, (old, old))
    real_replace = Path.replace

    def renewed_first(self: Path, target: Path) -> Path:
        os.utime(self, None)
        return real_replace(self, target)

    def third_caller_first(source: object, target: object) -> None:
        Path(str(target)).write_text("third", encoding="utf-8")
        raise FileExistsError(17, "exists")

    monkeypatch.setattr(Path, "replace", renewed_first)
    monkeypatch.setattr(os, "link", third_caller_first)
    task_slots._break_stale(lock)
    assert lock.read_text(encoding="utf-8") == "third" and list(scene.scratch.glob("*.stale-*")) == []
    task_slots._release(lock, "displaced")
    assert lock.read_text(encoding="utf-8") == "third"  # the displaced holder does not remove the third's lock
    task_slots._release(lock, "third")
    assert not lock.exists()


def test_a_lock_is_released_only_by_its_owner(scene: Scene) -> None:
    lock = scene.scratch / task_slots.LOCK_FILE
    lock.write_text("someone else", encoding="utf-8")
    task_slots._release(lock, "mine")
    assert lock.exists()
    task_slots._release(lock, "someone else")
    assert not lock.exists()
