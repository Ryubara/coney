# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for `task start --restart` (task_start_restart; docs/guides/issues.md#restarting): a unit that was started
from another unit's branch (stacked) is started again from the default branch once that base has merged, because
`pr open` releases a unit based on the default branch only.

The scene is `test_task_start`'s `World` (a main checkout, a bare origin, claims on a fake GitHub): a base branch with
two commits, a unit started from it with one commit of its own, and the base squash-merged into `main`, which keeps
none of its commits, as GitHub's squash merge does. The last test goes on through `pr open`.
"""

import dataclasses
import shutil
from pathlib import Path

import pytest
from git_support import git
from pr_open_support import OPENER, make_open
from pr_support import make
from test_task_start import TASK, WHO, World, parse

from coney_tools import stamps, task_adopt, task_cli, task_start, task_start_restart
from coney_tools.config import ConfigError
from coney_tools.task_file import parse_task_file, render_task_file
from coney_tools.task_start import StartEnv, StartRequest, start_task
from coney_tools.unit_record import read_record, write_record

STACK = "base-unit"


@pytest.fixture
def world(tmp_path: Path) -> World:
    return World(tmp_path)


def stacked(world: World) -> Path:
    """The base branch, and the unit started from it with a commit of its own; returns the unit's worktree."""
    world.ready()
    git(world.main, "checkout", "-q", "-b", STACK)
    for name in ("base-one.txt", "base-two.txt"):
        (world.main / name).write_text(f"{name}\n", encoding="utf-8")
        git(world.main, "add", "-A")
        git(world.main, "commit", "-q", "-m", f"core: Add {name}")
    git(world.main, "checkout", "-q", "main")
    assert world.run(base=STACK) == 0, world.said()
    tree = world.worktree(TASK)
    (tree / "own.txt").write_text("the unit's own work\n", encoding="utf-8")
    git(tree, "add", "-A")
    git(tree, "commit", "-q", "-m", "core: Add the unit's own work")
    return tree


def merge_base_by_squash(world: World, tweak: bool = False) -> None:
    """The base lands on `main` as one commit with none of its commits, and `main` moves on once more. With `tweak`
    the squash commit also holds a change the base did not (as when the merge was resolved by hand), so its change is
    not the base's."""
    git(world.main, "merge", "-q", "--squash", STACK)
    if tweak:
        (world.main / "base-one.txt").write_text("base-one.txt\nresolved by hand\n", encoding="utf-8")
        git(world.main, "add", "-A")
    git(world.main, "commit", "-q", "-m", "core: Add the base (#1)")
    git(world.main, "push", "-q", "origin", "main")
    world.push_to_origin("after-the-base.txt")


def rebase_onto_main(tree: Path) -> None:
    """The engineer's rebase of the unit off the merged base, as the guides give it."""
    git(tree, "fetch", "-q", "origin", "main")
    git(tree, "rebase", "-q", "--onto", "origin/main", STACK, TASK)


def log_folder(world: World) -> Path:
    return world.scratch / TASK / "stamps"


def archives(world: World) -> list[Path]:
    """The folders an old stamp log was moved to."""
    return [p for p in (world.scratch / TASK).iterdir() if p.name.startswith(task_adopt.ARCHIVE_PREFIX)]


def snapshot(world: World) -> dict[str, str]:
    """Every file of the task's scratch folder with its text, to show a refusal changed nothing."""
    return {
        str(p.relative_to(world.scratch)): p.read_text(encoding="utf-8")
        for p in sorted((world.scratch / TASK).rglob("*"))
        if p.is_file()
    }


