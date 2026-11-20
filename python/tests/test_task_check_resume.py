# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for the resume of `coney-tools task check` (issue 1380): a rerun for an unchanged change goes on from the
checks that passed, a changed tree runs everything, and the stamp says which checks it took from the earlier run. The
repository, the stamp log and the ledger are real files in a temporary folder; the checks and the build are the fakes
of test_task_check.py."""

import json
import shutil
from collections.abc import Callable
from dataclasses import replace
from datetime import timedelta
from pathlib import Path
from typing import Any

import pytest
import yaml
from conventions_support import REMOVE, changed, conventions_with
from git_support import git
from test_task_check import TASK, TESTS, World, make_world, start_fix

from coney_tools import check_resume, task_check, unit
from coney_tools.build_claims import SlotTimeout
from coney_tools.cli import main
from coney_tools.conventions import Conventions, ConventionsError, load_conventions, parse_conventions


@pytest.fixture
def world(tmp_path: Path) -> World:
    return make_world(tmp_path)


DOC = "# Widget\n\nTwo.\n"
FULL_DOC = ["docs-check", "mkdocs", "pre-commit", "pytest"]
BRAIN = "int main() { return 0; }\n"


def ledger_file(world: World) -> Path:
    """Where the run keeps what passed: in the task's scratch folder."""
    return unit.Unit.of(world.root, TASK).dir / check_resume.FOLDER / check_resume.FILE


def fail_then_fix(world: World, name: str = "mkdocs") -> None:
    """A run of the docs profile in which `name` fails, then the fault goes away with the tree as it was."""
    world.edit("docs/guides/widget.md", DOC)
    world.failing[name] = ["WARNING"]
    assert world.check() == 1
    world.failing.clear()
    world.calls.clear()


# --- what resumes -------------------------------------------------------------------------------------------------


def test_a_rerun_for_an_unchanged_change_goes_on_from_the_first_check_that_did_not_pass(world: World) -> None:
    fail_then_fix(world)
    assert world.check() == 0
    assert world.calls == ["mkdocs", "pre-commit", "pytest"]  # docs-check passed before and is not run again
    assert any("skip docs-check" in line for line in world.said)


def test_the_stamp_of_a_resumed_run_lists_every_check_and_says_which_were_taken_from_the_earlier_run(
    world: World,
) -> None:
    fail_then_fix(world)
    first = world.stamp
    key_sha256 = json.loads(ledger_file(world).read_text(encoding="utf-8"))["key_sha256"]
    assert world.check() == 0
    stamp = world.stamp
    assert stamp.result == "pass" and stamp.detail["checks"] == FULL_DOC
    assert stamp.covers["checks_sha256"] == world.stamp.covers["checks_sha256"]
    resumed = stamp.detail["resumed"]
    assert resumed["checks"] == ["docs-check"] and len(resumed["from"]) == 1
    assert resumed["from"][0] < first.time  # the run that did it began before it stamped
    assert resumed["key_sha256"] == key_sha256
    assert "resumed" not in first.detail  # a run that took nothing from an earlier one says nothing


def test_a_resumed_run_that_fails_again_stamps_the_fail_with_the_checks_it_took(world: World) -> None:
    fail_then_fix(world)
    world.failing["pre-commit"] = ["hook failed"]
    assert world.check() == 1
    stamp = world.stamp
    assert stamp.result == "fail" and stamp.detail["failed"] == ["pre-commit"]
    assert stamp.detail["checks"] == ["docs-check", "mkdocs", "pre-commit"]
    assert stamp.detail["resumed"]["checks"] == ["docs-check"]
    world.failing.clear()
    world.calls.clear()
    assert world.check() == 0
    assert world.calls == ["pre-commit", "pytest"]  # mkdocs passed in the second run, docs-check in the first
    assert world.stamp.detail["resumed"]["checks"] == ["docs-check", "mkdocs"]


def test_the_counts_of_a_check_taken_from_the_earlier_run_reach_the_stamp(world: World) -> None:
    world.counts["pytest"] = {"passed": 7276, "skipped": 11}
    world.edit("python/src/coney_tools/widget.py", "x = 1\n")
    world.failing["actionlint"] = ["bad"]
    world.edit(".github/workflows/widget.yml", "name: widget\n")
    assert world.check() == 1
    world.failing.clear()
    world.calls.clear()
    assert world.check() == 0
    assert world.calls == ["actionlint"]
    stamp = world.stamp
    assert stamp.detail["pytest"] == {"passed": 7276, "skipped": 11} and stamp.detail["pytest_scope"] == "whole"
    assert stamp.detail["resumed"]["checks"] == FULL_PYTHON_BEFORE_ACTIONLINT


