# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for `task start` (task_start, task_cli): real git in a temporary workspace with a bare remote, an in-memory
GitHub with real claim cards, and a clock nobody waits on.

The workspace is the layout of docs/guides/workspace.md: `<ws>/repos/coney` is the main checkout, units go to
`<ws>/worktrees/coney/<task>/` and `<ws>/scratch/<task>/` holds the task file, the unit record and the stamp log.
"""

import argparse
import json
import shutil
import subprocess
from dataclasses import replace
from pathlib import Path

import pytest
from audit_support import CONVENTIONS
from claim_support import Clock, FakeGitHub, plant_claim
from conventions_support import REPO
from git_support import git

from coney_tools import stamps, task_cli, task_start, unit
from coney_tools.config import ConfigError
from coney_tools.conventions import Conventions
from coney_tools.issue_claim_model import Card, parse_time, render_card
from coney_tools.task_file import TaskFile, parse_task_file, render_task_file
from coney_tools.task_start import StartEnv, StartRequest, start_task
from coney_tools.unit_record import UnitRecord, read_record, write_record

TASK = "10-fix-the-fight-goal"
USER = "Ryubara"
AGENT = {"id": "agent-7", "type": "engineer", "model": "Claude Sonnet 5.5"}
WHO = {"kind": "tool", "tool": "coney-tools task start", "agent": AGENT, "source": "hook"}
RULES = CONVENTIONS.stamps


class World:
    """A workspace with a main checkout and a bare origin, GitHub with one claimed issue, and a clock."""

    def __init__(self, tmp_path: Path) -> None:
        self.tmp = tmp_path
        self.ws = tmp_path / "ws"
        self.main = self.ws / "repos" / "coney"
        self.scratch = self.ws / "scratch"
        self.origin = tmp_path / "origin.git"
        self.origin.mkdir()
        git(self.origin, "init", "-q", "--bare", "-b", "main")
        self.main.mkdir(parents=True)
        git(self.main, "init", "-q", "-b", "main")
        (self.main / "coney.local.example.toml").write_text("", encoding="utf-8")
        (self.main / ".github").mkdir()
        shutil.copy(REPO / ".github" / "conventions.yml", self.main / ".github" / "conventions.yml")
        (self.main / "README.md").write_text("Coney\n", encoding="utf-8")
        git(self.main, "add", "-A")
        git(self.main, "commit", "-q", "-m", "base")
        git(self.main, "remote", "add", "origin", str(self.origin))
        git(self.main, "push", "-q", "origin", "main")
        self.clock = Clock()
        self.github = FakeGitHub(self.clock)
        self.github.add_issue(10, title="gameplay: Fix the fight goal")
        self.conventions: Conventions = CONVENTIONS
        self.output: list[str] = []
        self.posted: list[tuple[int, str]] = []
        original = self.github.post_command

        def record(number: int, body: str) -> int:
            self.posted.append((number, body))
            return original(number, body)

        self.github.post_command = record  # type: ignore[method-assign]

    # -- the scene

    def claim(self, issue: int = 10, task: str = TASK, login: str = USER) -> None:
        """A held claim, as `task create` leaves it."""
        if issue not in self.github.items:
            self.github.add_issue(issue)
        plant_claim(self.github, issue, login, task=task)

    def task_file(self, task: str = TASK, **changes: object) -> Path:
        """Write the task file of `task` the way `task create` does; returns its path."""
        values: dict[str, object] = {
            "task": task,
            "issues": (10,),
            "kind": "feature",
            "claimant": USER,
            "claimed_at": self.clock.now(),
            "scope": ("src/ai/",),
            "terms": (),
        }
        values.update(changes)
        path = self.scratch / task / self.conventions.tasks.file
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(render_task_file(TaskFile(**values)), encoding="utf-8")  # type: ignore[arg-type]
        return path

    def ready(self, task: str = TASK, issues: tuple[int, ...] = (10,), **changes: object) -> None:
        """A claimed task with its task file: what `task start` needs."""
        for number in issues:
            self.claim(number, task)
        self.task_file(task, issues=issues, **changes)

    def push_to_origin(self, name: str = "later.txt") -> str:
        """Another person lands a commit on origin/main; returns its hash."""
        other = self.tmp / "other"
        if not other.exists():
            subprocess.run(["git", "clone", "-q", str(self.origin), str(other)], check=True, capture_output=True)
        (other / name).write_text("more\n", encoding="utf-8")
        git(other, "add", "-A")
        git(other, "commit", "-q", "-m", "later")
        git(other, "push", "-q", "origin", "main")
        return git(other, "rev-parse", "HEAD").strip()

    # -- running

    def env(self) -> StartEnv:
        """The environment of `start_task`."""
        return StartEnv(
            root=self.main,
            remote=self.github,
            conventions=self.conventions,
            scratch=self.scratch,
            now=self.clock.now,
            who=lambda: dict(WHO),
            out=self.output.append,
        )

    def run(self, task: str = TASK, **request: object) -> int:
        """Run `start_task`."""
        return start_task(StartRequest(task=task, **request), self.env())  # type: ignore[arg-type]

    def said(self) -> str:
        """Everything printed."""
        return "\n".join(self.output)

    def worktree(self, task: str = TASK) -> Path:
        """Where the unit's worktree goes."""
        return self.ws / "worktrees" / "coney" / task

    def record(self, task: str = TASK) -> UnitRecord:
        """The unit record of a task."""
        return read_record(self.scratch / task / self.conventions.tasks.start.unit_file)

    def log(self, task: str = TASK) -> stamps.Report:
        """The stamp log of a task."""
        return stamps.verify(self.scratch / task / "stamps", RULES)

    def branches(self) -> list[str]:
        """The local branches."""
        return git(self.main, "branch", "--format=%(refname:short)").split()


