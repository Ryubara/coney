# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for the `unit-ready` event (docs/guides/conclave.md#watchers): it fires when `pr open` would pass the proof
and not before, and never for a unit the coordinator holds.

The units are real: `pr_support` builds a git checkout with a branch, a task file and a stamp log in which every stage
has passed, and each test breaks one stage. Only `gh` is made up (it says no branch has a pull request)."""

import itertools
import os
from pathlib import Path
from typing import Any

import pytest
from conclave_support import NOW, FakeGh, Layout, github_answers, sources
from git_support import git
from pr_support import PARAGRAPH, PR_TEXT, Built, make

from coney_tools import pr_proof, pr_ready, pr_text
from coney_tools.conclave_snapshot import Sources, collect
from coney_tools.pr_proof import ProofResult
from coney_tools.task_file import parse_task_file

HOLD = "HOLD.md"
#: Numbers the made-up workspaces of a test that reads the snapshot more than once.
_WORKSPACES = itertools.count()


def over(built: Built, tmp_path: Path, **changes: Any) -> Sources:
    """Conclave's sources over the made-up unit: its checkout is the unit's worktree and its scratch folder is the
    unit's, and `gh` says that no branch has a pull request."""
    answers = github_answers()
    answers[("pr", "list", "--state", "all")] = []
    answers[("pr", "list", "--state", "open")] = []
    base = sources(Layout(tmp_path / f"conclave-{next(_WORKSPACES)}"), FakeGh(answers), **changes)
    base.coord.scratch = built.unit.dir.parent
    base.coord.worktrees = lambda: [(built.root, built.task)]
    base.coord.health = lambda pulls, required, now: {"stuck": [], "other": [], "notices": []}
    base.ready = lambda root, unit, text, conventions, now: pr_ready.readiness(root, unit, text, conventions, now=now)
    return base


def ready_events(built: Built, tmp_path: Path, **changes: Any) -> list[dict[str, Any]]:
    """The `unit-ready` events a GitHub snapshot of the made-up unit raises."""
    report = collect(over(built, tmp_path, **changes), github=True)
    assert report["events"]["error"] is None
    found: list[dict[str, Any]] = report["events"]["events"]
    return [event for event in found if event["kind"] == "unit-ready"]


def proof(built: Built) -> ProofResult:
    """What `pr verify` says of the unit as it is now."""
    return built.verify()


def text_ok(built: Built) -> bool:
    """Whether `pr text` accepts the unit's description as it is now."""
    subject = built.subject()
    form = pr_text.read_engineer_file(built.unit.dir / "PR.md", subject.conventions)
    task = parse_task_file((built.unit.dir / "TASK.md").read_text(encoding="utf-8"), subject.conventions.tasks)
    return pr_text.generate(subject, task, form, keys={}, trailer_models=[]).ok


# --- the four cases of the issue ------------------------------------------------------------------------------------


def test_a_unit_with_a_pr_file_and_no_check_stamp_raises_no_event(tmp_path: Path) -> None:
    built = make(tmp_path / "unit", shape="tooling", skip=frozenset({"checked"}))
    assert (built.unit.dir / "PR.md").is_file()
    assert ready_events(built, tmp_path) == []


def test_a_unit_with_a_check_but_no_review_raises_no_event(tmp_path: Path) -> None:
    built = make(tmp_path / "unit", shape="tooling", skip=frozenset({"reviewed"}))
    assert ready_events(built, tmp_path) == []


def test_a_unit_whose_stamps_cover_its_change_raises_exactly_one_event(tmp_path: Path) -> None:
    built = make(tmp_path / "unit", shape="tooling")
    [event] = ready_events(built, tmp_path)
    assert event["state"] == "open" and event["guard"] is False and event["kind"] == "unit-ready"
    assert event["ref"] == {"pr": None, "task": "work", "branch": "work"}
    assert event["text"] == "work: PR.md is ready for pr open" and event["do"] == "coney-tools pr open work"
    # the id names the change the stamps cover, not the file's time
    assert event["id"] == f"unit:work:PR.md:{built.diff[:12]}"


def test_a_unit_the_coordinator_holds_raises_no_event_until_the_hold_is_lifted(tmp_path: Path) -> None:
    built = make(tmp_path / "unit", shape="tooling")
    marker = built.unit.dir / HOLD
    marker.write_text("waiting for capacity\n", encoding="utf-8")
    assert ready_events(built, tmp_path) == []
    marker.unlink()
    assert len(ready_events(built, tmp_path)) == 1


# --- what the stamps must cover -------------------------------------------------------------------------------------


def test_a_review_that_a_new_commit_made_stale_raises_no_event(tmp_path: Path) -> None:
    built = make(tmp_path / "unit", shape="tooling")
    built.edit("python/src/coney_tools/widget.py", '"""A widget, changed."""\n')
    assert ready_events(built, tmp_path) == []