FULL_PYTHON_BEFORE_ACTIONLINT = ["docs-check", "mkdocs", "pre-commit", "ruff", "mypy", "pytest"]


def test_a_run_that_gave_up_waiting_for_a_slot_keeps_what_passed(world: World) -> None:
    world.edit("python/src/coney_tools/widget.py", "x = 1\n")
    world.hold_timeout = SlotTimeout("no check slot free after 180 minutes; held by bob (pytest)", 2, 3)
    assert world.check() == 75
    world.hold_timeout = None
    world.calls.clear()
    assert world.check() == 0
    assert world.calls == ["pytest"]
    assert world.stamp.detail["resumed"]["checks"] == ["docs-check", "mkdocs", "pre-commit", "ruff", "mypy"]


def test_a_run_that_cannot_run_a_check_keeps_what_passed_before_it(world: World) -> None:
    world.edit("python/src/coney_tools/widget.py", "x = 1\n")
    world.unavailable.add("mypy")
    assert world.check() == 2
    world.unavailable.clear()
    world.calls.clear()
    assert world.check() == 0
    assert world.calls == ["mypy", "pytest"]


def test_keep_going_keeps_the_checks_that_passed_after_a_failure_too(world: World) -> None:
    world.edit("docs/guides/widget.md", DOC)
    world.failing["mkdocs"] = ["WARNING"]
    assert world.check(keep_going=True) == 1
    world.failing.clear()
    world.calls.clear()
    assert world.check() == 0
    assert world.calls == ["mkdocs"]


# --- what does not resume -----------------------------------------------------------------------------------------


def test_a_changed_tree_runs_everything(world: World) -> None:
    fail_then_fix(world)
    world.edit("docs/guides/widget.md", "# Widget\n\nThree.\n")
    assert world.check() == 0
    assert world.calls == FULL_DOC and "resumed" not in world.stamp.detail


def test_a_commit_that_changes_nothing_in_the_tree_still_runs_everything(world: World) -> None:
    fail_then_fix(world)
    git(world.root, "commit", "-q", "--allow-empty", "-m", "nothing")  # another tip, the same change
    assert world.check() == 0
    assert world.calls == FULL_DOC


def test_a_new_base_runs_everything_though_the_change_is_the_same(world: World) -> None:
    fail_then_fix(world)
    before = world.change().sha256
    git(world.root, "checkout", "-q", "main")
    world.edit("docs/guides/other.md", "# Other\n", "main moves")
    git(world.root, "checkout", "-q", "work")
    git(world.root, "rebase", "-q", "main")
    assert world.change().sha256 == before  # the change id survives the rebase, the checks' ground does not
    assert world.check() == 0
    assert world.calls == FULL_DOC


def test_another_profile_runs_everything(world: World) -> None:
    fail_then_fix(world)
    assert world.check(profile="tooling") == 0
    assert world.calls == ["docs-check", "mkdocs", "pre-commit", "ruff", "mypy", "pytest"]


def test_another_disc_setting_runs_everything(world: World) -> None:
    world.edit("docs/guides/widget.md", DOC)
    disc = ["the environment: D:/games/a.iso"]
    env = world.env()
    env.disc_key = lambda root: disc[0]
    world.failing["mkdocs"] = ["WARNING"]
    assert task_check.run_check(world.request(), env) == 1
    world.failing.clear()
    world.calls.clear()
    disc[0] = "game_dir in coney.local.toml: D:/games/b.iso"
    assert task_check.run_check(world.request(), env) == 0
    assert world.calls == FULL_DOC


def test_the_same_disc_setting_resumes(world: World) -> None:
    world.edit("docs/guides/widget.md", DOC)
    env = world.env()
    env.disc_key = lambda root: "the environment: D:/games/a.iso"
    world.failing["mkdocs"] = ["WARNING"]
    assert task_check.run_check(world.request(), env) == 1
    world.failing.clear()
    world.calls.clear()
    assert task_check.run_check(world.request(), env) == 0
    assert world.calls == ["mkdocs", "pre-commit", "pytest"]


def test_the_rules_the_checks_read_are_part_of_the_key(world: World) -> None:
    conventions = load_conventions(world.root)
    same = check_resume.rules_fingerprint(conventions)
    assert same == check_resume.rules_fingerprint(load_conventions(world.root))
    wider = replace(conventions, proof=replace(conventions.proof, pytest=replace(conventions.proof.pytest, scoped=())))
    assert check_resume.rules_fingerprint(wider) != same
    docs_gone = replace(conventions, paths=replace(conventions.paths, docs_only=()))
    assert check_resume.rules_fingerprint(docs_gone) != same