@pytest.fixture
def world(tmp_path: Path) -> World:
    return World(tmp_path)


# --- the unit: branch, worktree, scratch ----------------------------------------------------------------------------
def test_a_started_task_has_its_branch_and_worktree_in_the_workspace_layout(world: World) -> None:
    world.ready()
    assert world.run() == 0
    tree = world.worktree()
    assert tree.is_dir() and (tree / "README.md").is_file()
    # The layout is the one the conventions name (workspace.worktrees), not a second copy of it.
    template = world.conventions.workspace.worktrees.replace("<branch>", TASK)
    assert (world.main / template).resolve() == tree.resolve()
    assert TASK in world.branches()
    assert git(tree, "rev-parse", "--abbrev-ref", "HEAD").strip() == TASK
    assert git(tree, "status", "--porcelain").strip() == ""
    # The branch does not track origin/main: a push of it is the release's business, not git status noise.
    assert "[origin" not in git(tree, "status", "-sb")


def test_the_branch_starts_at_the_tip_of_the_default_branch_after_a_fetch(world: World) -> None:
    world.ready()
    tip = world.push_to_origin()
    assert world.run() == 0
    assert git(world.worktree(), "rev-parse", "HEAD").strip() == tip
    record = world.record()
    assert (record.base_ref, record.base_commit) == ("origin/main", tip)


def test_no_fetch_starts_from_the_ref_as_it_is(world: World) -> None:
    world.ready()
    before = git(world.main, "rev-parse", "origin/main").strip()
    world.push_to_origin()
    assert world.run(fetch=False) == 0
    assert git(world.worktree(), "rev-parse", "HEAD").strip() == before


def test_a_base_and_a_remote_can_be_named(world: World) -> None:
    world.ready()
    git(world.main, "branch", "release", "main")
    assert world.run(base="release", fetch=False) == 0
    assert world.record().base_ref == "release"
    world.ready(task="11-other", issues=(11,))
    with pytest.raises(ConfigError, match=r"git fetch nowhere main.*--no-fetch"):
        world.run("11-other", remote="nowhere")
    assert not world.worktree("11-other").exists() and "11-other" not in world.branches()
    assert not (world.scratch / "11-other" / "unit.json").exists()


def test_a_base_that_is_not_a_commit_is_refused_before_anything_is_made(world: World) -> None:
    world.ready()
    with pytest.raises(ConfigError, match="no-such-ref"):
        world.run(base="no-such-ref", fetch=False)
    assert not world.worktree().exists() and TASK not in world.branches() and not world.posted


def test_the_unit_record_says_where_the_work_is(world: World) -> None:
    world.ready()
    assert world.run() == 0
    record = world.record()
    assert (record.task, record.kind, record.issues, record.claimant) == (TASK, "feature", (10,), USER)
    assert record.branch == TASK and record.worktree.resolve() == world.worktree().resolve()
    assert record.base_commit == git(world.main, "rev-parse", "origin/main").strip()
    assert record.started == world.clock.now() and record.parent == "" and record.progress_posted is True
    assert record.log_task == TASK