def test_a_failed_review_raises_no_event(tmp_path: Path) -> None:
    built = make(tmp_path / "unit", shape="tooling", skip=frozenset({"reviewed"}))
    built.review(verdict="fail", blockers=2)
    assert ready_events(built, tmp_path) == []


def test_a_worktree_with_uncommitted_changes_raises_no_event(tmp_path: Path) -> None:
    built = make(tmp_path / "unit", shape="tooling")
    (built.root / "notes.txt").write_text("not committed\n", encoding="utf-8")
    assert ready_events(built, tmp_path) == []


def test_a_feature_unit_with_every_stage_passed_raises_one_event(tmp_path: Path) -> None:
    built = make(tmp_path / "unit", shape="feature")
    assert len(ready_events(built, tmp_path)) == 1


def test_a_feature_unit_whose_function_changed_after_it_was_compared_raises_no_event(tmp_path: Path) -> None:
    built = make(tmp_path / "unit", shape="feature")
    path = built.root / "src/ai/widget.cpp"
    built.edit("src/ai/widget.cpp", path.read_text(encoding="utf-8").replace("update", "update ", 1) + "\n")
    assert ready_events(built, tmp_path) == []


def test_a_unit_that_is_not_a_task_raises_no_event(tmp_path: Path) -> None:
    built = make(tmp_path / "unit", shape="tooling")
    (built.unit.dir / "TASK.md").unlink()
    assert ready_events(built, tmp_path) == []


def test_a_unit_whose_branch_has_a_pull_request_raises_no_event(tmp_path: Path) -> None:
    built = make(tmp_path / "unit", shape="tooling")
    src = over(built, tmp_path)
    src.coord.gh = FakeGh(
        {
            **github_answers(),
            ("pr", "list", "--state", "all"): [{"number": 5, "headRefName": "work", "state": "OPEN", "closedAt": None}],
            ("pr", "list", "--state", "open"): [],
        }
    )
    found = collect(src, github=True)["events"]["events"]
    assert [e for e in found if e["kind"] == "unit-ready"] == []


# --- told once ------------------------------------------------------------------------------------------------------


def test_an_edit_of_the_pr_file_that_the_review_still_covers_is_the_same_event(tmp_path: Path) -> None:
    built = make(tmp_path / "unit", shape="tooling")
    [first] = ready_events(built, tmp_path)
    path = built.unit.dir / "PR.md"
    text = path.read_text(encoding="utf-8")
    # the Issues affected section is not part of what a review covers, and the file's time moves on
    path.write_text(
        text.replace("None (no open issue is affected)", "None (nothing else is affected)"), encoding="utf-8"
    )
    os.utime(path, (NOW.timestamp(), NOW.timestamp()))
    [again] = ready_events(built, tmp_path)
    assert again["id"] == first["id"]


def test_a_touched_pr_file_is_the_same_event(tmp_path: Path) -> None:
    built = make(tmp_path / "unit", shape="tooling")
    [first] = ready_events(built, tmp_path)
    path = built.unit.dir / "PR.md"
    os.utime(path, (NOW.timestamp() + 600, NOW.timestamp() + 600))
    assert ready_events(built, tmp_path)[0]["id"] == first["id"]


def test_an_edit_of_the_summary_the_review_covered_is_not_ready_until_it_is_reviewed_again(tmp_path: Path) -> None:
    built = make(tmp_path / "unit", shape="tooling")
    path = built.unit.dir / "PR.md"
    path.write_text(
        path.read_text(encoding="utf-8").replace(PARAGRAPH, PARAGRAPH + " It also waits."), encoding="utf-8"
    )
    assert ready_events(built, tmp_path) == []
    # the reviewer covers the new text: ready again, for the same change
    built.review()
    assert len(ready_events(built, tmp_path)) == 1


def test_a_rebase_that_keeps_the_change_keeps_the_event(tmp_path: Path) -> None:
    built = make(tmp_path / "unit", shape="tooling")
    [first] = ready_events(built, tmp_path)
    # main moves on (a file the branch does not touch) and the branch is rebased onto it: its change is the same
    git(built.root, "checkout", "-q", "main")
    (built.root / "other.txt").write_text("main moved\n", encoding="utf-8")
    git(built.root, "add", "-A")
    git(built.root, "commit", "-q", "-m", "main moves")
    git(built.root, "push", "-q", "origin", "main")
    git(built.root, "checkout", "-q", "work")
    git(built.root, "rebase", "-q", "main")
    [again] = ready_events(built, tmp_path)
    assert again["id"] == first["id"]


def test_a_second_description_file_the_review_covers_is_a_second_event_that_names_it(tmp_path: Path) -> None:
    built = make(tmp_path / "unit", shape="tooling")
    second = built.unit.dir / "PR-2.md"
    second.write_text((built.unit.dir / "PR.md").read_text(encoding="utf-8"), encoding="utf-8")
    found = ready_events(built, tmp_path)
    assert [event["id"].split(":")[2] for event in found] == ["PR.md", "PR-2.md"]
    assert found[1]["do"] == f"coney-tools pr open work --text {second}"


