# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for `task start --adopt-branch` (task_start, task_adopt; docs/guides/issues.md#adopting): the unit of a task on
a branch that was released before `pr open` existed, so a further round has a route once the PR guard refuses every
hand-typed push.

The first half uses the workspace of `test_task_start` (a main checkout, a bare origin, claims on a fake GitHub); the
last test goes on through `pr verify` and `pr open --adopt` on a unit of `pr_support`, to show that the route ends in
a release.
"""

import dataclasses
import subprocess
from pathlib import Path

import pytest
from git_support import git
from pr_open_support import OPENER, make_open
from pr_support import make
from test_task_start import TASK, WHO, World, parse

from coney_tools import pr_publish, stamps, task_adopt, task_cli
from coney_tools.task_file import parse_task_file, render_task_file
from coney_tools.task_start import StartEnv, StartRequest, start_task
from coney_tools.unit_record import read_record

BRANCH = "fix-player-targeting"


@pytest.fixture
def world(tmp_path: Path) -> World:
    return World(tmp_path)


def legacy_branch(world: World, remote_only: bool = False) -> str:
    """A branch with two commits of its own, as a pull request opened by hand leaves it; returns its tip."""
    git(world.main, "checkout", "-q", "-b", BRANCH)
    (world.main / "work.txt").write_text("the work\n", encoding="utf-8")
    git(world.main, "add", "-A")
    git(world.main, "commit", "-q", "-m", "gameplay: the work")
    (world.main / "more.txt").write_text("more of it\n", encoding="utf-8")
    git(world.main, "add", "-A")
    git(world.main, "commit", "-q", "-m", "gameplay: more of it")
    tip = git(world.main, "rev-parse", "HEAD").strip()
    git(world.main, "push", "-q", "origin", BRANCH)
    git(world.main, "checkout", "-q", "main")
    if remote_only:
        git(world.main, "branch", "-q", "-D", BRANCH)
    return tip


def legacy_log(world: World, task: str = TASK) -> Path:
    """The stamp log a unit of the time before the workflow left: it starts with `studied`."""
    folder = world.scratch / task / "stamps"
    stamps.append(
        folder,
        rules=world.conventions.stamps,
        task=task,
        step="studied",
        subject="0x001490b8",
        result="pass",
        commit=git(world.main, "rev-parse", "main").strip(),
        covers={},
        detail={},
        who=WHO,
        clock=world.clock.now,
    )
    return folder


def archives(world: World, task: str = TASK) -> list[Path]:
    """The folders an old stamp log was moved to."""
    return [p for p in (world.scratch / task).iterdir() if p.name.startswith(task_adopt.ARCHIVE_PREFIX)]


def test_the_unit_is_made_on_the_existing_branch_and_the_branch_is_not_moved(world: World) -> None:
    world.ready()
    tip = legacy_branch(world)
    assert world.run(adopt_branch=BRANCH) == 0, world.said()
    tree = world.worktree(BRANCH)
    assert tree.is_dir() and git(tree, "rev-parse", "HEAD").strip() == tip
    assert git(tree, "rev-parse", "--abbrev-ref", "HEAD").strip() == BRANCH
    # No branch of the task's name was made: the unit is on the pull request's branch.
    assert TASK not in world.branches()
    record = world.record()
    assert record.branch == BRANCH and record.worktree.resolve() == tree.resolve() and record.task == TASK
    assert record.base_ref == "origin/main"
    assert record.base_commit == git(world.main, "rev-parse", "origin/main").strip()
    assert record.progress_posted and world.posted == [(10, "/progress claimed")]
    assert "pr open" in world.said() and "--adopt" in world.said()


def test_the_log_starts_with_assigned_and_claimed_and_the_claim_says_it_was_adopted(world: World) -> None:
    world.ready()
    legacy_branch(world)
    assert world.run(adopt_branch=BRANCH) == 0, world.said()
    report = world.log()
    assert report.ok and [s.step for s in report.stamps] == ["assigned", "claimed"]
    claimed = report.stamps[1]
    assert claimed.detail["branch"] == BRANCH and claimed.detail["adopted"] is True
    assert claimed.who["source"] == "hook"


def test_a_branch_that_exists_only_on_the_remote_is_made_here_first(world: World) -> None:
    world.ready()
    tip = legacy_branch(world, remote_only=True)
    assert BRANCH not in world.branches()
    assert world.run(adopt_branch=BRANCH) == 0, world.said()
    assert git(world.main, "rev-parse", f"refs/heads/{BRANCH}").strip() == tip
    assert git(world.worktree(BRANCH), "rev-parse", "HEAD").strip() == tip


def test_a_worktree_the_branch_already_has_becomes_the_units(world: World) -> None:
    world.ready()
    legacy_branch(world)
    elsewhere = world.tmp / "elsewhere"
    git(world.main, "worktree", "add", "-q", str(elsewhere), BRANCH)
    assert world.run(adopt_branch=BRANCH) == 0, world.said()
    assert world.record().worktree.resolve() == elsewhere.resolve()
    assert not world.worktree(BRANCH).exists()


def test_the_old_stamp_log_is_moved_aside_not_deleted_and_the_new_one_starts_afresh(world: World) -> None:
    world.ready()
    legacy_branch(world)
    old = legacy_log(world)
    before = (old / "0001.jsonl").read_text(encoding="utf-8")
    assert world.run(adopt_branch=BRANCH) == 0, world.said()
    moved = archives(world)
    assert len(moved) == 1 and (moved[0] / "0001.jsonl").read_text(encoding="utf-8") == before
    assert str(moved[0]) in world.said()
    report = world.log()
    assert report.ok and [s.step for s in report.stamps] == ["assigned", "claimed"]


def add_stamp(world: World, step: str, subject: str = "") -> None:
    """Append one more stamp to the task's log."""
    stamps.append(
        world.scratch / TASK / "stamps",
        rules=world.conventions.stamps,
        task=TASK,
        step=step,
        subject=subject,
        result="pass",
        commit=git(world.main, "rev-parse", "main").strip(),
        covers={},
        detail={},
        who=WHO,
        clock=world.clock.now,
    )