# --- the stamps -----------------------------------------------------------------------------------------------------
def test_assigned_then_claimed_are_the_first_stamps_of_the_log(world: World) -> None:
    world.ready(issues=(10, 11), scope=("src/ai/", "0x00322b78"))
    assert world.run() == 0
    report = world.log()
    assert report.ok and [s.step for s in report.stamps] == ["assigned", "claimed"]
    assigned, claimed = report.stamps
    base = git(world.main, "rev-parse", "origin/main").strip()
    assert assigned.task == claimed.task == TASK and assigned.result == claimed.result == "pass"
    assert assigned.detail == {
        "issues": [10, 11],
        "kind": "feature",
        "claimant": USER,
        "claimed_at": "2026-10-22T12:00:00Z",
    }
    assert claimed.commit == assigned.commit == base and claimed.covers == {"base": base}
    assert claimed.detail == {"branch": TASK, "base_ref": "origin/main", "kind": "feature", "fix": ""}
    assert claimed.who["agent"] == AGENT and claimed.who["source"] == "hook"


def test_the_assigned_stamp_covers_the_task_file_but_not_its_prose(world: World) -> None:
    path = world.task_file(sections={"Goal": "one"})
    world.claim()
    assert world.run() == 0
    covered = world.log().stamps[0].covers["task_sha256"]
    parsed = parse_task_file(path.read_text(encoding="utf-8"), world.conventions.tasks)
    assert task_start.front_sha256(parsed) == covered
    prose = replace(parsed, sections={**parsed.sections, "Goal": "something else entirely"})
    assert task_start.front_sha256(prose) == covered
    assert task_start.front_sha256(replace(parsed, scope=("src/other/",))) != covered


def test_every_issue_of_the_task_gets_progress_claimed(world: World) -> None:
    world.ready(issues=(10, 11))
    assert world.run() == 0
    assert world.posted == [(10, "/progress claimed"), (11, "/progress claimed")]


# --- what must be true before anything is made ----------------------------------------------------------------------
def untouched(world: World, task: str = TASK) -> bool:
    """Whether a refused start left no branch, worktree, record, stamp or comment."""
    return (
        not world.worktree(task).exists()
        and task not in world.branches()
        and not (world.scratch / task / world.conventions.tasks.start.unit_file).exists()
        and not (world.scratch / task / "stamps").exists()
        and not world.posted
    )


def test_a_task_needs_its_task_file(world: World) -> None:
    assert world.run() == 1
    assert "no task file" in world.said() and "task create" in world.said() and untouched(world)


def test_the_task_file_must_belong_to_the_task(world: World) -> None:
    world.claim()
    world.task_file().write_text(
        render_task_file(
            TaskFile(task="other", issues=(10,), kind="feature", claimant=USER, claimed_at=world.clock.now())
        ),
        encoding="utf-8",
    )
    assert world.run() == 1 and "is the task file of 'other'" in world.said() and untouched(world)


def test_a_damaged_task_file_stops_the_command(world: World) -> None:
    world.task_file().write_text("nothing", encoding="utf-8")
    with pytest.raises(ConfigError, match="does not start with a --- block"):
        world.run()
    assert untouched(world)


def test_the_claim_must_still_be_held_by_the_person_starting(world: World) -> None:
    world.task_file()
    assert world.run() == 1 and "#10: no claim card" in world.said() and untouched(world)
    world.output.clear()
    world.claim(login="alice")
    assert world.run() == 1 and "#10 is claimed by alice, not Ryubara" in world.said() and untouched(world)


def test_a_task_file_of_another_account_is_refused(world: World) -> None:
    world.ready(claimant="alice")
    assert world.run() == 1 and "task file belongs to alice" in world.said() and untouched(world)


def test_the_claim_must_be_for_this_task(world: World) -> None:
    world.claim(task="10-another-name")
    world.task_file()
    assert world.run() == 1 and "was made for the task '10-another-name'" in world.said() and untouched(world)