def test_a_unit_whose_base_merged_starts_again_from_the_default_branch(world: World) -> None:
    tree = stacked(world)
    merge_base_by_squash(world)
    rebase_onto_main(tree)
    tip = git(tree, "rev-parse", "HEAD").strip()
    assert world.record().base_ref == STACK
    world.output.clear()
    assert world.run(restart=True) == 0, world.said()
    record = world.record()
    assert record.base_ref == "origin/main"
    # The unit starts where the branch left the default branch, not at the default branch's tip.
    assert record.base_commit == git(tree, "merge-base", "HEAD", "origin/main").strip()
    # The branch and the worktree are the unit's own: nothing is moved, rebased or remade.
    assert record.branch == TASK and record.worktree.resolve() == tree.resolve()
    assert git(tree, "rev-parse", "HEAD").strip() == tip
    assert "restarted" in world.said()


def test_the_new_log_has_assigned_and_claimed_and_says_it_was_restarted(world: World) -> None:
    tree = stacked(world)
    merge_base_by_squash(world)
    rebase_onto_main(tree)
    assert world.run(restart=True) == 0, world.said()
    report = world.log()
    assert report.ok and [s.step for s in report.stamps] == ["assigned", "claimed"]
    assigned, claimed = report.stamps
    assert assigned.detail["restarted"] is True
    assert claimed.detail["base_ref"] == "origin/main" and claimed.detail["branch"] == TASK
    assert claimed.detail["restarted"]["from_base_ref"] == STACK
    assert claimed.covers["base"] == world.record().base_commit
    assert claimed.who["source"] == "hook"


def test_the_old_log_is_moved_aside_not_deleted(world: World) -> None:
    tree = stacked(world)
    stamps.append(
        log_folder(world),
        rules=world.conventions.stamps,
        task=TASK,
        step="studied",
        subject="0x001490b8",
        result="pass",
        commit=git(tree, "rev-parse", "HEAD").strip(),
        covers={},
        detail={},
        who=WHO,
        clock=world.clock.now,
    )
    before = (log_folder(world) / "0001.jsonl").read_text(encoding="utf-8")
    merge_base_by_squash(world)
    rebase_onto_main(tree)
    assert world.run(restart=True) == 0, world.said()
    moved = archives(world)
    assert len(moved) == 1 and (moved[0] / "0001.jsonl").read_text(encoding="utf-8") == before
    assert str(moved[0]) in world.said()
    # The stages are left to redo: the new log holds only the start.
    assert [s.step for s in world.log().stamps] == ["assigned", "claimed"]


def test_progress_is_not_posted_again(world: World) -> None:
    tree = stacked(world)
    merge_base_by_squash(world)
    rebase_onto_main(tree)
    assert world.posted == [(10, "/progress claimed")]
    assert world.run(restart=True) == 0, world.said()
    assert world.posted == [(10, "/progress claimed")]


def test_a_dry_run_says_what_it_would_do_and_changes_nothing(world: World) -> None:
    tree = stacked(world)
    merge_base_by_squash(world)
    rebase_onto_main(tree)
    before = snapshot(world)
    assert world.run(restart=True, dry_run=True) == 0, world.said()
    assert "would" in world.said() and "restart" in world.said()
    assert snapshot(world) == before and archives(world) == []


def test_a_restarted_unit_is_started_already_and_cannot_be_restarted_again(world: World) -> None:
    tree = stacked(world)
    merge_base_by_squash(world)
    rebase_onto_main(tree)
    assert world.run(restart=True) == 0, world.said()
    before = snapshot(world)
    world.output.clear()
    assert world.run(restart=True) == 1
    assert "started from origin/main already" in world.said()
    assert world.run() == 0 and "already started" in world.said()
    assert snapshot(world) == before


def test_a_base_that_has_not_merged_is_refused_and_nothing_changes(world: World) -> None:
    stacked(world)
    before = snapshot(world)
    assert world.run(restart=True) == 1
    assert "has not merged" in world.said() and STACK in world.said()
    assert snapshot(world) == before and archives(world) == []
    assert world.record().base_ref == STACK


