# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for what `workspace clean` leaves alone because a unit in work needs it (coney_tools.workspace_clean_units):
a worktree with no commits and no changes looks merged, so the unit record, its stamp log and its claim decide
(issue #1683). Real git in a temporary workspace, an in-memory GitHub, nothing waits."""

import shutil
import time
from collections import namedtuple
from dataclasses import replace
from pathlib import Path

import pytest
from audit_support import CONVENTIONS
from claim_support import Clock, FakeGitHub, plant_claim
from test_workspace_clean import OLD, Space

from coney_tools import pcsx2_proc, stale_processes, stamps, workspace_clean
from coney_tools.config import ConfigError
from coney_tools.issue_claim_remote import Comment, Remote
from coney_tools.task_file import TaskFile, render_task_file
from coney_tools.unit_record import UnitRecord, write_record
from coney_tools.workspace_clean import Entry, plan, run_clean
from coney_tools.workspace_clean_units import units_in_work

Usage = namedtuple("Usage", "total used free")
USER = "Ryubara"
WHO = {"kind": "tool", "tool": "test", "agent": {"id": "a", "type": "engineer", "model": "m"}, "source": "hook"}
TASK = "10-fix-the-fight-goal"


@pytest.fixture(autouse=True)
def machine(monkeypatch: pytest.MonkeyPatch) -> None:
    """No real processes are listed or stopped, and the drive has room."""
    monkeypatch.setattr(pcsx2_proc, "pid_alive", lambda pid: False)
    monkeypatch.setattr(stale_processes, "list_processes", lambda: [])
    monkeypatch.setattr(stale_processes, "kill_tree", lambda pid: None)
    monkeypatch.setattr(shutil, "disk_usage", lambda path: Usage(1000 * 1024**3, 0, 500 * 1024**3))


class Scene:
    """A workspace whose worktrees are units: scratch folders with task files, unit records and stamp logs, and the
    claim cards GitHub holds."""

    def __init__(self, tmp_path: Path) -> None:
        self.space = Space(tmp_path)
        self.scratch = tmp_path / "scratch"
        self.scratch.mkdir()
        self.clock = Clock()
        self.github = FakeGitHub(self.clock)
        #: The issues GitHub was asked about.
        self.asked: list[int] = []
        self.down = False
        real = self.github.comments

        def comments(number: int) -> list[Comment]:
            self.asked.append(number)
            if self.down:
                raise ConfigError("cannot read from GitHub with gh (offline)")
            return real(number)

        self.github.comments = comments  # type: ignore[method-assign]

    def unit(
        self,
        task: str = TASK,
        *steps: str,
        commits: int = 0,
        claim: bool = True,
        parent: str = "",
        age: float = OLD,
    ) -> Path:
        """The worktree of a unit of `task` (`commits` of its own, last used `age` hours ago) with its task file, its
        record and a stamp log of `steps` after `assigned` and `claimed`; `claim` holds the claim card of its issue."""
        issue = int(task.split("-", 1)[0])
        path = self.space.worktree(task, commits=commits, age=age)
        if issue not in self.github.items:
            self.github.add_issue(issue)
        if claim:
            plant_claim(self.github, issue, USER, task=parent or task)
        folder = self.scratch / task
        folder.mkdir(exist_ok=True)
        front = {
            "task": task,
            "issues": (issue,),
            "kind": "fix" if parent else "feature",
            "claimant": USER,
            "claimed_at": self.clock.now(),
            "scope": (),
            "terms": (),
            "parent": parent,
        }
        (folder / CONVENTIONS.tasks.file).write_text(render_task_file(TaskFile(**front)), encoding="utf-8")  # type: ignore[arg-type]
        write_record(
            folder / CONVENTIONS.tasks.start.unit_file,
            UnitRecord(
                task,
                "fix" if parent else "feature",
                (issue,),
                USER,
                task,
                path,
                "origin/main",
                "b" * 40,
                self.clock.now(),
                parent=parent,
            ),
        )
        for step in ("assigned", "claimed", *steps):
            self.stamp(parent or task, step, fix=task if parent and step == "claimed" else "")
        return path

    def stamp(self, log: str, step: str, fix: str = "") -> None:
        """Append `step` to the log of `log`, a `claimed` stamp of a fix naming it in `detail`."""
        if step == "assigned" and (self.scratch / log / "stamps").is_dir() and fix:
            return
        stamps.append(
            self.scratch / log / "stamps",
            rules=CONVENTIONS.stamps,
            task=log,
            step=step,
            subject="",
            result="pass",
            commit="b" * 40,
            covers={},
            detail={"fix": fix} if step == "claimed" else {},
            who=WHO,
            clock=self.clock.now,
        )
        self.clock.advance(seconds=1)

    def entries(self, remote: Remote | None = None) -> dict[str, Entry]:
        """The plan with the units read from this scene's scratch folder and GitHub."""
        settings = replace(
            self.space.settings(), in_work=units_in_work(self.scratch, CONVENTIONS, lambda: remote or self.github)
        )
        entries, _ = plan(settings)
        return {entry.name: entry for entry in entries}


@pytest.fixture
def scene(tmp_path: Path) -> Scene:
    return Scene(tmp_path)


def test_a_unit_with_no_commits_yet_is_kept_though_it_looks_merged(scene: Scene) -> None:
    scene.unit()
    entry = scene.entries()[TASK]
    assert not entry.remove
    assert "unit in work" in entry.why and TASK in entry.why and "task slots counts it" in entry.why
    assert not entry.merged
    assert scene.asked == []


def test_a_worktree_no_unit_names_is_still_removed_when_merged(scene: Scene) -> None:
    scene.space.worktree("loose")
    scene.unit()
    entries = scene.entries()
    assert entries["loose"].remove
    assert not entries[TASK].remove


def test_a_unit_that_passed_review_and_was_not_released_is_kept_while_its_claim_is_held(scene: Scene) -> None:
    scene.unit(TASK, "checked", "reviewed")
    entry = scene.entries()[TASK]
    assert not entry.remove
    assert "holds its claim" in entry.why and "no pr-opened stamp" in entry.why
    assert scene.asked == [10]


def test_a_unit_that_passed_review_with_its_claim_given_back_is_removed(scene: Scene) -> None:
    scene.unit(TASK, "checked", "reviewed", claim=False)
    assert scene.entries()[TASK].remove


def test_a_claim_held_for_another_task_is_not_this_units_claim(scene: Scene) -> None:
    scene.unit(TASK, "checked", "reviewed", claim=False)
    plant_claim(scene.github, 10, USER, task="10-another-task")
    assert scene.entries()[TASK].remove


def test_a_released_unit_is_removed_and_its_claim_is_not_asked(scene: Scene) -> None:
    scene.unit(TASK, "checked", "reviewed", "pr-opened")
    assert scene.entries()[TASK].remove
    assert scene.asked == []


def test_a_finished_unit_is_removed_without_asking_github(scene: Scene) -> None:
    scene.unit(TASK, "checked", "reviewed", "finished")
    assert scene.entries()[TASK].remove
    assert scene.asked == []


def test_a_claim_github_cannot_say_keeps_the_worktree_and_says_why(scene: Scene) -> None:
    scene.unit(TASK, "checked", "reviewed")
    scene.down = True
    entry = scene.entries()[TASK]
    assert not entry.remove
    assert "claim" in entry.why and "could not be read" in entry.why


def test_a_stamp_log_that_cannot_be_read_keeps_the_worktree(scene: Scene) -> None:
    scene.unit(TASK, "checked", "reviewed", "pr-opened")
    log = scene.scratch / TASK / "stamps"
    victim = log / "0001.jsonl"
    victim.write_text("not a stamp log\n", encoding="utf-8")
    entry = scene.entries()[TASK]
    assert not entry.remove
    assert "stamp log" in entry.why


def test_a_record_that_cannot_be_read_keeps_the_worktree_of_its_folder_name(scene: Scene) -> None:
    scene.unit()
    (scene.scratch / TASK / "unit.json").write_text("{", encoding="utf-8")
    entry = scene.entries()[TASK]
    assert not entry.remove
    assert "unit record" in entry.why


def test_a_unit_with_commits_of_its_own_is_kept_for_that_reason_first(scene: Scene) -> None:
    scene.unit(TASK, commits=1)
    entry = scene.entries()[TASK]
    assert not entry.remove
    assert "unmerged" in entry.why or "not in" in entry.why


def test_a_fix_in_work_keeps_the_worktree_of_the_unit_it_fixes(scene: Scene) -> None:
    path = scene.unit(TASK, "checked", "reviewed", "pr-opened")
    fix = "10-fix-the-fix"
    folder = scene.scratch / fix
    folder.mkdir()
    write_record(
        folder / CONVENTIONS.tasks.start.unit_file,
        UnitRecord(fix, "fix", (10,), USER, TASK, path, "origin/main", "b" * 40, scene.clock.now(), parent=TASK),
    )
    (folder / CONVENTIONS.tasks.file).write_text(
        render_task_file(
            TaskFile(
                task=fix,
                issues=(10,),
                kind="fix",
                claimant=USER,
                claimed_at=scene.clock.now(),
                scope=(),
                terms=(),
                parent=TASK,
            )
        ),
        encoding="utf-8",
    )
    scene.stamp(TASK, "claimed", fix=fix)
    entry = scene.entries()[TASK]
    assert not entry.remove
    assert fix in entry.why
    # Once the fix passed its review and its pull request was updated, nothing holds the worktree.
    scene.stamp(TASK, "reviewed")
    scene.stamp(TASK, "pr-opened")
    assert scene.entries()[TASK].remove


def test_the_units_are_read_once_per_run_and_each_claim_once(scene: Scene) -> None:
    scene.unit(TASK, "checked", "reviewed")
    scene.unit("11-other-task", "checked", "reviewed")
    in_work = units_in_work(scene.scratch, CONVENTIONS, lambda: scene.github)
    tree = scene.space.worktrees / TASK
    assert "holds its claim" in in_work(tree)
    assert "holds its claim" in in_work(tree)
    assert scene.asked == [10]


def test_without_conventions_a_worktree_a_record_names_is_kept(scene: Scene) -> None:
    scene.unit(TASK, "checked", "reviewed", "pr-opened")
    in_work = units_in_work(scene.scratch, None, lambda: scene.github)
    assert "conventions" in in_work(scene.space.worktrees / TASK)
    assert in_work(scene.space.worktrees / "unrelated") == ""


def test_clean_prints_why_it_keeps_the_worktree_and_apply_leaves_it(
    scene: Scene, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    path = scene.unit()
    scene.space.worktree("done")
    settings = replace(
        workspace_clean.default_settings(
            scene.space.root,
            scene.space.checkout,
            scene.space.checkout,
            now=time.time(),
            merged_heads=lambda branch: None,
        ),
        in_work=units_in_work(scene.scratch, CONVENTIONS, lambda: scene.github),
    )
    monkeypatch.setattr(workspace_clean, "default_settings", lambda *args, **kwargs: settings)
    assert run_clean(scene.space.checkout, scene.space.root, True) == 0
    said = capsys.readouterr().out
    assert path.is_dir() and not (scene.space.worktrees / "done").exists()
    assert f"keep         {TASK}: unit in work" in said