def test_a_released_claim_is_not_a_claim(world: World) -> None:
    world.task_file()
    released = Card(issue=10, claimant=USER, claimed_at=world.clock.now(), task=TASK, status="released")
    world.github.create_comment(10, render_card(released, world.conventions.claims))
    assert world.run() == 1 and "no claim card" in world.said() and untouched(world)


def test_a_closed_issue_cannot_be_started(world: World) -> None:
    world.ready()
    world.github.items[10].view = replace(world.github.view(10), state="closed")
    assert world.run() == 1 and "#10 is closed" in world.said() and untouched(world)


def test_an_existing_branch_or_folder_is_never_overwritten(world: World) -> None:
    world.ready()
    git(world.main, "branch", TASK, "main")
    assert world.run() == 1 and f"branch '{TASK}' exists already" in world.said()
    assert not world.worktree().exists() and not world.posted
    git(world.main, "branch", "-q", "-D", TASK)
    world.output.clear()
    world.worktree().mkdir(parents=True)
    assert world.run() == 1 and "exists already" in world.said() and not world.posted
    assert TASK not in world.branches()


def test_a_dry_run_makes_and_posts_nothing(world: World) -> None:
    world.ready()
    assert world.run(dry_run=True) == 0
    assert untouched(world)
    said = world.said()
    assert "dry run" in said and TASK in said and "origin/main" in said and str(world.worktree().name) in said


# --- the limits -----------------------------------------------------------------------------------------------------
def test_a_claimant_over_the_limit_of_open_claims_cannot_start(world: World) -> None:
    for number, task in ((20, "a"), (21, "b"), (22, "c"), (23, "d")):
        world.claim(number, task)
    world.ready()  # a fifth open claim: more than the 3 a maintainer may hold
    assert world.run() == 1
    assert "holds 5 open claim(s)" in world.said() and "3 at most for a maintainer" in world.said()
    assert untouched(world)


def test_a_claimant_at_the_limit_can_start(world: World) -> None:
    world.claim(20, "a")
    world.claim(21, "b")
    world.ready()
    assert world.run() == 0


def test_a_contributor_has_the_smaller_limit(world: World) -> None:
    world.github.permissions[USER] = "read"
    world.github.association_of_actor = "NONE"
    world.claim(20, "a")
    world.claim(21, "b")
    world.ready()
    assert world.run() == 1 and "2 at most for a contributor" in world.said()


def test_issues_of_one_task_are_one_claim(world: World) -> None:
    world.claim(20, "a")
    world.claim(21, "b")
    world.ready(issues=(10, 11, 12))
    assert world.run() == 0


# --- starting twice -------------------------------------------------------------------------------------------------
def test_starting_a_started_task_changes_nothing(world: World) -> None:
    world.ready()
    assert world.run() == 0
    tip = git(world.worktree(), "rev-parse", "HEAD").strip()
    stamp_count = len(world.log().stamps)
    posted = len(world.posted)
    world.output.clear()
    world.push_to_origin()
    assert world.run() == 0
    assert "already started" in world.said() and str(world.worktree().name) in world.said()
    assert git(world.worktree(), "rev-parse", "HEAD").strip() == tip
    assert len(world.log().stamps) == stamp_count and len(world.posted) == posted


def test_it_can_be_run_from_inside_a_worktree_too(world: World) -> None:
    world.ready()
    world.ready(task="11-other", issues=(11,))
    assert world.run() == 0
    env = world.env()
    env.root = world.worktree()
    assert start_task(StartRequest(task="11-other", fetch=False), env) == 0
    # The new unit is made beside the others, not inside the worktree it was started from.
    assert world.worktree("11-other").is_dir() and not (world.worktree() / "11-other").exists()


def test_a_start_that_stopped_after_the_worktree_is_finished_by_a_rerun(
    world: World, monkeypatch: pytest.MonkeyPatch
) -> None:
    world.ready()
    real = stamps.append_many
    calls = {"n": 0}

    def broken(*args: object, **kwargs: object) -> list[stamps.Stamp]:
        calls["n"] += 1
        if calls["n"] == 1:
            raise stamps.StampError("disk full")
        return real(*args, **kwargs)  # type: ignore[arg-type]

    monkeypatch.setattr(stamps, "append_many", broken)
    with pytest.raises(stamps.StampError, match="disk full"):
        world.run()
    # The unit exists, with no stamps and no progress yet.
    assert world.worktree().is_dir() and world.record().progress_posted is False and not world.posted
    assert world.run() == 0
    assert [s.step for s in world.log().stamps] == ["assigned", "claimed"]
    assert world.posted == [(10, "/progress claimed")] and world.record().progress_posted is True