def rules(world: World, monkeypatch: pytest.MonkeyPatch, change: Callable[[Conventions], Conventions]) -> None:
    """Make the next runs read the rules `change` makes of the base's, leaving the base commit as it is."""
    original = task_check._rules

    def read(root: Path, base: str, own: Conventions, say: Callable[[str], None]) -> tuple[Conventions, str]:
        found, which = original(root, base, own, say)
        return change(found), which

    monkeypatch.setattr(task_check, "_rules", read)


def fail_with_rules_then_change(
    world: World, monkeypatch: pytest.MonkeyPatch, change: Callable[[Conventions], Conventions]
) -> None:
    """A run of the docs profile that fails at mkdocs, then the rules the checks read change, the base commit not."""
    fail_then_fix(world)
    rules(world, monkeypatch, change)


def test_proof_rules_that_change_on_the_same_base_run_everything(world: World, monkeypatch: pytest.MonkeyPatch) -> None:
    def wider(c: Conventions) -> Conventions:
        return replace(c, proof=replace(c.proof, ctest=replace(c.proof.ctest, max_files=7)))

    fail_with_rules_then_change(world, monkeypatch, wider)
    assert world.check() == 0
    assert world.calls == FULL_DOC and any("another rules" in line for line in world.said)


def test_path_classes_that_change_on_the_same_base_run_everything(
    world: World, monkeypatch: pytest.MonkeyPatch
) -> None:
    def more(c: Conventions) -> Conventions:
        return replace(c, paths=replace(c.paths, tools_only=(*c.paths.tools_only, "zzz/**")))

    fail_with_rules_then_change(world, monkeypatch, more)
    assert world.check() == 0
    assert world.calls == FULL_DOC


def test_a_profile_whose_checks_list_changed_runs_everything(world: World, monkeypatch: pytest.MonkeyPatch) -> None:
    def fewer(c: Conventions) -> Conventions:
        profiles = tuple(
            replace(p, checks=tuple(x for x in p.checks if x != "pre-commit")) if p.name == "docs" else p
            for p in c.proof.profiles
        )
        return replace(c, proof=replace(c.proof, profiles=profiles))

    fail_with_rules_then_change(world, monkeypatch, fewer)
    assert world.check() == 0
    assert world.calls == ["docs-check", "mkdocs", "pytest"]  # all of them, minus the check the rules dropped
    assert any("another" in line and "checks" in line for line in world.said)


def test_proof_rules_that_change_on_the_base_run_everything(world: World) -> None:
    fail_then_fix(world)
    git(world.root, "checkout", "-q", "main")
    path = world.root / ".github" / "conventions.yml"
    data = yaml.safe_load(path.read_text(encoding="utf-8"))
    data["proof"]["pytest"]["whole_when"].append("docs/")
    path.write_text(yaml.safe_dump(data), encoding="utf-8", newline="")
    git(world.root, "commit", "-qam", "main moves on")
    git(world.root, "checkout", "-q", "work")
    git(world.root, "rebase", "-q", "main")
    assert world.check() == 0
    assert world.calls == FULL_DOC


def writes(path: Path) -> Callable[[], None]:
    """A step's side effect: a file a check leaves in the worktree."""

    def write() -> None:
        path.write_text("made by a check", encoding="utf-8")

    return write


def test_a_worktree_that_changed_while_a_check_ran_leaves_no_pass_for_a_later_run_to_take(world: World) -> None:
    world.edit("docs/guides/widget.md", DOC)
    stray = world.root / "stray.txt"
    world.on_step["mkdocs"] = writes(stray)
    assert world.check() == 2
    assert any("worktree changed while the checks ran" in line for line in world.said)
    stray.unlink()
    world.on_step.clear()
    world.calls.clear()
    assert world.check() == 0
    assert world.calls == ["mkdocs", "pre-commit", "pytest"]  # docs-check ran on the clean tree; mkdocs did not count
    assert world.stamp.detail["resumed"]["checks"] == ["docs-check"]


def test_a_change_made_by_the_last_check_leaves_nothing_to_resume(world: World) -> None:
    world.edit("docs/guides/widget.md", DOC)
    stray = world.root / "stray.txt"
    world.on_step["pytest"] = writes(stray)
    assert world.check() == 2
    stray.unlink()
    world.on_step.clear()
    world.calls.clear()
    assert world.check() == 0
    assert world.calls == ["pytest"]