def test_a_base_merged_in_part_is_refused(world: World) -> None:
    tree = stacked(world)
    # Only the first of the base's two commits reached `main`.
    git(world.main, "cherry-pick", f"{STACK}~1")
    git(world.main, "push", "-q", "origin", "main")
    git(tree, "fetch", "-q", "origin", "main")
    assert world.run(restart=True) == 1
    assert "has not merged" in world.said() and "base-two.txt" in world.said()
    assert archives(world) == []


def test_a_branch_still_holding_the_old_bases_commits_is_refused(world: World) -> None:
    stacked(world)
    merge_base_by_squash(world)
    before = snapshot(world)
    assert world.run(restart=True) == 1
    assert "still holds 2 commits of the old base" in world.said()
    # It names the rebase that takes them off, the one the guides give.
    assert f"git rebase --onto origin/main {STACK} {TASK}" in world.said()
    assert snapshot(world) == before and archives(world) == []


def test_a_copy_of_a_base_commit_under_another_hash_is_refused(world: World) -> None:
    tree = stacked(world)
    fork = git(tree, "merge-base", TASK, "main").strip()
    merge_base_by_squash(world)
    # The branch was rebuilt on the old `main` with the base's commits cherry-picked: other hashes, the same changes.
    git(tree, "reset", "-q", "--hard", fork)
    git(tree, "cherry-pick", f"{STACK}~1", STACK)
    (tree / "own.txt").write_text("the unit's own work\n", encoding="utf-8")
    git(tree, "add", "-A")
    git(tree, "commit", "-q", "-m", "core: Add the unit's own work")
    git(tree, "fetch", "-q", "origin", "main")
    assert not set(git(tree, "rev-list", f"origin/main..{TASK}").split()) & set(git(tree, "rev-list", STACK).split())
    assert world.run(restart=True) == 1
    assert "same change as a commit of the old base" in world.said()
    assert archives(world) == []


def test_a_base_the_default_branch_edited_after_the_squash_is_judged_merged_by_its_change(world: World) -> None:
    tree = stacked(world)
    merge_base_by_squash(world)
    rebase_onto_main(tree)
    # `main` edits a file the base added, after the squash: merging the base into it conflicts, but the squash commit
    # in `main` is exactly the base's whole change.
    world.push_to_origin("base-one.txt")
    assert world.run(restart=True) == 0, world.said()
    assert world.record().base_ref == "origin/main"
    assert "base_merged_override" not in world.log().stamps[1].detail["restarted"]


def test_a_base_merged_commit_by_commit_and_edited_since_is_judged_merged(world: World) -> None:
    tree = stacked(world)
    git(world.main, "cherry-pick", f"{STACK}~1", STACK)
    git(world.main, "push", "-q", "origin", "main")
    world.push_to_origin("base-one.txt")
    rebase_onto_main(tree)
    assert world.run(restart=True) == 0, world.said()
    assert world.record().base_ref == "origin/main"


def test_the_merge_the_check_cannot_tell_is_refused_and_names_the_way_on(world: World) -> None:
    tree = stacked(world)
    merge_base_by_squash(world, tweak=True)
    rebase_onto_main(tree)
    world.push_to_origin("base-one.txt")
    before = snapshot(world)
    assert world.run(restart=True) == 1
    said = world.said()
    assert "has not merged" in said and "conflicts in base-one.txt" in said
    assert f'task start {TASK} --restart --base-merged-reason "<why it merged>"' in said
    assert "coordinator" in said
    assert snapshot(world) == before and archives(world) == []
    assert world.record().base_ref == STACK


def test_a_coordinator_reason_goes_on_and_is_recorded_in_the_claimed_stamp(world: World) -> None:
    tree = stacked(world)
    merge_base_by_squash(world, tweak=True)
    rebase_onto_main(tree)
    world.push_to_origin("base-one.txt")
    why = "PR #1 merged it with a hand-resolved squash; checked by the coordinator"
    assert world.run(restart=True, base_merged_reason=why) == 0, world.said()
    assert world.record().base_ref == "origin/main"
    claimed = world.log().stamps[1]
    override = claimed.detail["restarted"]["base_merged_override"]
    assert override["reason"] == why and "conflicts in base-one.txt" in override["check"]
    assert claimed.detail["restarted"]["from_base_ref"] == STACK
    assert "recorded in the claimed stamp" in world.said()


