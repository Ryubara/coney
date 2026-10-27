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
import subprocess
import threading
import time
from dataclasses import replace
from datetime import datetime, timedelta
from pathlib import Path

import pytest
from audit_support import CONVENTIONS
from claim_support import Clock, FakeGitHub, plant_claim

from coney_tools import stamps, task_cli, task_slots
from coney_tools.config import ConfigError
from coney_tools.conventions import Conventions
from coney_tools.issue_claim_remote import Remote
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
        #: What the claim check answers (None: the claim is held) and the tasks it was asked about.
        self.claim_result: str | None = None
        self.claim_asked: list[str] = []
        #: GitHub for the command tests: the claim cards `task create` would have left.
        self.github = FakeGitHub(self.clock)

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

    def claim_check(self, task: TaskFile) -> str | None:
        """The stand-in for the claim cards on GitHub."""
        self.claim_asked.append(task.task)
        return self.claim_result

    def admit(self, brief: str, agent_type: str = "engineer", **options: object) -> task_slots.Verdict:
        return task_slots.admit(
            self.scratch,
            self.conventions,
            agent_type,
            brief,
            self.now(),
            self.claim_check,
            **options,  # type: ignore[arg-type]
        )

    def holders(self, slot: str = "task") -> list[str]:
        return [holder.task for holder in task_slots.scan(self.scratch, self.conventions, self.now()).held(slot)]


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
    """Three task units in work, which is every task slot."""
    for name in ("a-one", "b-two", "c-three"):
        scene.unit(name)


# --- the numbers ----------------------------------------------------------------------------------------------------


def test_the_committed_slots_are_three_and_one_and_enforced() -> None:
    slots = CONVENTIONS.tasks.start.slots
    assert (slots.task, slots.fix, slots.enforce) == (3, 1, True)
    assert slots.exempt_workflow is True
    assert slots.agent_types == ("engineer", "tooling")


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


def test_a_workflow_unit_holds_no_slot_but_is_listed(scene: Scene) -> None:
    scene.unit("a-flow", kind="tooling", workflow=True)
    current = task_slots.scan(scene.scratch, scene.conventions, scene.now())
    assert current.holders == () and current.exempt == ("a-flow",)


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
    current = task_slots.scan(scene.scratch, scene.conventions, scene.now())
    assert [(h.task, h.state) for h in current.holders] == [("a-odd", "record unreadable")]


def test_worktrees_no_record_names_are_listed_as_unknown(scene: Scene) -> None:
    scene.unit("a-one")
    old = scene.trees / "old-branch"
    old.mkdir(parents=True)
    current = task_slots.scan(scene.scratch, scene.conventions, scene.now(), [scene.trees / "a-one", old])
    assert current.unknown_trees == (old,)
    lines = task_slots.report(current, scene.conventions)
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
    monkeypatch.setattr("sys.stdin", io.TextIOWrapper(io.BytesIO(stdin.encode("utf-8"))))
    group = argparse.ArgumentParser().add_subparsers(dest="group")
    task_cli.add_task_parser(group)
    args = group.choices["task"].parse_args(["slots", *words])
    code = task_cli.run_task(args, now=scene.now, remote=remote or scene.github)
    return code, capsys.readouterr().out


def claimed(scene: Scene, name: str, issue: int = 10, login: str = USER) -> None:
    """The claim `task create` made for `name`, held on GitHub now."""
    if issue not in scene.github.items:
        scene.github.add_issue(issue)
    plant_claim(scene.github, issue, login, task=name)


def test_the_report_lists_both_slots(
    scene: Scene, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    scene.unit("a-one")
    scene.unit("b-fix", kind="fix")
    scene.unit("t-flow", kind="tooling", workflow=True)
    scene.task("c-fix", kind="fix")
    code, out = run(scene, monkeypatch, capsys)
    assert code == 0
    assert "task slots: 1 of 3 in use" in out and "fix slots: 1 of 1 in use" in out
    assert "a-one" in out and "b-fix" in out and "t-flow" in out and "c-fix" in out


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
    scene.task("d-four", issues=(11,))
    claimed(scene, "d-four", 11)
    code, out = run(
        scene, monkeypatch, capsys, "--admit", "--agent-type=engineer", "--brief", "-", stdin=brief("d-four")
    )
    assert code == 3 and "no task slot is free" in out and "a-one" in out
    claimed(scene, "a-one", 10)
    code, out = run(scene, monkeypatch, capsys, "--admit", "--task", "a-one")
    assert code == 0 and "holds a task slot already" in out


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
    scene.task("d-four")
    scene.github.add_issue(10)  # the issue is there, the claim card is not
    code, out = run(scene, monkeypatch, capsys, "--admit", "--brief", "-", "--reserve", stdin=brief("d-four"))
    assert code == task_slots.REFUSED and "no claim to work under" in out and "not held now" in out
    assert not (scene.scratch / "d-four" / task_slots.RESERVATION_FILE).exists()


def test_a_claim_held_by_someone_else_or_for_another_task_is_refused(
    scene: Scene, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    scene.task("d-four")
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
    claimed(scene, "the-parent")
    assert run(scene, monkeypatch, capsys, "--admit", "--task", "e-fix")[0] == 0


def test_a_held_claim_with_a_full_slot_is_refused_for_the_slot(
    scene: Scene, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    full(scene)
    scene.task("d-four", issues=(11,))
    claimed(scene, "d-four", 11)
    code, out = run(scene, monkeypatch, capsys, "--admit", "--task", "d-four")
    assert code == 3 and "no task slot is free" in out and "no claim" not in out


def test_a_github_that_cannot_be_read_is_not_a_refusal(
    scene: Scene, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    class Down:
        def comments(self, number: int) -> list[object]:
            raise ConfigError("cannot read from GitHub with gh (offline)")

    scene.task("d-four")
    with pytest.raises(ConfigError, match="offline"):
        run(scene, monkeypatch, capsys, "--admit", "--task", "d-four", remote=Down())  # type: ignore[arg-type]


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
    scene.claim_result = "the claim on #10 is not held now (released, never made, or idle too long)"
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
    assert not verdict.allowed and "no task slot is free" in verdict.message and scene.claim_asked == ["d-four"]


def test_a_workflow_task_still_needs_its_claim(scene: Scene) -> None:
    scene.task("t-flow", kind="tooling", workflow=True)
    scene.claim_result = "the claim on #10 is not held now"
    assert not scene.admit(brief("t-flow"), agent_type="tooling").allowed


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


def test_a_permission_error_that_never_clears_gives_up_with_a_config_error(
    scene: Scene, monkeypatch: pytest.MonkeyPatch
) -> None:
    def denied(path: object, flags: int, *rest: int) -> int:
        raise PermissionError(13, "denied")

    monkeypatch.setattr(os, "open", denied)
    ticks = iter(range(0, 1000, 10))
    with (
        pytest.raises(ConfigError),
        task_slots.locked(scene.scratch, clock=lambda: float(next(ticks)), sleep=lambda _: None),
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


def test_a_lock_is_released_only_by_its_owner(scene: Scene) -> None:
    lock = scene.scratch / task_slots.LOCK_FILE
    lock.write_text("someone else", encoding="utf-8")
    task_slots._release(lock, "mine")
    assert lock.exists()
    task_slots._release(lock, "someone else")
    assert not lock.exists()