def test_an_entry_dated_after_the_run_began_is_not_used(world: World) -> None:
    fail_then_fix(world)
    data = json.loads(ledger_file(world).read_text(encoding="utf-8"))
    for entry in data["entries"].values():
        entry["at"] = "2099-01-01T00:00:00Z"
    ledger_file(world).write_text(json.dumps(data), encoding="utf-8")
    assert world.check() == 0
    assert world.calls == FULL_DOC and any("after this run began" in line for line in world.said)


def test_no_resume_runs_everything_and_the_next_run_can_resume_from_it(world: World) -> None:
    fail_then_fix(world)
    world.failing["pre-commit"] = ["hook failed"]
    assert world.check(resume=False) == 1
    assert world.calls == ["docs-check", "mkdocs", "pre-commit"]
    world.failing.clear()
    world.calls.clear()
    assert world.check() == 0
    assert world.calls == ["pre-commit", "pytest"]


def test_a_passing_run_clears_what_it_kept_so_the_next_run_runs_everything(world: World) -> None:
    world.edit("docs/guides/widget.md", DOC)
    assert world.check() == 0
    assert not ledger_file(world).exists()
    world.calls.clear()
    assert world.check() == 0
    assert world.calls == FULL_DOC and "resumed" not in world.stamp.detail


def test_what_passed_for_another_change_is_dropped_not_added_to(world: World) -> None:
    fail_then_fix(world)
    world.edit("docs/guides/widget.md", "# Widget\n\nThree.\n")
    world.failing["mkdocs"] = ["WARNING"]
    assert world.check() == 1
    world.failing.clear()
    world.calls.clear()
    assert world.check() == 0
    assert world.calls == ["mkdocs", "pre-commit", "pytest"]
    assert world.stamp.detail["resumed"]["checks"] == ["docs-check"]  # of the second run only


def test_a_ledger_older_than_the_limit_is_not_used(world: World) -> None:
    fail_then_fix(world)
    hours = load_conventions(world.root).proof.resume.max_age_hours
    assert hours > 0
    world.clock.now += timedelta(hours=hours, minutes=1)
    assert world.check() == 0
    assert world.calls == FULL_DOC and any("older than" in line for line in world.said)


def test_a_ledger_that_cannot_be_read_is_ignored_and_said(world: World) -> None:
    fail_then_fix(world)
    ledger_file(world).write_text("{not json", encoding="utf-8")
    assert world.check() == 0
    assert world.calls == FULL_DOC and any("resume: ignoring" in line for line in world.said)


# --- the build ----------------------------------------------------------------------------------------------------


def test_a_build_that_passed_is_not_built_again_and_its_record_is_the_one_the_stamp_covers(world: World) -> None:
    world.edit("src/ai/brain.cpp", BRAIN)
    world.failing["pytest"] = ["a test"]
    assert world.check(keep_going=True) == 1  # pytest failed, the build ran after it and passed
    assert len(world.builds) == 1
    world.failing.clear()
    world.calls.clear()
    assert world.check() == 0
    assert world.calls == ["pytest"] and len(world.builds) == 1
    stamp = world.stamp
    [record] = (unit.Unit.of(world.root, TASK).dir / "build-runs").glob("*.json")
    assert stamp.covers["build_record_sha256"] == record.stem and stamp.detail["tests"] == TESTS
    assert stamp.detail["ctest_scope"] == "whole" and stamp.detail["preset"] == "ci"
    # the clang-tidy check after the build passed in the first run too, so it is taken as well
    assert stamp.detail["resumed"]["checks"][-3:] == ["msvc-ci-build", "ctest-full-disc", "clang-tidy"]


def test_a_build_whose_tests_failed_is_built_again_with_the_checks_before_it_resumed(world: World) -> None:
    world.edit("src/ai/brain.cpp", BRAIN)
    world.build_tests = {"total": 412, "passed": 411, "failed": 1, "skipped": 0, "skipped_disc": 0}
    assert world.check() == 1
    world.build_tests = dict(TESTS)
    world.calls.clear()
    assert world.check() == 0
    # the build and its tests run as one, so both again; the clang-tidy check after them had not run yet
    assert world.calls == ["clang-tidy"] and len(world.builds) == 2
    assert world.stamp.detail["resumed"]["checks"] == [
        "docs-check",
        "mkdocs",
        "pre-commit",
        "ruff",
        "mypy",
        "pytest",
    ]