def test_a_reason_is_not_needed_and_not_recorded_when_the_base_is_shown_merged(world: World) -> None:
    tree = stacked(world)
    merge_base_by_squash(world)
    rebase_onto_main(tree)
    assert world.run(restart=True, base_merged_reason="to be sure") == 0, world.said()
    assert "not needed" in world.said()
    assert "base_merged_override" not in world.log().stamps[1].detail["restarted"]


def test_a_reason_does_not_excuse_the_branch_holding_the_old_bases_commits(world: World) -> None:
    stacked(world)
    merge_base_by_squash(world, tweak=True)
    assert world.run(restart=True, base_merged_reason="it merged") == 1
    assert "still holds 2 commits of the old base" in world.said() and archives(world) == []


def test_a_reason_in_a_dry_run_changes_nothing(world: World) -> None:
    tree = stacked(world)
    merge_base_by_squash(world, tweak=True)
    rebase_onto_main(tree)
    world.push_to_origin("base-one.txt")
    before = snapshot(world)
    assert world.run(restart=True, dry_run=True, base_merged_reason="it merged") == 0, world.said()
    assert "it merged" in world.said() and snapshot(world) == before and world.record().base_ref == STACK


def test_a_reason_needs_restart_and_words(world: World) -> None:
    tree = stacked(world)
    merge_base_by_squash(world)
    rebase_onto_main(tree)
    assert world.run(base_merged_reason="it merged") == 1 and "goes with --restart" in world.said()
    world.output.clear()
    assert world.run(restart=True, base_merged_reason="  ") == 1 and "needs a reason" in world.said()
    assert world.record().base_ref == STACK and archives(world) == []


def test_a_base_branch_deleted_after_the_merge_is_judged_from_the_commit_the_unit_recorded(world: World) -> None:
    tree = stacked(world)
    merge_base_by_squash(world)
    rebase_onto_main(tree)
    git(world.main, "branch", "-q", "-D", STACK)
    assert world.run(restart=True) == 0, world.said()
    assert world.record().base_ref == "origin/main"


def test_a_base_that_is_not_in_the_repository_is_refused(world: World) -> None:
    tree = stacked(world)
    merge_base_by_squash(world)
    rebase_onto_main(tree)
    git(world.main, "branch", "-q", "-D", STACK)
    record_path = world.scratch / TASK / "unit.json"
    write_record(record_path, dataclasses.replace(read_record(record_path), base_commit="1" * 40))
    before = snapshot(world)
    assert world.run(restart=True) == 1
    assert "is not in this repository" in world.said()
    assert snapshot(world) == before and archives(world) == []


def test_no_fetch_judges_by_the_refs_as_they_are(world: World) -> None:
    tree = stacked(world)
    merge_base_by_squash(world)
    rebase_onto_main(tree)
    # The remote cannot be reached, so any fetch would fail.
    git(world.main, "remote", "set-url", "origin", str(world.tmp / "gone.git"))
    with pytest.raises(ConfigError, match="fetch"):
        world.run(restart=True)
    assert world.record().base_ref == STACK
    assert world.run(restart=True, fetch=False) == 0, world.said()
    assert world.record().base_ref == "origin/main"


def test_a_branch_with_no_commit_in_common_with_the_default_branch_is_refused(world: World) -> None:
    tree = stacked(world)
    merge_base_by_squash(world)
    rebase_onto_main(tree)
    # The branch is rebuilt as a root commit with the same files.
    root = git(tree, "commit-tree", "HEAD^{tree}", "-m", "core: Add everything at once").strip()
    git(tree, "reset", "-q", "--hard", root)
    before = snapshot(world)
    assert world.run(restart=True) == 1
    assert "no common commit" in world.said()
    assert snapshot(world) == before and archives(world) == []