def test_a_damaged_old_log_that_starts_with_another_step_is_moved_aside_too(world: World) -> None:
    world.ready()
    legacy_branch(world)
    folder = legacy_log(world)
    add_stamp(world, "studied", "0x001490c0")
    (folder / "0002.jsonl").write_text("not json\n", encoding="utf-8")
    assert not world.log().ok
    assert world.run(adopt_branch=BRANCH) == 0, world.said()
    assert world.log().ok and len(archives(world)) == 1


def test_a_damaged_log_whose_first_stamp_is_assigned_is_not_reset_by_adopting(world: World) -> None:
    world.ready()
    legacy_branch(world)
    assert world.run() == 0
    add_stamp(world, "studied", "0x001490c0")
    folder = world.scratch / TASK / "stamps"
    (folder / "0003.jsonl").write_text("not json\n", encoding="utf-8")
    (world.scratch / TASK / "unit.json").unlink()
    before = {p.name: p.read_text(encoding="utf-8") for p in folder.iterdir() if p.suffix == ".jsonl"}
    world.output.clear()
    assert world.run(adopt_branch=BRANCH) == 1
    assert "starts the workflow's way already" in world.said()
    assert archives(world) == [] and {p.name: p.read_text(encoding="utf-8") for p in folder.glob("*.jsonl")} == before


def test_a_log_whose_first_stamp_cannot_be_read_is_not_moved_aside(world: World) -> None:
    world.ready()
    legacy_branch(world)
    folder = legacy_log(world)
    (folder / "0001.jsonl").write_text("not json\n", encoding="utf-8")
    assert world.run(adopt_branch=BRANCH) == 1
    assert "first stamp cannot be read" in world.said() and archives(world) == []
    assert (folder / "0001.jsonl").read_text(encoding="utf-8") == "not json\n"


def test_a_log_that_already_starts_the_workflows_way_is_not_moved(world: World) -> None:
    world.ready()
    legacy_branch(world)
    assert world.run() == 0
    (world.scratch / TASK / "unit.json").unlink()
    world.output.clear()
    assert world.run(adopt_branch=BRANCH) == 1
    assert "starts the workflow's way already" in world.said()
    assert archives(world) == []


def test_a_rerun_after_the_adoption_changes_nothing(world: World) -> None:
    world.ready()
    legacy_branch(world)
    legacy_log(world)
    assert world.run(adopt_branch=BRANCH) == 0
    snapshot = sorted(p.name for p in (world.scratch / TASK).iterdir())
    world.output.clear()
    assert world.run(adopt_branch=BRANCH) == 0
    assert "already started" in world.said()
    assert sorted(p.name for p in (world.scratch / TASK).iterdir()) == snapshot


def test_a_dry_run_says_what_it_would_do_and_changes_nothing(world: World) -> None:
    world.ready()
    legacy_branch(world)
    legacy_log(world)
    before = sorted(p.name for p in (world.scratch / TASK).iterdir())
    assert world.run(adopt_branch=BRANCH, dry_run=True) == 0, world.said()
    assert "would move the old stamp log" in world.said() and BRANCH in world.said()
    assert sorted(p.name for p in (world.scratch / TASK).iterdir()) == before
    assert not world.worktree(BRANCH).exists() and not world.posted


def test_a_branch_that_does_not_exist_is_refused(world: World) -> None:
    world.ready()
    assert world.run(adopt_branch="no-such-branch") == 1
    assert "no branch 'no-such-branch'" in world.said() and not world.posted


def test_a_branch_that_is_already_a_units_is_refused(world: World) -> None:
    world.ready()
    legacy_branch(world)
    world.ready("11-other", issues=(11,))
    assert world.run(adopt_branch=BRANCH) == 0
    world.output.clear()
    assert world.run("11-other", adopt_branch=BRANCH) == 1
    assert f"the unit of the task '{TASK}' already" in world.said()