def test_a_second_description_file_the_review_did_not_cover_is_not_ready(tmp_path: Path) -> None:
    built = make(tmp_path / "unit", shape="tooling")
    (built.unit.dir / "PR-2.md").write_text(PR_TEXT, encoding="utf-8")
    assert [event["id"].split(":")[2] for event in ready_events(built, tmp_path)] == ["PR.md"]


# --- the same test as pr verify and pr text -------------------------------------------------------------------------


@pytest.mark.parametrize(
    "skip",
    [frozenset(), frozenset({"checked"}), frozenset({"reviewed"}), frozenset({"fails-first"}), frozenset({"start"})],
)
def test_ready_is_what_pr_verify_and_pr_text_accept(tmp_path: Path, skip: frozenset[str]) -> None:
    built = make(tmp_path / "unit", shape="tooling", skip=skip - {"start"}, start="start" not in skip)
    accepted = proof(built).ok and text_ok(built)
    answer = pr_ready.readiness(built.root, built.unit, built.unit.dir / "PR.md", built.conventions, now=NOW)
    assert answer.ready is accepted
    assert (answer.reason == "") is accepted


def test_the_readiness_names_the_first_refusal_and_the_change(tmp_path: Path) -> None:
    built = make(tmp_path / "unit", shape="tooling", skip=frozenset({"reviewed"}))
    answer = pr_ready.readiness(built.root, built.unit, built.unit.dir / "PR.md", built.conventions, now=NOW)
    assert answer.ready is False and answer.reason.startswith("reviewed: not reviewed")
    stale = make(tmp_path / "other", shape="tooling")
    stale.edit("python/src/coney_tools/widget.py", '"""Changed."""\n')
    answer = pr_ready.readiness(stale.root, stale.unit, stale.unit.dir / "PR.md", stale.conventions, now=NOW)
    assert answer.ready is False and answer.change == stale.diff


# --- not being able to tell -----------------------------------------------------------------------------------------


def test_a_checkout_whose_default_branch_is_unknown_marks_the_kind_unread_and_raises_nothing(tmp_path: Path) -> None:
    built = make(tmp_path / "unit", shape="tooling")
    git(built.root, "remote", "set-head", "origin", "-d")
    section = collect(over(built, tmp_path), github=True)["events"]
    assert "unit-ready" in section["unread"]
    assert [e for e in section["events"] if e["kind"] == "unit-ready"] == []


def test_a_unit_that_stamps_say_is_not_ready_is_not_read_from_git(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    """The cheap refusals come from the stamp log alone, so twenty units waiting on a check cost no git."""
    built = make(tmp_path / "unit", shape="tooling", skip=frozenset({"checked"}))

    def refuse(*args: object, **more: object) -> None:
        raise AssertionError("the unit's git was read")

    monkeypatch.setattr(pr_proof, "load_subject", refuse)
    assert ready_events(built, tmp_path) == []


def test_reading_the_snapshot_writes_nothing_to_the_unit(tmp_path: Path) -> None:
    built = make(tmp_path / "unit", shape="tooling")
    before = sorted((p.relative_to(tmp_path).as_posix(), p.stat().st_mtime_ns) for p in (tmp_path / "unit").rglob("*"))
    ready_events(built, tmp_path)
    after = sorted((p.relative_to(tmp_path).as_posix(), p.stat().st_mtime_ns) for p in (tmp_path / "unit").rglob("*"))
    assert after == before


def test_a_unit_whose_stamps_are_for_another_change_does_not_read_its_functions(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    """Reading every `@orig` function takes seconds; a stale Check or Review is known without it."""
    built = make(tmp_path / "unit", shape="feature")
    built.edit("src/ai/widget.cpp", (built.root / "src/ai/widget.cpp").read_text(encoding="utf-8") + "// more\n")

    def scan(*args: object) -> None:
        raise AssertionError("the functions were read")

    monkeypatch.setattr(pr_proof, "changed_functions", scan)
    answer = pr_ready.readiness(built.root, built.unit, built.unit.dir / "PR.md", built.conventions, now=NOW)
    assert answer.ready is False and answer.reason.startswith("checked: stale: the branch changed since Check")
    assert answer.change == built.diff


def test_a_unit_whose_stamps_cover_its_change_has_its_functions_read_once(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    built = make(tmp_path / "unit", shape="feature")
    calls: list[str] = []
    real = pr_proof.changed_functions

    def counted(*args: Any) -> Any:
        calls.append("read")
        return real(*args)

    monkeypatch.setattr(pr_proof, "changed_functions", counted)
    answer = pr_ready.readiness(built.root, built.unit, built.unit.dir / "PR.md", built.conventions, now=NOW)
    assert answer.ready is True and calls == ["read"]