def test_a_worktree_that_is_gone_is_refused(world: World) -> None:
    tree = stacked(world)
    merge_base_by_squash(world)
    rebase_onto_main(tree)
    shutil.rmtree(tree)
    assert world.run(restart=True) == 1
    assert "worktree is gone" in world.said() and archives(world) == []


def test_a_branch_that_is_gone_is_refused(world: World) -> None:
    tree = stacked(world)
    merge_base_by_squash(world)
    rebase_onto_main(tree)
    git(tree, "checkout", "-q", "--detach")
    git(world.main, "branch", "-q", "-D", TASK)
    assert world.run(restart=True) == 1
    assert "does not exist here" in world.said() and archives(world) == []


def test_a_damaged_log_is_refused_before_anything_is_moved(world: World) -> None:
    tree = stacked(world)
    merge_base_by_squash(world)
    rebase_onto_main(tree)
    file = log_folder(world) / "0001.jsonl"
    file.write_text(file.read_text(encoding="utf-8") + "not a stamp" + chr(10), encoding="utf-8")
    before = snapshot(world)
    assert world.run(restart=True) == 1
    assert "stamp log cannot be used" in world.said()
    assert snapshot(world) == before and archives(world) == []
    assert world.record().base_ref == STACK


def test_a_restart_whose_stamping_failed_is_run_again(world: World, monkeypatch: pytest.MonkeyPatch) -> None:
    tree = stacked(world)
    merge_base_by_squash(world)
    rebase_onto_main(tree)
    real = task_start._stamp_start

    def fail(*args: object, **kwargs: object) -> None:
        raise ConfigError("the disk is full")

    monkeypatch.setattr("coney_tools.task_start_restart._stamp_start", fail)
    with pytest.raises(ConfigError, match="disk is full"):
        world.run(restart=True)
    # The old log was moved, the record was not touched: the unit is still the stacked one and can restart.
    assert len(archives(world)) == 1 and world.record().base_ref == STACK
    monkeypatch.setattr("coney_tools.task_start_restart._stamp_start", real)
    world.output.clear()
    assert world.run(restart=True) == 0, world.said()
    assert world.record().base_ref == "origin/main"
    assert [s.step for s in world.log().stamps] == ["assigned", "claimed"]
    assert world.log().stamps[1].detail["restarted"]["from_base_ref"] == STACK
    # The new `claimed` stamp points at the log the first run moved aside, not at nothing.
    assert world.log().stamps[1].detail["restarted"]["log"] == archives(world)[0].name


def test_a_failing_git_is_an_error_and_not_an_empty_answer(world: World) -> None:
    stacked(world)
    merge_base_by_squash(world)
    with pytest.raises(ConfigError, match="rev-list"):
        task_start_restart.foreign_problem(world.main, "origin/main", TASK, ["1" * 40], STACK)


def test_a_unit_that_was_not_stacked_has_nothing_to_restart(world: World) -> None:
    world.ready()
    assert world.run() == 0
    before = snapshot(world)
    world.output.clear()
    assert world.run(restart=True) == 1
    assert "started from origin/main already" in world.said()
    assert snapshot(world) == before


def test_a_task_never_started_has_no_unit_to_restart(world: World) -> None:
    world.ready()
    assert world.run(restart=True) == 1
    assert "no unit to restart" in world.said() and not world.posted


def test_a_unit_that_was_released_is_not_restarted(world: World) -> None:
    tree = stacked(world)
    merge_base_by_squash(world)
    rebase_onto_main(tree)
    stamps.append(
        log_folder(world),
        rules=world.conventions.stamps,
        task=TASK,
        step="pr-opened",
        subject="",
        result="pass",
        commit=git(tree, "rev-parse", "HEAD").strip(),
        covers={},
        detail={"pr": 5},
        who=WHO,
        clock=world.clock.now,
    )
    assert world.run(restart=True) == 1
    assert "pr-opened" in world.said() and archives(world) == []


