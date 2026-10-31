# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for the events of the Conclave snapshot (docs/guides/conclave.md#watchers): a failed check, a merge, a unit
ready for `pr open` and a question, each with a stable id and a handled state, on made-up snapshots; no network."""

import os
from datetime import timedelta
from pathlib import Path
from typing import Any

import pytest
from conclave_support import (
    CHANGE,
    CONVENTIONS,
    NOW,
    FakeGh,
    Layout,
    ago,
    drive,
    github_answers,
    iso,
    make_layout,
    sources,
    watching,
)

from coney_tools import pr_hold
from coney_tools.conclave_snapshot import collect
from coney_tools.pr_hold import Hold
from coney_tools.pr_ready import Readiness
from coney_tools.watched_drives import Watch

RULES = CONVENTIONS.conclave.watch


@pytest.fixture
def layout(tmp_path: Path) -> Layout:
    """Two units, `fix-frame-812` (a pull request with a failed check) and `fix-new`, each with a worktree."""
    return make_layout(tmp_path)


def events(report: dict[str, Any], *kinds: str) -> list[dict[str, Any]]:
    """The events of a report, of these kinds when given."""
    found: list[dict[str, Any]] = report["events"]["events"]
    return [event for event in found if not kinds or event["kind"] in kinds]


def read(
    layout: Layout, answers: dict[tuple[str, ...], Any] | None = None, plain: tuple[str, ...] = (), **changes: Any
) -> dict[str, Any]:
    """A GitHub snapshot over the layout; `plain` names more worktrees whose branch has no task file."""
    return collect(sources(layout, FakeGh(answers or github_answers()), plain, **changes), github=True)


def hold_on(layout: Layout, pr: int = 812, hours: float = 4, **changes: Any) -> None:
    """A hold on the pull request, made now (the snapshot's clock)."""
    base: dict[str, Any] = {"who": "engineer-2", "cause": None, "note": "fixing it"}
    base.update(changes)
    pr_hold.write(
        pr_hold.holds_folder(layout.scratch, RULES), Hold(pr=pr, until=NOW + timedelta(hours=hours), at=NOW, **base)
    )


def write_file(path: Path, text: str = "x\n", when: float = 1.0) -> Path:
    """Write a file whose last change was `when` hours before NOW."""
    path.write_text(text, encoding="utf-8")
    stamp = ago(hours=when).timestamp()
    os.utime(path, (stamp, stamp))
    return path


# --- the section ----------------------------------------------------------------------------------------------------


def test_a_local_snapshot_carries_no_events(layout: Layout) -> None:
    assert collect(sources(layout), github=False)["events"] is None


def test_the_snapshot_says_the_schema_that_has_events(layout: Layout) -> None:
    assert read(layout)["schema"] >= 2


def test_the_config_carries_the_watch_settings_the_mod_acts_by(layout: Layout) -> None:
    watch = read(layout)["config"]["watch"]
    assert watch["roles"] == ["coordinator"] and watch["preamble"] == RULES.preamble
    assert watch["github_seconds"] == RULES.github_seconds and watch["wake_gap_seconds"] == RULES.wake_gap_seconds
    assert watch["defer_typing_minutes"] == RULES.defer_typing_minutes
    assert watch["resend_minutes"] == RULES.resend_minutes and watch["stop_max_blocks"] == RULES.stop_max_blocks
    assert watch["stop_reset_minutes"] == RULES.stop_reset_minutes and watch["stop_preamble"] == RULES.stop_preamble


def test_every_event_has_the_same_fields(layout: Layout) -> None:
    write_file(layout.scratch / "fix-new" / "PR.md")
    write_file(layout.scratch / "fix-new" / "QUESTION.md", "Which step comes first?\n")
    found = events(read(layout))
    assert {event["kind"] for event in found} >= {"check-failed", "merged", "unit-ready", "question"}
    for event in found:
        assert set(event) == {"id", "kind", "state", "ref", "since", "text", "do", "guard", "handled_by"}
        assert event["state"] in {"open", "handled"} and event["id"] and event["text"]


# --- a failed check -------------------------------------------------------------------------------------------------


def test_a_failed_check_is_an_open_event_named_by_pull_request_cause_and_head(layout: Layout) -> None:
    [event] = events(read(layout), "check-failed")
    assert event["id"].startswith("pr:812:checks-failed:bbbbbbb")
    assert event["state"] == "open" and event["handled_by"] is None and event["guard"] is True
    assert event["ref"] == {"pr": 812, "task": None, "branch": "fix-frame-812"}
    assert "#812" in event["text"] and "build-ok" in event["text"]
    assert "pr hold 812" in event["do"]


def test_the_same_failure_read_again_has_the_same_id(layout: Layout) -> None:
    assert events(read(layout), "check-failed")[0]["id"] == events(read(layout), "check-failed")[0]["id"]


def test_a_new_push_is_a_new_event(layout: Layout) -> None:
    first = events(read(layout), "check-failed")[0]["id"]
    answers = github_answers()
    answers[("pr", "list", "--state", "open")][0]["headRefOid"] = "c" * 40
    assert events(read(layout, answers), "check-failed")[0]["id"] != first


def test_another_failing_check_on_the_same_head_is_a_new_event(layout: Layout) -> None:
    first = events(read(layout), "check-failed")[0]["id"]
    src = sources(layout, FakeGh(github_answers()))
    src.coord.health = lambda pulls, required, now: {
        "stuck": [{"number": 812, "branch": "fix-frame-812", "cause": "checks-failed", "checks": ["body", "build-ok"]}],
        "other": [],
        "notices": [],
    }
    assert events(collect(src, github=True), "check-failed")[0]["id"] != first


def test_a_hold_on_the_pull_request_handles_the_event_and_names_who(layout: Layout) -> None:
    hold_on(layout, hours=4, who="engineer-2")
    [event] = events(read(layout), "check-failed")
    assert event["state"] == "handled" and event["handled_by"] == "engineer-2 until " + iso(NOW + timedelta(hours=4))


def test_a_waiver_says_it_is_one(layout: Layout) -> None:
    hold_on(layout, who="waived", note="the runner pool is down")
    [event] = events(read(layout), "check-failed")
    assert event["state"] == "handled" and event["handled_by"].startswith("waived")


def test_a_hold_on_another_cause_does_not_handle_the_event(layout: Layout) -> None:
    hold_on(layout, cause="conflict")
    assert events(read(layout), "check-failed")[0]["state"] == "open"
    hold_on(layout, cause="checks-failed")
    assert events(read(layout), "check-failed")[0]["state"] == "handled"


def test_a_hold_on_another_pull_request_does_not_handle_the_event(layout: Layout) -> None:
    hold_on(layout, pr=813)
    assert events(read(layout), "check-failed")[0]["state"] == "open"


def test_a_hold_does_not_cover_a_new_push(layout: Layout) -> None:
    # pr.md#hold: a new push is a new problem, so a failing fix attempt is not silenced by the hold on the old head
    hold_on(layout, head="bbbbbbb")
    assert events(read(layout), "check-failed")[0]["state"] == "handled"
    answers = github_answers()
    answers[("pr", "list", "--state", "open")][0]["headRefOid"] = "c" * 40
    [event] = events(read(layout, answers), "check-failed")
    assert event["state"] == "open" and event["handled_by"] is None and event["guard"] is True


def test_a_hold_that_ran_out_raises_the_same_event_again(layout: Layout) -> None:
    hold_on(layout, hours=-1)
    [event] = events(read(layout), "check-failed")
    assert event["state"] == "open"
    hold_on(layout, hours=4)
    assert events(read(layout), "check-failed")[0]["id"] == event["id"]


def test_the_other_causes_are_pull_request_problems_that_guard_too(layout: Layout) -> None:
    src = sources(layout, FakeGh(github_answers()))
    src.coord.health = lambda pulls, required, now: {
        "stuck": [{"number": 812, "branch": "fix-frame-812", "cause": "conflict", "detail": "main"}],
        "other": [],
        "notices": [],
    }
    [event] = events(collect(src, github=True), "pr-stuck")
    assert event["id"].startswith("pr:812:conflict:") and event["guard"] is True and "conflict" in event["text"]


def test_a_pull_request_that_is_not_stuck_raises_nothing(layout: Layout) -> None:
    src = sources(layout, FakeGh(github_answers()))
    src.coord.health = lambda pulls, required, now: {"stuck": [], "other": [], "notices": []}
    assert events(collect(src, github=True), "check-failed", "pr-stuck") == []


# --- a merge --------------------------------------------------------------------------------------------------------


def test_a_recent_merge_is_news_once_and_never_a_guard(layout: Layout) -> None:
    [event] = events(read(layout), "merged")
    assert event["id"] == "merged:799" and event["state"] == "open" and event["guard"] is False
    assert event["since"] == iso(ago(minutes=4)) and "#799" in event["text"] and "add-task-start" in event["text"]


def test_a_merge_older_than_the_window_is_not_news(layout: Layout) -> None:
    answers = github_answers()
    answers[("pr", "list", "--state", "merged")][0]["mergedAt"] = iso(ago(minutes=RULES.merged_minutes + 1))
    assert events(read(layout, answers), "merged") == []


# --- a unit ready for pr open ---------------------------------------------------------------------------------------


def test_a_unit_with_a_pr_file_and_no_pull_request_is_ready(layout: Layout) -> None:
    write_file(layout.scratch / "fix-new" / "PR.md", when=2)
    [event] = events(read(layout), "unit-ready")
    assert event["id"] == f"unit:fix-new:PR.md:{CHANGE[:12]}" and event["guard"] is False
    assert event["since"] == iso(ago(hours=2))
    assert event["ref"]["task"] == "fix-new" and "pr open fix-new" in event["do"]


def test_a_unit_whose_branch_has_a_pull_request_is_not_ready(layout: Layout) -> None:
    write_file(layout.scratch / "fix-frame-812" / "PR.md", when=2)
    assert events(read(layout), "unit-ready") == []


def test_a_rewritten_pr_file_is_the_same_event_while_the_change_it_covers_is_the_same(layout: Layout) -> None:
    first = write_file(layout.scratch / "fix-new" / "PR.md", when=3)
    old = events(read(layout), "unit-ready")[0]
    write_file(first, "rewritten\n", when=1)
    [again] = events(read(layout), "unit-ready")
    assert again["id"] == old["id"] and again["since"] != old["since"]


def test_a_unit_whose_change_moved_on_is_a_new_event(layout: Layout) -> None:
    write_file(layout.scratch / "fix-new" / "PR.md", when=3)
    old = events(read(layout), "unit-ready")[0]["id"]
    moved = Readiness(True, "", "d" * 40)
    [again] = events(read(layout, ready=lambda root, unit, text, conventions, now: moved), "unit-ready")
    assert again["id"] != old


def test_a_unit_that_is_not_ready_raises_no_event_and_one_that_cannot_be_told_marks_the_kind_unread(
    layout: Layout,
) -> None:
    write_file(layout.scratch / "fix-new" / "PR.md", when=3)
    no = Readiness(False, "checked: no passing Check; run task check", "")
    assert events(read(layout, ready=lambda root, unit, text, conventions, now: no), "unit-ready") == []
    unknown = Readiness(False, "origin/HEAD is not set here", unknown=True)
    section = read(layout, ready=lambda root, unit, text, conventions, now: unknown)["events"]
    assert "unit-ready" in section["unread"] and [e for e in section["events"] if e["kind"] == "unit-ready"] == []


def test_a_unit_with_a_hold_file_raises_no_event_until_the_file_goes(layout: Layout) -> None:
    write_file(layout.scratch / "fix-new" / "PR.md", when=3)
    hold = write_file(layout.scratch / "fix-new" / RULES.unit_hold_file, "waiting for capacity\n")
    assert events(read(layout), "unit-ready") == []
    hold.unlink()
    assert len(events(read(layout), "unit-ready")) == 1


def test_several_units_of_one_branch_are_several_events(layout: Layout) -> None:
    write_file(layout.scratch / "fix-new" / "PR.md", when=2)
    write_file(layout.scratch / "fix-new" / "PR-2.md", when=1)
    assert [e["ref"]["task"] for e in events(read(layout), "unit-ready")] == ["fix-new", "fix-new"]


# --- a question -----------------------------------------------------------------------------------------------------


def test_a_question_file_is_an_open_event_with_its_first_line(layout: Layout) -> None:
    write_file(layout.scratch / "fix-new" / "QUESTION.md", "\n# Which step comes first?\n\nMore.\n", when=1)
    [event] = events(read(layout), "question")
    assert event["id"] == f"question:fix-new:QUESTION.md:{iso(ago(hours=1))}" and event["guard"] is False
    assert "fix-new" in event["text"] and "Which step comes first?" in event["text"] and "More" not in event["text"]
    assert "ANSWER.md" in event["do"]


def test_an_answer_newer_than_the_question_closes_it(layout: Layout) -> None:
    write_file(layout.scratch / "fix-new" / "QUESTION.md", "Which?\n", when=3)
    write_file(layout.scratch / "fix-new" / "ANSWER.md", "This.\n", when=2)
    assert events(read(layout), "question") == []


def test_an_answer_older_than_the_question_does_not(layout: Layout) -> None:
    write_file(layout.scratch / "fix-new" / "ANSWER.md", "This.\n", when=3)
    write_file(layout.scratch / "fix-new" / "QUESTION.md", "And again?\n", when=2)
    assert len(events(read(layout), "question")) == 1


def test_numbered_questions_are_answered_by_their_own_number(layout: Layout) -> None:
    write_file(layout.scratch / "fix-new" / "QUESTION-1.md", "One?\n", when=3)
    write_file(layout.scratch / "fix-new" / "QUESTION-2.md", "Two?\n", when=3)
    write_file(layout.scratch / "fix-new" / "ANSWER-1.md", "Yes.\n", when=2)
    [event] = events(read(layout), "question")
    assert "Two?" in event["text"] and "ANSWER-2.md" in event["do"]


def test_a_question_that_is_a_long_line_is_cut(layout: Layout) -> None:
    write_file(layout.scratch / "fix-new" / "QUESTION.md", "w" * 400 + "\n")
    assert len(events(read(layout), "question")[0]["text"]) < 260


def test_a_question_from_a_branch_without_a_task_file_is_an_event(layout: Layout) -> None:
    # most tracks are plain branches with a worktree and a scratch folder: they ask too, as they are released too
    layout.worktree("fix-plain-9")
    (layout.scratch / "fix-plain-9").mkdir()
    write_file(layout.scratch / "fix-plain-9" / "QUESTION.md", "Which file?\n", when=1)
    [event] = events(read(layout, plain=("fix-plain-9",)), "question")
    assert event["ref"]["task"] == "fix-plain-9" and "Which file?" in event["text"]


def test_a_question_in_a_folder_with_no_worktree_is_history(layout: Layout) -> None:
    (layout.scratch / "old-branch").mkdir()
    write_file(layout.scratch / "old-branch" / "QUESTION.md", "Long ago?\n", when=1)
    assert events(read(layout), "question") == []


def test_a_task_file_unit_asks_once_even_with_a_worktree(layout: Layout) -> None:
    write_file(layout.scratch / "fix-new" / "QUESTION.md", "Once?\n", when=1)
    assert len(events(read(layout), "question")) == 1


def test_other_files_in_a_unit_folder_are_no_questions(layout: Layout) -> None:
    write_file(layout.scratch / "fix-new" / "QUESTIONS.md")
    write_file(layout.scratch / "fix-new" / "notes-QUESTION.md")
    assert events(read(layout), "question") == []


# --- what could not be read -----------------------------------------------------------------------------------------


def test_a_pull_request_read_that_failed_marks_its_kinds_unread_and_raises_none_of_them(layout: Layout) -> None:
    answers = github_answers()
    answers[("pr", "list", "--state", "open")] = OSError("gh pr list failed")
    section = read(layout, answers)["events"]
    assert section["error"] is None
    assert {"check-failed", "pr-stuck", "merged"} <= set(section["unread"])
    assert [e for e in section["events"] if e["kind"] in {"check-failed", "pr-stuck", "merged"}] == []


def test_a_release_read_that_failed_marks_the_unit_kind_unread(layout: Layout) -> None:
    answers = github_answers()
    answers[("pr", "list", "--state", "all")] = OSError("gh pr list failed")
    assert "unit-ready" in read(layout, answers)["events"]["unread"]


def test_a_read_that_worked_leaves_nothing_unread(layout: Layout) -> None:
    assert read(layout)["events"]["unread"] == []


def test_a_hold_file_that_cannot_be_read_is_reported_and_handles_nothing(layout: Layout) -> None:
    folder = pr_hold.holds_folder(layout.scratch, RULES)
    folder.mkdir(parents=True)
    (folder / "812.json").write_text("{", encoding="utf-8")
    section = read(layout)["events"]
    assert section["holds_problems"] and "812.json" in section["holds_problems"][0]
    assert events(read(layout), "check-failed")[0]["state"] == "open"


def test_reading_the_snapshot_writes_nothing(layout: Layout) -> None:
    hold_on(layout)
    write_file(layout.scratch / "fix-new" / "PR.md")
    before = sorted((p.relative_to(layout.root).as_posix(), p.stat().st_mtime_ns) for p in layout.root.rglob("*"))
    read(layout)
    assert (
        sorted((p.relative_to(layout.root).as_posix(), p.stat().st_mtime_ns) for p in layout.root.rglob("*")) == before
    )


# --- a low drive ----------------------------------------------------------------------------------------------------


def low_drives(*free_gb: float) -> dict[str, Any]:
    """The `drives` source of a machine whose drives have this much free space."""
    return {"drives": lambda: watching(*free_gb)}


def test_a_drive_with_room_raises_no_disk_event(layout: Layout) -> None:
    assert events(read(layout, **low_drives(200, 61)), "disk-low") == []


def test_a_drive_under_its_floor_is_an_event_naming_it_and_the_cleanup(layout: Layout) -> None:
    [event] = events(read(layout, **low_drives(200, 3.7)), "disk-low")
    assert event["id"] == "disk-low:E:" and event["state"] == "open" and not event["guard"]
    assert "E:" in event["text"] and "3.7 GB" in event["text"] and "30 GB" in event["text"]
    assert "the build root (E:/coney_builds)" in event["text"]
    assert "workspace clean --apply" in event["do"]
    assert event["since"] == iso(NOW) and event["handled_by"] is None


def test_an_amber_drive_is_not_yet_an_event(layout: Layout) -> None:
    assert events(read(layout, **low_drives(35)), "disk-low") == []


def test_each_low_drive_is_its_own_event(layout: Layout) -> None:
    found = events(read(layout, **low_drives(10, 3.7, 80)), "disk-low")
    assert [event["id"] for event in found] == ["disk-low:C:", "disk-low:E:"]


def test_a_drive_is_low_by_its_own_floor(layout: Layout) -> None:
    def watch() -> Watch:
        return Watch([drive("C:", 61), drive("F:", 8, ("the watched folder (F:/x)",), floor=10)])

    assert [event["id"] for event in events(read(layout, drives=watch), "disk-low")] == ["disk-low:F:"]


def test_the_same_low_drive_keeps_its_id_as_the_free_space_changes(layout: Layout) -> None:
    first = events(read(layout, **low_drives(200, 3.7)), "disk-low")[0]["id"]
    assert events(read(layout, **low_drives(200, 2.1)), "disk-low")[0]["id"] == first


def test_a_disk_that_cannot_be_read_names_the_kind_unread(layout: Layout) -> None:
    def broken() -> Watch:
        raise OSError("no such drive")

    report = read(layout, drives=broken)
    assert "disk-low" in report["events"]["unread"] and events(report, "disk-low") == []