def test_a_progress_comment_that_failed_is_posted_by_the_rerun(world: World) -> None:
    world.ready()
    failing = {"on": True}
    original = world.github.post_command

    def flaky(number: int, body: str) -> int:
        if failing["on"]:
            raise ConfigError("cannot write to GitHub with gh (gh api failed: boom)")
        return original(number, body)

    world.github.post_command = flaky  # type: ignore[method-assign]
    assert world.run() == 0
    assert "could not post /progress claimed on #10" in world.said() and world.record().progress_posted is False
    failing["on"] = False
    world.output.clear()
    assert world.run() == 0
    assert world.record().progress_posted is True and len(world.log().stamps) == 2


def test_a_record_whose_worktree_is_gone_is_not_silently_remade(world: World) -> None:
    world.ready()
    assert world.run() == 0
    git(world.main, "worktree", "remove", "--force", str(world.worktree()))
    world.output.clear()
    assert world.run() == 1
    assert "worktree is gone" in world.said()
    assert str(world.scratch / TASK / "unit.json").replace("\\", "/") in world.said().replace("\\", "/")


def not_made(world: World, task: str = TASK) -> bool:
    """Whether no branch, worktree or unit record was made (a log the test planted may be there)."""
    return (
        not world.worktree(task).exists()
        and task not in world.branches()
        and not (world.scratch / task / world.conventions.tasks.start.unit_file).exists()
        and not world.posted
    )


def test_a_dry_run_of_a_started_task_finishes_nothing(world: World, monkeypatch: pytest.MonkeyPatch) -> None:
    world.ready()
    real = stamps.append_many
    calls = {"n": 0}

    def broken(*args: object, **kwargs: object) -> list[stamps.Stamp]:
        calls["n"] += 1
        if calls["n"] == 1:
            raise stamps.StampError("disk full")
        return real(*args, **kwargs)  # type: ignore[arg-type]

    monkeypatch.setattr(stamps, "append_many", broken)
    with pytest.raises(stamps.StampError):
        world.run()
    world.output.clear()
    # The unit has no stamps and no progress comment yet; a dry run says so and does neither.
    assert world.run(dry_run=True) == 0
    assert not (world.scratch / TASK / "stamps").exists() and not world.posted
    assert world.record().progress_posted is False
    said = world.said()
    assert "dry run" in said and "assigned" in said and "claimed" in said and "/progress claimed" in said
    assert calls["n"] == 1
    assert world.run() == 0 and [s.step for s in world.log().stamps] == ["assigned", "claimed"]
    assert world.posted == [(10, "/progress claimed")]


def test_a_dry_run_of_a_complete_unit_has_nothing_to_do(world: World) -> None:
    world.ready()
    assert world.run() == 0
    posted = len(world.posted)
    world.output.clear()
    assert world.run(dry_run=True) == 0
    assert "nothing to do" in world.said() and len(world.posted) == posted and len(world.log().stamps) == 2


def plant_bad_log(world: World, task: str = TASK) -> None:
    """A stamp log that cannot be trusted: a line that is not a stamp."""
    folder = world.scratch / task / "stamps"
    folder.mkdir(parents=True)
    (folder / "0001.jsonl").write_text("not a stamp\n", encoding="utf-8")


@pytest.mark.parametrize("dry_run", [False, True])
def test_a_log_that_cannot_be_trusted_stops_the_start_before_anything_is_made(world: World, dry_run: bool) -> None:
    world.ready()
    plant_bad_log(world)
    assert world.run(dry_run=dry_run) == 1
    assert "the stamp log cannot be used" in world.said() and "not valid JSON" in world.said()
    assert not_made(world)


def test_the_log_of_another_task_stops_the_start(world: World) -> None:
    world.ready()
    plant_unit(world, "30-other")
    shutil.copytree(world.scratch / "30-other" / "stamps", world.scratch / TASK / "stamps")
    assert world.run() == 1 and "the stamp log cannot be used" in world.said() and "30-other" in world.said()
    assert not_made(world)