def test_a_worktree_on_another_branch_is_refused(world: World) -> None:
    tree = stacked(world)
    merge_base_by_squash(world)
    rebase_onto_main(tree)
    git(tree, "checkout", "-q", "--detach")
    assert world.run(restart=True) == 1
    assert "is not on the unit's branch" in world.said() and archives(world) == []


def test_restart_does_not_go_with_adopt_or_base_or_a_fix_task(world: World) -> None:
    tree = stacked(world)
    merge_base_by_squash(world)
    rebase_onto_main(tree)
    assert world.run(restart=True, adopt=True) == 1
    assert "--restart" in world.said()
    world.output.clear()
    assert world.run(restart=True, base="origin/main") == 1
    assert "--restart" in world.said()
    fix = f"{TASK}-fix"
    world.task_file(fix, kind="fix", parent=TASK)
    world.output.clear()
    assert world.run(fix, restart=True) == 1
    assert "fix task" in world.said()
    assert archives(world) == []


def test_the_command_line_takes_restart(
    world: World, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    assert parse("start", "t", "--restart").restart is True
    assert parse("start", "t").restart is False
    assert parse("start", "t", "--restart", "--base-merged-reason", "why").base_merged_reason == "why"
    assert parse("start", "t").base_merged_reason is None
    tree = stacked(world)
    merge_base_by_squash(world)
    rebase_onto_main(tree)
    monkeypatch.chdir(world.main)
    monkeypatch.delenv("CONEY_TASK", raising=False)
    status = task_cli.run_task(parse("start", TASK, "--restart"), remote=world.github, now=world.clock.now)
    assert status == 0 and world.record().base_ref == "origin/main"
    assert "restarted" in capsys.readouterr().out


def test_pr_open_refuses_a_stacked_unit_and_accepts_it_once_restarted(tmp_path: Path) -> None:
    """The route end to end: `pr open` refuses the unit started from another branch and names the command, the restart
    makes it a unit from `origin/main`, its stages are redone and `pr open` releases it."""
    built = make(tmp_path, "docs", start=False)
    task_path = built.unit.dir / "TASK.md"
    task = parse_task_file(task_path.read_text(encoding="utf-8"), built.conventions.tasks)
    task_path.write_text(render_task_file(dataclasses.replace(task, claimant=OPENER)), encoding="utf-8")
    opened = make_open(built)
    # The unit as a stacked start left it: its base is another branch, whose commits are all in `main` here.
    git(built.root, "branch", STACK, built.base)
    record_path = built.unit.dir / "unit.json"
    record = read_record(record_path)
    write_record(record_path, dataclasses.replace(record, base_ref=STACK))
    assert opened.run() == 2
    assert "task start" in opened.text() and "--restart" in opened.text()
    env = StartEnv(
        root=built.root,
        remote=opened.github,
        conventions=built.conventions,
        scratch=built.unit.dir.parent,
        now=built.clock,
        who=lambda: dict(WHO),
        out=opened.said.append,
    )
    assert start_task(StartRequest(task=built.task, restart=True, fetch=False), env) == 0, opened.text()
    assert read_record(record_path).base_ref == "origin/main"
    assert [s.step for s in stamps.verify(built.unit.stamps, built.conventions.stamps).stamps] == [
        "assigned",
        "claimed",
    ]
    # The stages are redone on the restarted unit, as after an adoption.
    built.stamp("fails-first", result="none", detail={"reason": "documentation, no game behaviour"})
    built.check("docs")
    built.review(functions={})
    result = built.verify()
    assert result.ok, result.refusals
    opened.said.clear()
    assert opened.run() == 0, opened.text()
    assert opened.opened_stamps()