def test_the_default_branch_is_not_a_branch_to_adopt(world: World) -> None:
    world.ready()
    assert world.run(adopt_branch="main") == 1
    assert "not a branch with a pull request" in world.said()


def test_a_branch_checked_out_in_the_main_checkout_is_refused(world: World) -> None:
    world.ready()
    legacy_branch(world)
    git(world.main, "checkout", "-q", BRANCH)
    assert world.run(adopt_branch=BRANCH) == 1
    assert "main checkout" in world.said()


def test_a_fix_task_works_in_its_parents_unit_and_adopts_nothing(world: World) -> None:
    world.ready()
    legacy_branch(world)
    assert world.run(adopt_branch=BRANCH) == 0
    fix = f"{TASK}-fix"
    world.task_file(fix, kind="fix", parent=TASK)
    world.output.clear()
    assert world.run(fix, adopt_branch=BRANCH) == 1
    assert "fix task works in its parent's unit" in world.said()


def test_the_command_line_takes_adopt_branch_and_starts_the_unit(
    world: World, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    assert parse("start", "t", "--adopt-branch", BRANCH).adopt_branch == BRANCH
    assert parse("start", "t").adopt_branch is None
    world.ready()
    legacy_branch(world)
    monkeypatch.chdir(world.main)
    monkeypatch.delenv("CONEY_TASK", raising=False)
    status = task_cli.run_task(parse("start", TASK, "--adopt-branch", BRANCH), remote=world.github, now=world.clock.now)
    assert status == 0 and world.record().branch == BRANCH
    assert "started on the existing branch" in capsys.readouterr().out


def test_an_adopted_pull_request_is_released_again_by_pr_open(tmp_path: Path) -> None:
    """The route end to end: a unit with a legacy log and a pull request opened by hand, adopted, its stages redone,
    verified and updated by `pr open --adopt`."""
    built = make(tmp_path, "docs", start=False)
    task_path = built.unit.dir / "TASK.md"
    task = parse_task_file(task_path.read_text(encoding="utf-8"), built.conventions.tasks)
    task_path.write_text(render_task_file(dataclasses.replace(task, claimant=OPENER)), encoding="utf-8")
    # The legacy state: stamps that do not begin with assigned and claimed, no unit record, a pull request by hand.
    assert any(r.stage == "log" for r in built.verify(text=False).refusals)
    opened = make_open(built)
    (built.unit.dir / "unit.json").unlink()
    other = tmp_path / "by-hand"
    subprocess.run(["git", "clone", "-q", str(built.origin), str(other)], check=True)
    git(other, "checkout", "-q", "-b", "work", "origin/main")
    (other / "early.md").write_text("an early round\n", encoding="utf-8")
    git(other, "add", "-A")
    git(other, "commit", "-q", "-m", "an early round, pushed by hand")
    git(other, "push", "-q", "origin", "work")
    head = git(other, "rev-parse", "HEAD").strip()
    opened.gh.prs[900] = {
        "head": "work",
        "title": "docs: Add the widget",
        "body": "by hand",
        "state": "OPEN",
        "merged": False,
        "auto": True,
        "url": "https://github.com/acme/widgets/pull/900",
    }
    # The work is in the main checkout on `work`; the unit needs a worktree, so the checkout goes back to main.
    git(built.root, "checkout", "-q", "main")
    env = StartEnv(
        root=built.root,
        remote=opened.github,
        conventions=built.conventions,
        scratch=built.unit.dir.parent,
        now=built.clock,
        who=lambda: dict(WHO),
        out=opened.said.append,
    )
    assert start_task(StartRequest(task=built.task, adopt_branch="work", fetch=False), env) == 0, opened.text()
    tree = read_record(built.unit.dir / "unit.json").worktree
    assert git(tree, "rev-parse", "--abbrev-ref", "HEAD").strip() == "work"
    log = stamps.verify(built.unit.stamps, built.conventions.stamps)
    assert [s.step for s in log.stamps] == ["assigned", "claimed"]
    assert any(p.name.startswith(task_adopt.ARCHIVE_PREFIX) for p in built.unit.dir.iterdir())
    # The unit's local branch lacks the commit pushed by hand; the engineer brings it in, as `pr open --adopt` demands.
    git(tree, "fetch", "-q", "origin", "work")
    git(tree, "cherry-pick", head)
    # Redo the stages on the adopted unit; the log's first step passes now.
    redone = dataclasses.replace(built, root=tree)
    redone.stamp("fails-first", result="none", detail={"reason": "documentation, no game behaviour"})
    redone.check("docs")
    redone.review(functions={})
    result = redone.verify()
    assert result.ok, result.refusals
    # `pr open --adopt` takes the pull request over with the head read from GitHub.
    opened.said.clear()
    assert opened.run(worktree=tree, adopt=head) == 0, opened.text()
    assert opened.origin_tip() == git(tree, "rev-parse", "HEAD").strip()
    assert opened.gh.prs[900]["title"] != "docs: Add the widget"
    assert (built.unit.dir / pr_publish.PUBLISHED_FILE).is_file()