def test_a_rerun_with_a_damaged_log_is_refused_not_raised(world: World) -> None:
    world.ready()
    assert world.run() == 0
    segment = world.scratch / TASK / "stamps" / "0001.jsonl"
    segment.write_text(segment.read_text(encoding="utf-8").replace('"seq":1', '"seq":9'), encoding="utf-8")
    world.output.clear()
    assert world.run() == 1 and "the stamp log cannot be used" in world.said()


def test_a_fix_whose_parents_log_is_damaged_is_refused_not_raised(world: World) -> None:
    started_parent(world)
    fix_file(world)
    segment = world.scratch / TASK / "stamps" / "0001.jsonl"
    segment.write_text(segment.read_text(encoding="utf-8").replace('"seq":1', '"seq":9'), encoding="utf-8")
    assert world.run(FIX) == 1 and "the stamp log cannot be used" in world.said()
    assert not (world.scratch / FIX / "unit.json").exists()


def test_the_worktree_goes_where_the_workspace_rule_says(world: World) -> None:
    workspace = replace(world.conventions.workspace, worktrees="../../trees/<branch>/")
    world.conventions = replace(world.conventions, workspace=workspace)
    world.ready()
    assert world.run() == 0
    assert (world.ws / "trees" / TASK / "README.md").is_file() and not world.worktree().exists()
    assert world.record().worktree.resolve() == (world.ws / "trees" / TASK).resolve()


def test_a_start_the_hook_did_not_vouch_for_warns_that_the_stamps_will_say_so(world: World) -> None:
    world.ready()
    env = world.env()
    env.who = lambda: {
        "kind": "tool",
        "tool": "coney-tools task start",
        "agent": {"id": "unknown", "type": "unknown", "model": "unknown"},
        "source": "unverified",
    }
    assert start_task(StartRequest(task=TASK), env) == 0
    assert "warning" in world.said() and "unverified" in world.said() and "coney-agent" in world.said()
    assert world.log().stamps[0].who["source"] == "unverified"


def test_a_vouched_start_has_no_such_warning(world: World) -> None:
    world.ready()
    assert world.run() == 0
    assert "unverified" not in world.said()


def test_a_claim_made_without_a_task_id_says_so(world: World) -> None:
    plant_claim(world.github, 10, USER, task=None)
    world.task_file()
    assert world.run() == 1
    assert "has no task id" in world.said() and "None" not in world.said() and untouched(world)


# --- slots ----------------------------------------------------------------------------------------------------------


def plant_unit(world: World, task: str, kind: str = "feature", end: str | None = None, tree: bool = True) -> None:
    """Another unit of this machine: a record, a worktree folder and a log that ends at `end` (reviewed, finished)."""
    folder = world.scratch / task
    tree_path = world.worktree(task)
    if tree:
        tree_path.mkdir(parents=True)
    write_record(
        folder / "unit.json",
        UnitRecord(task, kind, (30,), USER, task, tree_path, "origin/main", "b" * 40, world.clock.now()),
    )
    steps = ["assigned", "claimed", *([end] if end else [])]
    for step in steps:
        stamps.append(
            folder / "stamps",
            rules=RULES,
            task=task,
            step=step,
            subject="",
            result="pass",
            commit="b" * 40,
            covers={},
            detail={},
            who=WHO,
            clock=world.clock.now,
        )


def test_the_slots_in_use_are_reported(world: World) -> None:
    plant_unit(world, "30-a")
    plant_unit(world, "31-b")
    world.ready()
    assert world.run() == 0
    assert "slots: 3 of 3 task slots in use" in world.said()


def test_over_the_slot_limit_is_only_reported_before_the_cutover(world: World) -> None:
    for number in range(30, 33):
        plant_unit(world, f"{number}-x")
    world.ready()
    assert world.run() == 0
    assert "slots: 4 of 3 task slots in use" in world.said() and "not enforced" in world.said()


def test_over_the_slot_limit_is_refused_once_it_is_enforced(world: World) -> None:
    start = replace(world.conventions.tasks.start, slots=replace(world.conventions.tasks.start.slots, enforce=True))
    world.conventions = replace(world.conventions, tasks=replace(world.conventions.tasks, start=start))
    for number in range(30, 33):
        plant_unit(world, f"{number}-x")
    world.ready()
    assert world.run() == 1
    assert "3 task units are in work" in world.said() and "tasks.start.slots" in world.said() and untouched(world)


