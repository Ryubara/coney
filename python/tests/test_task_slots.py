# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for the engineer slots (task_slots, `task slots` in task_cli): which units hold a slot, what a brief must say
and when the coordinator's start of an agent is refused.

The scratch folder is a temporary one with task files, unit records and real stamp logs, as `task create` and
`task start` leave them; nothing touches GitHub, git or the network (docs/guides/agents.md#slots).
"""

import argparse
import io
import json
import os
import time
from dataclasses import replace
from datetime import datetime, timedelta
from pathlib import Path

import pytest
from audit_support import CONVENTIONS
from claim_support import Clock

from coney_tools import stamps, task_cli, task_slots
from coney_tools.config import ConfigError
from coney_tools.conventions import Conventions
from coney_tools.task_file import TaskFile, render_task_file
from coney_tools.unit_record import UnitRecord, write_record

RULES = CONVENTIONS.stamps
WHO = {"kind": "tool", "tool": "test", "agent": {"id": "a", "type": "engineer", "model": "m"}, "source": "hook"}
USER = "Ryubara"


class Scene:
    """A scratch folder with tasks in it, a clock and the committed conventions."""

    def __init__(self, tmp_path: Path) -> None:
        self.scratch = tmp_path / "scratch"
        self.scratch.mkdir()
        self.trees = tmp_path / "worktrees"
        self.clock = Clock()
        self.conventions: Conventions = CONVENTIONS

    def now(self) -> datetime:
        return self.clock.now()

    def task(self, name: str, kind: str = "feature", age_minutes: int = 0, **changes: object) -> Path:
        """A task file as `task create` writes it, claimed `age_minutes` ago."""
        values: dict[str, object] = {
            "task": name,
            "issues": (10,),
            "kind": kind,
            "claimant": USER,
            "claimed_at": self.now() - timedelta(minutes=age_minutes),
            "scope": (),
            "terms": (),
        }
        if kind == "fix":
            values["parent"] = "parent"
        values.update(changes)
        path = self.scratch / name / self.conventions.tasks.file
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(render_task_file(TaskFile(**values)), encoding="utf-8")  # type: ignore[arg-type]
        return path

    def unit(self, name: str, kind: str = "feature", end: str | None = None, tree: bool = True) -> None:
        """A started unit: task file, record, worktree folder and a log that ends at `end` (reviewed, finished)."""
        self.task(name, kind)
        tree_path = self.trees / name
        if tree:
            tree_path.mkdir(parents=True, exist_ok=True)
        write_record(
            self.scratch / name / "unit.json",
            UnitRecord(name, kind, (10,), USER, name, tree_path, "origin/main", "b" * 40, self.now()),
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

    def admit(self, brief: str, agent_type: str = "engineer", **options: object) -> task_slots.Verdict:
        return task_slots.admit(self.scratch, self.conventions, agent_type, brief, self.now(), **options)  # type: ignore[arg-type]

    def holders(self, slot: str = "task") -> list[str]:
        return [holder.task for holder in task_slots.scan(self.scratch, self.conventions, self.now()).held(slot)]


@pytest.fixture
def scene(tmp_path: Path) -> Scene:
    return Scene(tmp_path)


def brief(task: str) -> str:
    """A brief that names its unit."""
    return f"You are an engineer.\n\nTask id: {task}\nScope: src/ai/\n"


def full(scene: Scene) -> None:
    """Three task units in work, which is every task slot."""
    for name in ("a-one", "b-two", "c-three"):
        scene.unit(name)


# --- the numbers ----------------------------------------------------------------------------------------------------


def test_the_committed_slots_are_three_and_one_and_enforced() -> None:
    slots = CONVENTIONS.tasks.start.slots
    assert (slots.task, slots.fix, slots.enforce) == (3, 1, True)
    assert slots.exempt_kinds == ("tooling",) and slots.agent_types == ("engineer", "tooling")


# --- what holds a slot ----------------------------------------------------------------------------------------------


def test_a_started_unit_holds_a_task_slot_and_a_fix_holds_the_fix_slot(scene: Scene) -> None:
    scene.unit("a-one")
    scene.unit("b-fix", kind="fix")
    assert scene.holders("task") == ["a-one"] and scene.holders("fix") == ["b-fix"]


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


def test_a_tooling_unit_holds_no_slot_but_is_listed(scene: Scene) -> None:
    scene.unit("a-tools", kind="tooling")
    current = task_slots.scan(scene.scratch, scene.conventions, scene.now())
    assert current.holders == () and current.exempt == ("a-tools",)


def test_folders_that_are_not_units_are_ignored(scene: Scene) -> None:
    (scene.scratch / "pcsx2").mkdir()
    (scene.scratch / "notes.txt").write_text("x", encoding="utf-8")
    (scene.scratch / "broken").mkdir()
    (scene.scratch / "broken" / "unit.json").write_text("{", encoding="utf-8")
    assert scene.holders() == []


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
        "- Task id: 733-fix-it",
        "**Task id:** 733-fix-it",
        "task ID = 733-fix-it  (from task create)",
        "Intro\n\n> `Task id: 733-fix-it`\nmore",
    ],
)
def test_the_brief_names_its_task(text: str) -> None:
    assert task_slots.brief_task(text) == ("733-fix-it", None)


def test_the_brief_may_say_it_has_no_unit() -> None:
    assert task_slots.brief_task("Slot: none - survey of the AI pages, reads only") == (
        None,
        "survey of the AI pages, reads only",
    )
    assert task_slots.brief_task("Slot: none") == (None, None)  # a reason is needed
    assert task_slots.brief_task("Do the Task: it is hard") == (None, None)


# --- admit ----------------------------------------------------------------------------------------------------------


def test_a_free_slot_lets_the_start_through(scene: Scene) -> None:
    scene.unit("a-one")
    scene.task("d-four")
    verdict = scene.admit(brief("d-four"))
    assert verdict.allowed and "2 of 3 task slots" in verdict.message


def test_a_full_slot_refuses_the_start_and_names_the_busy_units(scene: Scene) -> None:
    full(scene)
    scene.task("d-four")
    verdict = scene.admit(brief("d-four"))
    assert not verdict.allowed
    for name in ("a-one", "b-two", "c-three"):
        assert name in verdict.message
    assert "3 of 3" in verdict.message and "passes Review" in verdict.message and "agents.md#slots" in verdict.message


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
    assert verdict.allowed and "holds a task slot already" in verdict.message


def test_the_fix_slot_is_counted_apart(scene: Scene) -> None:
    full(scene)
    scene.task("e-fix", kind="fix")
    assert scene.admit(brief("e-fix")).allowed  # the task slots are full, the fix slot is free
    scene.unit("f-fix", kind="fix")
    verdict = scene.admit(brief("e-fix"))
    assert not verdict.allowed and "no fix slot is free" in verdict.message and "f-fix" in verdict.message


def test_a_refused_fix_start_names_the_queue_oldest_first(scene: Scene) -> None:
    scene.unit("f-fix", kind="fix")
    scene.task("g-fix", kind="fix", age_minutes=10)
    scene.task("h-fix", kind="fix", age_minutes=60)
    verdict = scene.admit(brief("g-fix"))
    assert not verdict.allowed and "h-fix, g-fix" in verdict.message


def test_a_tooling_task_for_the_cutover_never_waits(scene: Scene) -> None:
    full(scene)
    scene.task("t-tools", kind="tooling")
    verdict = scene.admit(brief("t-tools"), agent_type="tooling")
    assert verdict.allowed and "holds no slot" in verdict.message


def test_the_exemption_ends_when_the_cutover_empties_the_list(scene: Scene) -> None:
    slots = replace(scene.conventions.tasks.start.slots, exempt_kinds=())
    start = replace(scene.conventions.tasks.start, slots=slots)
    scene.conventions = replace(scene.conventions, tasks=replace(scene.conventions.tasks, start=start))
    full(scene)
    scene.task("t-tools", kind="tooling")
    assert not scene.admit(brief("t-tools"), agent_type="tooling").allowed


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


def test_a_brief_without_a_unit_is_refused_with_the_two_ways_to_name_it(scene: Scene) -> None:
    verdict = scene.admit("Fix the doors.")
    assert not verdict.allowed and "Task id: <id>" in verdict.message and "Slot: none <reason>" in verdict.message


def test_a_brief_that_says_no_slot_with_a_reason_passes_when_full(scene: Scene) -> None:
    full(scene)
    verdict = scene.admit("Slot: none - Tether repository, no unit here")
    assert verdict.allowed and "Tether repository" in verdict.message


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


def test_a_tooling_start_reserves_nothing(scene: Scene) -> None:
    scene.task("t-tools", kind="tooling")
    assert scene.admit(brief("t-tools"), agent_type="tooling", reserve=True).allowed
    assert not (scene.scratch / "t-tools" / task_slots.RESERVATION_FILE).exists()


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
    scene: Scene, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str], *words: str, stdin: str = ""
) -> tuple[int, str]:
    """`coney-tools task slots ...` against the scene's scratch folder; (exit code, what it printed)."""
    monkeypatch.setattr(task_cli, "scratch_folder", lambda root: scene.scratch)
    monkeypatch.setattr(task_cli, "load_conventions", lambda root: scene.conventions)
    monkeypatch.setattr(task_cli, "find_repo_root", lambda start: scene.scratch)
    monkeypatch.setattr("sys.stdin", io.TextIOWrapper(io.BytesIO(stdin.encode("utf-8"))))
    group = argparse.ArgumentParser().add_subparsers(dest="group")
    task_cli.add_task_parser(group)
    args = group.choices["task"].parse_args(["slots", *words])
    code = task_cli.run_task(args, now=scene.now)
    return code, capsys.readouterr().out


def test_the_report_lists_both_slots(
    scene: Scene, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    scene.unit("a-one")
    scene.unit("b-fix", kind="fix")
    scene.unit("t-tools", kind="tooling")
    scene.task("c-fix", kind="fix")
    code, out = run(scene, monkeypatch, capsys)
    assert code == 0
    assert "task slots: 1 of 3 in use" in out and "fix slots: 1 of 1 in use" in out
    assert "a-one" in out and "b-fix" in out and "t-tools" in out and "c-fix" in out


def test_the_json_report(scene: Scene, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]) -> None:
    scene.unit("a-one")
    code, out = run(scene, monkeypatch, capsys, "--json")
    data = json.loads(out)
    assert code == 0 and data["enforce"] is True
    assert data["slots"]["task"]["limit"] == 3 and data["slots"]["task"]["holders"][0]["task"] == "a-one"


def test_admit_reads_the_brief_from_standard_input(
    scene: Scene, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    full(scene)
    scene.task("d-four")
    code, out = run(
        scene, monkeypatch, capsys, "--admit", "--agent-type=engineer", "--brief", "-", stdin=brief("d-four")
    )
    assert code == 1 and "no task slot is free" in out and "a-one" in out
    code, out = run(scene, monkeypatch, capsys, "--admit", "--task", "a-one")
    assert code == 0 and "holds a task slot already" in out


def test_admit_reserves_only_when_asked(
    scene: Scene, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    scene.task("d-four")
    assert run(scene, monkeypatch, capsys, "--admit", "--task", "d-four")[0] == 0
    assert not (scene.scratch / "d-four" / task_slots.RESERVATION_FILE).exists()
    assert run(scene, monkeypatch, capsys, "--admit", "--task", "d-four", "--reserve")[0] == 0
    assert (scene.scratch / "d-four" / task_slots.RESERVATION_FILE).is_file()