def test_a_build_record_that_is_gone_is_built_again(world: World) -> None:
    world.edit("src/ai/brain.cpp", BRAIN)
    world.failing["pytest"] = ["a test"]
    assert world.check(keep_going=True) == 1
    shutil.rmtree(unit.Unit.of(world.root, TASK).dir / "build-runs")
    world.failing.clear()
    assert world.check() == 0
    assert len(world.builds) == 2
    # the clang-tidy check after the build passed in the first run and does not hang on the build record
    assert world.stamp.detail["resumed"]["checks"] == [
        "docs-check",
        "mkdocs",
        "pre-commit",
        "ruff",
        "mypy",
        "clang-tidy",
    ]


def test_a_build_that_gave_up_waiting_leaves_the_checks_before_it_resumable(world: World) -> None:
    world.edit("src/ai/brain.cpp", BRAIN)
    world.build_exit = 75
    assert world.check() == 75
    world.build_exit = 0
    world.calls.clear()
    assert world.check() == 0
    assert world.calls == ["clang-tidy"] and len(world.builds) == 2


# --- around it ----------------------------------------------------------------------------------------------------


def test_a_dry_run_says_what_would_resume_and_changes_nothing(world: World) -> None:
    fail_then_fix(world)
    before = ledger_file(world).read_bytes()
    assert world.check(dry_run=True) == 0
    assert any("would skip docs-check" in line for line in world.said) and world.calls == []
    assert ledger_file(world).read_bytes() == before


def test_a_fix_task_keeps_its_ledger_in_its_parents_folder(world: World) -> None:
    world.edit("docs/guides/widget.md", DOC)
    start_fix(world.root, "757-fix", TASK)
    world.failing["mkdocs"] = ["WARNING"]
    assert world.check(task="757-fix") == 1
    assert ledger_file(world).is_file()
    assert not (unit.Unit.of(world.root, "757-fix").dir / check_resume.FOLDER).exists()
    world.failing.clear()
    world.calls.clear()
    assert world.check(task="757-fix") == 0
    assert world.calls == ["mkdocs", "pre-commit", "pytest"]


def test_the_progress_file_counts_the_checks_that_were_taken(world: World) -> None:
    fail_then_fix(world)
    assert world.check() == 0
    record = json.loads((unit.Unit.of(world.root, TASK).dir / "check-progress.json").read_text(encoding="utf-8"))
    assert record["state"] == "passed" and record["total"] == 4 and record["index"] == 4


def test_the_facts_of_the_description_say_the_checks_were_resumed(world: World) -> None:
    from coney_tools import pr_text

    fail_then_fix(world)
    assert world.check() == 0
    assert "resumed" in pr_text.resumed_words(world.stamp.detail)
    assert pr_text.resumed_words({"checks": ["a"]}) == ""


# --- the command line ---------------------------------------------------------------------------------------------


def test_the_flag_is_wired_and_a_detached_child_gets_it(
    world: World, monkeypatch: pytest.MonkeyPatch, tmp_path: Path
) -> None:
    from coney_tools.cli import _build_parser as build_parser

    parsed = build_parser().parse_args(["task", "check", "--no-resume"])
    assert parsed.no_resume and not build_parser().parse_args(["task", "check"]).no_resume
    command = task_check.child_command(parsed, tmp_path, tmp_path / "r.json")
    assert "--no-resume" in command
    assert "--no-resume" not in task_check.child_command(
        build_parser().parse_args(["task", "check"]), tmp_path, tmp_path / "r.json"
    )
    seen: list[Any] = []
    monkeypatch.chdir(world.root)
    monkeypatch.setattr(task_check, "default_env", world.env)

    def record(request: task_check.Request, env: task_check.Env) -> int:
        seen.append(request.resume)
        return 0

    monkeypatch.setattr(task_check, "run_check", record)
    assert main(["task", "check", TASK, "--agent", "lane-a", "--base", "main", "--no-resume"]) == 0
    assert main(["task", "check", TASK, "--agent", "lane-a", "--base", "main"]) == 0
    assert seen == [False, True]


# --- the rule -----------------------------------------------------------------------------------------------------


def test_the_age_limit_is_a_rule_of_the_conventions_file_with_a_default_for_an_older_base() -> None:
    assert conventions_with().proof.resume.max_age_hours == 24
    assert conventions_with(("proof.resume.max_age_hours", 6)).proof.resume.max_age_hours == 6
    assert conventions_with(("proof.resume", REMOVE)).proof.resume.max_age_hours == 24
    for bad in (0, -1, "a day", True):
        with pytest.raises(ConventionsError, match="max_age_hours"):
            parse_conventions(changed(("proof.resume.max_age_hours", bad)))
    with pytest.raises(ConventionsError, match="resume"):
        parse_conventions(changed(("proof.resume.days", 1), ("proof.resume.max_age_hours", REMOVE)))