def test_a_unit_that_passed_review_finished_or_lost_its_worktree_holds_no_slot(world: World) -> None:
    plant_unit(world, "30-reviewed", end="reviewed")
    plant_unit(world, "31-finished", end="finished")
    plant_unit(world, "32-gone", tree=False)
    plant_unit(world, "33-working")
    world.ready()
    assert world.run() == 0
    assert "slots: 2 of 3 task slots in use" in world.said()


def test_a_unit_with_a_damaged_log_keeps_its_slot(world: World) -> None:
    plant_unit(world, "30-odd")
    segment = world.scratch / "30-odd" / "stamps" / "0001.jsonl"
    segment.write_text(segment.read_text(encoding="utf-8").replace('"seq":1', '"seq":9'), encoding="utf-8")
    world.ready()
    assert world.run() == 0 and "slots: 2 of 3 task slots in use" in world.said()


def test_a_folder_that_is_not_a_unit_is_ignored(world: World) -> None:
    (world.scratch / "pcsx2").mkdir(parents=True)
    (world.scratch / "30-odd").mkdir()
    (world.scratch / "30-odd" / "unit.json").write_text("{", encoding="utf-8")
    world.ready()
    assert world.run() == 0 and "slots: 1 of 3 task slots in use" in world.said()


# --- a fix task -----------------------------------------------------------------------------------------------------
FIX = "10-fix"


def started_parent(world: World, released: bool = True) -> None:
    """The ordinary task started and, when `released`, with a `pr-opened` stamp in its log."""
    world.ready()
    assert world.run() == 0
    if released:
        stamps.append(
            world.scratch / TASK / "stamps",
            rules=RULES,
            task=TASK,
            step="pr-opened",
            subject="",
            result="pass",
            commit=git(world.worktree(), "rev-parse", "HEAD").strip(),
            covers={},
            detail={},
            who=WHO,
            clock=world.clock.now,
        )
    world.output.clear()
    world.posted.clear()


def fix_file(world: World, **changes: object) -> None:
    """The task file `task create --fix-of 10` wrote."""
    values: dict[str, object] = {"kind": "fix", "parent": TASK}
    values.update(changes)
    world.task_file(FIX, **values)


def test_a_fix_task_works_in_its_parents_unit_and_log(world: World) -> None:
    started_parent(world)
    fix_file(world)
    branches = world.branches()
    assert world.run(FIX) == 0
    # No new branch or worktree: the fix has a record that points at the parent's.
    assert world.branches() == branches and not world.worktree(FIX).exists()
    record = world.record(FIX)
    parent = world.record(TASK)
    assert (record.kind, record.parent, record.branch, record.worktree) == ("fix", TASK, TASK, parent.worktree)
    assert record.log_task == TASK and record.base_commit == parent.base_commit
    # Its claimed stamp joins the parent's log and says which fix it is.
    report = world.log(TASK)
    assert [s.step for s in report.stamps] == ["assigned", "claimed", "pr-opened", "claimed"]
    assert report.stamps[-1].detail == {"branch": TASK, "base_ref": "origin/main", "kind": "fix", "fix": FIX}
    assert report.stamps[-1].commit == git(world.worktree(), "rev-parse", "HEAD").strip()
    assert not (world.scratch / FIX / "stamps").exists()
    assert world.posted == []  # `task create --fix-of` posted /progress fix; the claim is not new


def test_a_fix_needs_the_claim_of_the_task_it_fixes(world: World) -> None:
    started_parent(world)
    fix_file(world, parent="10-another-name")
    assert world.run(FIX) == 1 and "was made for the task '10-fix-the-fight-goal'" in world.said()
    assert not (world.scratch / FIX / "unit.json").exists()


def test_a_fix_of_a_task_that_has_no_unit_is_refused(world: World) -> None:
    started_parent(world)
    fix_file(world, parent="10-another-name")
    world.github.comment_list[10].clear()
    world.claim(task="10-another-name")
    assert world.run(FIX) == 1 and "no unit of task '10-another-name'" in world.said()


def test_a_fix_of_a_unit_that_was_never_started_is_refused(world: World) -> None:
    world.claim()
    fix_file(world)
    assert world.run(FIX) == 1 and f"no unit of task '{TASK}'" in world.said() and "task start" in world.said()


def test_a_fix_of_a_unit_never_released_is_refused(world: World) -> None:
    started_parent(world, released=False)
    fix_file(world)
    assert world.run(FIX) == 1 and "never released" in world.said() and "pr-opened" in world.said()
    assert not (world.scratch / FIX / "unit.json").exists()


def test_a_fix_whose_worktree_is_gone_is_refused(world: World) -> None:
    started_parent(world)
    fix_file(world)
    git(world.main, "worktree", "remove", "--force", str(world.worktree()))
    assert world.run(FIX) == 1 and "worktree is gone" in world.said()


def test_a_fix_task_file_without_a_parent_is_refused(world: World) -> None:
    started_parent(world)
    fix_file(world, parent="")
    assert world.run(FIX) == 1 and "names no parent" in world.said() and "task create --fix-of" in world.said()


def test_a_fix_reuses_the_claim_so_the_limit_does_not_apply(world: World) -> None:
    started_parent(world)
    fix_file(world)
    for number, task in ((20, "a"), (21, "b"), (22, "c")):
        world.claim(number, task)  # four open claims now: over the limit of 3
    assert world.run(FIX) == 0


def test_fix_slots_are_counted_apart_from_task_slots(world: World) -> None:
    plant_unit(world, "40-fix", kind="fix")
    started_parent(world)
    fix_file(world)
    assert world.run(FIX) == 0
    assert "slots: 2 of 1 fix slots in use" in world.said() and "not enforced" in world.said()
    assert "task slots" not in world.said()


def test_a_fix_is_over_when_a_review_passes_after_it_started(world: World) -> None:
    started_parent(world)
    fix_file(world)
    assert world.run(FIX) == 0
    assert task_start.units_in_work(world.scratch, world.conventions).fix == [FIX]
    stamps.append(
        world.scratch / TASK / "stamps",
        rules=RULES,
        task=TASK,
        step="reviewed",
        subject="",
        result="pass",
        commit=git(world.worktree(), "rev-parse", "HEAD").strip(),
        covers={},
        detail={},
        who=WHO,
        clock=world.clock.now,
    )
    in_work = task_start.units_in_work(world.scratch, world.conventions)
    # The parent has passed review too, so neither unit holds a slot.
    assert in_work.fix == [] and in_work.task == []


# --- the command line -----------------------------------------------------------------------------------------------
def parse(*words: str) -> argparse.Namespace:
    """Parse a `task` command line."""
    group = argparse.ArgumentParser().add_subparsers(dest="group")
    task_cli.add_task_parser(group)
    return group.choices["task"].parse_args(words)


def test_the_start_arguments() -> None:
    args = parse("start", "10-fix")
    assert (args.command, args.task, args.base, args.remote, args.no_fetch, args.dry_run) == (
        "start",
        "10-fix",
        None,
        "origin",
        False,
        False,
    )
    args = parse("start", "--base", "release", "--remote", "up", "--no-fetch", "--dry-run")
    assert (args.task, args.base, args.remote, args.no_fetch, args.dry_run) == (None, "release", "up", True, True)


def test_start_from_the_command_line_uses_the_checkout_it_runs_in(
    world: World, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    world.ready()
    monkeypatch.chdir(world.main)
    monkeypatch.delenv("CONEY_TASK", raising=False)
    status = task_cli.run_task(parse("start", TASK), remote=world.github, now=world.clock.now)
    assert status == 0 and world.worktree().is_dir()
    assert "started" in capsys.readouterr().out
    # The report names the one place to continue: the worktree.
    assert unit.task_name(world.worktree()) == TASK


def test_start_without_a_task_name_uses_coney_task_then_says_what_is_missing(
    world: World, monkeypatch: pytest.MonkeyPatch
) -> None:
    world.ready()
    monkeypatch.chdir(world.main)
    monkeypatch.setenv("CONEY_TASK", TASK)
    assert task_cli.run_task(parse("start"), remote=world.github, now=world.clock.now) == 0
    monkeypatch.delenv("CONEY_TASK")
    with pytest.raises(ConfigError, match="name the task"):
        task_cli.run_task(parse("start"), remote=world.github, now=world.clock.now)


def test_the_json_of_a_record_has_no_game_data_and_no_backslashes(world: World) -> None:
    world.ready()
    assert world.run() == 0
    text = (world.scratch / TASK / "unit.json").read_text(encoding="utf-8")
    assert "\\" not in text and parse_time(json.loads(text)["started"]) == world.clock.now()
