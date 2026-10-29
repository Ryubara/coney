# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for the conflict events of the Conclave snapshot (docs/guides/conclave.md#conflicts).

A pull request in conflict is work: a clean rebase whose stamps hold is a `unit-ready`, a real conflict (or stale
stamps) is one `conflict-fix` event naming the command to run, and a hold on it only counts when it is an agent's or a
waiver that names a maintainer decision. The trial rebase is made up here (`Sources.trial`); `test_conflict_work` runs
the real one, and the last tests of this file run the whole chain on a synthetic repository."""

from datetime import timedelta
from pathlib import Path
from typing import Any, cast

import pytest
from conclave_support import (
    CHANGE,
    CONVENTIONS,
    NOW,
    TIP,
    FakeGh,
    Layout,
    ago,
    github_answers,
    iso,
    make_layout,
    sources,
    stamp_steps,
)

from coney_tools import pr_hold
from coney_tools.conclave_snapshot import Sources, collect
from coney_tools.conflict_work import CONFLICT, READY, STALE, UNKNOWN, Ledger, Trial
from coney_tools.pr_hold import Hold
from coney_tools.unit import Unit
from coney_tools.unit_record import UnitRecord, write_record

RULES = CONVENTIONS.conclave.watch
CLASH = Trial(CONFLICT, "rebase onto origin/main stops at 0123456789ab", ("src/ai/ai_goal.cpp", "src/ai/ai_path.cpp"))
DECISION = "waits for the maintainer's answer on #1130"


@pytest.fixture
def layout(tmp_path: Path) -> Layout:
    """Two units, `fix-frame-812` (pull request 812, released) and `fix-new`, each with a worktree."""
    made = make_layout(tmp_path)
    release(made, "fix-frame-812")
    release(made, "fix-new", released=False)
    return made


def release(layout: Layout, task: str, released: bool = True) -> None:
    """Give the unit a record, as `task start` does, and (released) a passing `pr-opened` stamp."""
    folder = layout.scratch / task
    write_record(
        folder / "unit.json",
        UnitRecord(
            task, "feature", (741,), "example-maintainer", task, layout.trees / task, "origin/main", "a" * 40, NOW
        ),
    )
    if released:
        stamp_steps(layout.scratch, task, [("pr-opened", "pass")], hours_ago=2)


def answering(trial: Trial | None = None, calls: list[str] | None = None) -> Any:
    """A `Sources.trial` that answers `trial` for every branch and records the keys it was asked for."""

    def trial_of(root: Path, unit: Unit, text: Path, key: str, head: str, now: Any) -> Trial:
        if calls is not None:
            calls.append(key)
        return trial or CLASH

    return trial_of


def conflicting(layout: Layout, trial: Any = None, extra: tuple[int, ...] = (), **changes: Any) -> Sources:
    """Sources in which pull request 812 (and each of `extra`, a unit's branch `fix-new` for 813) is in conflict."""
    answers = github_answers()
    pulls = answers[("pr", "list", "--state", "open")]
    pulls[0]["mergeable"] = "CONFLICTING"
    for number in extra:
        pulls.append({**pulls[0], "number": number, "headRefName": "fix-new", "headRefOid": "e" * 40})
    src = sources(layout, FakeGh(answers), trial=trial or answering(), **changes)
    stuck = [
        {"number": n, "branch": b, "cause": "conflict", "detail": "with main", "checks": []}
        for n, b in [(812, "fix-frame-812"), *((n, "fix-new") for n in extra)]
    ]
    src.coord.health = lambda pulls, required, now: {"stuck": stuck, "other": [], "notices": []}
    return src


def events(report: dict[str, Any], *kinds: str) -> list[dict[str, Any]]:
    """The events of a report, of these kinds when given."""
    found: list[dict[str, Any]] = report["events"]["events"]
    return [event for event in found if not kinds or event["kind"] in kinds]


def read(src: Sources) -> dict[str, Any]:
    """A GitHub snapshot over the sources."""
    return collect(src, github=True)


def hold_on(layout: Layout, pr: int = 812, hours: float = 4, **changes: Any) -> None:
    """A hold on the pull request, made now (the snapshot's clock)."""
    base: dict[str, Any] = {"who": "engineer-2", "cause": None, "note": "rebasing", "head": "bbbbbbb"}
    base.update(changes)
    folder = pr_hold.holds_folder(layout.scratch, RULES)
    pr_hold.write(folder, Hold(pr=pr, until=NOW + timedelta(hours=hours), at=NOW, **base))


# --- a clean rebase is a unit that is ready -------------------------------------------------------------------------


def test_a_conflict_whose_rebase_is_clean_and_whose_stamps_hold_is_a_unit_ready_and_nothing_else(
    layout: Layout,
) -> None:
    ready = Trial(READY, change=CHANGE)
    found = events(read(conflicting(layout, answering(ready))))
    [event] = [e for e in found if e["kind"] in ("unit-ready", "conflict-fix", "pr-stuck")]
    assert event["kind"] == "unit-ready" and event["state"] == "open" and event["guard"] is False
    assert event["id"] == f"unit:fix-frame-812:PR.md:{CHANGE[:12]}"
    assert event["ref"] == {"pr": 812, "task": "fix-frame-812", "branch": "fix-frame-812"}
    assert event["do"] == "coney-tools pr open fix-frame-812" and "#812" in event["text"]


def test_a_clean_conflict_whose_unit_is_held_by_the_coordinator_raises_nothing(layout: Layout) -> None:
    (layout.scratch / "fix-frame-812" / RULES.unit_hold_file).write_text("waiting\n", encoding="utf-8")
    found = events(read(conflicting(layout, answering(Trial(READY, change=CHANGE)))))
    assert [e for e in found if e["kind"] in ("unit-ready", "conflict-fix", "pr-stuck")] == []


# --- a real conflict is a fix task ----------------------------------------------------------------------------------


def test_a_real_conflict_raises_one_conflict_fix_event_naming_the_command(layout: Layout) -> None:
    found = events(read(conflicting(layout)))
    [event] = [e for e in found if e["kind"] == "conflict-fix"]
    assert [e for e in found if e["kind"] in ("pr-stuck", "unit-ready")] == []
    assert event["id"] == "conflict-fix:812:bbbbbbb:go" and event["state"] == "open" and event["guard"] is True
    assert event["ref"] == {"pr": 812, "task": "fix-frame-812", "branch": "fix-frame-812"}
    assert "#812" in event["text"] and "src/ai/ai_goal.cpp, src/ai/ai_path.cpp" in event["text"]
    assert event["do"].startswith("coney-tools task create --fix-of 741; brief: rebase fix-frame-812 onto origin/main")
    assert "resolve src/ai/ai_goal.cpp" in event["do"] and "pr hold 812 --who <agent> --cause conflict" in event["do"]


def test_the_same_conflict_read_again_is_the_same_event_and_a_new_push_is_a_new_one(layout: Layout) -> None:
    first = events(read(conflicting(layout)), "conflict-fix")[0]["id"]
    assert events(read(conflicting(layout)), "conflict-fix")[0]["id"] == first
    src = conflicting(layout)
    cast(FakeGh, src.coord.gh).answers[("pr", "list", "--state", "open")][0]["headRefOid"] = "c" * 40
    assert events(read(src), "conflict-fix")[0]["id"] != first


def test_stale_stamps_after_a_clean_rebase_are_a_conflict_fix_that_says_so(layout: Layout) -> None:
    stale = Trial(STALE, "reviewed: the review covers another change; run task review", change="e" * 40)
    [event] = events(read(conflicting(layout, answering(stale))), "conflict-fix")
    assert "rebases clean" in event["text"] and "reviewed: the review covers another change" in event["text"]
    assert "redo the stamps it made stale" in event["do"] and "resolve" not in event["do"]


def test_a_conflict_the_trial_could_not_judge_stays_a_pr_stuck_event(layout: Layout) -> None:
    found = events(read(conflicting(layout, answering(Trial(UNKNOWN, "git fetch origin main failed")))))
    [event] = [e for e in found if e["kind"] in ("pr-stuck", "conflict-fix")]
    assert event["kind"] == "pr-stuck" and event["id"] == "pr:812:conflict:bbbbbbb" and event["guard"] is True


def test_a_trial_that_raises_does_not_take_the_other_events_down(layout: Layout) -> None:
    def broken(*args: object) -> Trial:
        raise OSError("disk full")

    report = read(conflicting(layout, broken))
    assert report["events"]["error"] is None and "conflict-fix" in report["events"]["unread"]
    assert [e["kind"] for e in events(report, "pr-stuck", "conflict-fix")] == ["pr-stuck"]


def test_a_conflict_on_a_branch_with_no_worktree_here_is_left_to_the_coordinator(layout: Layout) -> None:
    calls: list[str] = []
    src = conflicting(layout, answering(calls=calls))
    src.coord.worktrees = lambda: []
    [event] = events(read(src), "pr-stuck", "conflict-fix")
    assert event["kind"] == "pr-stuck" and calls == []


def test_pull_requests_that_cannot_be_read_name_the_kind_unread(layout: Layout) -> None:
    src = conflicting(layout)
    cast(FakeGh, src.coord.gh).answers[("pr", "list", "--state", "open")] = OSError("gh is down")
    assert "conflict-fix" in read(src)["events"]["unread"]


# --- the fix place ------------------------------------------------------------------------------------------------


def test_the_oldest_conflict_goes_first_and_the_next_says_which_fix_it_waits_behind(layout: Layout) -> None:
    release(layout, "fix-new")
    Ledger(layout.scratch / RULES.conflicts_dir).first_seen("812", "bbbbbbb", ago(hours=5))
    Ledger(layout.scratch / RULES.conflicts_dir).first_seen("813", "eeeeeee", ago(hours=9))
    found = {e["ref"]["pr"]: e for e in events(read(conflicting(layout, extra=(813,))), "conflict-fix")}
    assert found[813]["id"].endswith(":go") and found[812]["id"].endswith(":wait")
    assert "waits behind #813's fix" in found[812]["do"] and found[812]["do"].count("task create --fix-of") == 1
    assert found[813]["since"] == iso(ago(hours=9))
    # only the conflict that may go is held against the coordinator's turn end: it can do nothing for the one that waits
    assert found[813]["guard"] is True and found[812]["guard"] is False


def test_a_fix_already_running_makes_every_conflict_wait_behind_it(layout: Layout) -> None:
    other = layout.worktree("fix-running")
    folder = layout.scratch / "fix-running"
    folder.mkdir()
    (folder / "TASK.md").write_text("---\ntask: fix-running\nkind: fix\n---\n", encoding="utf-8")
    write_record(
        folder / "unit.json",
        UnitRecord(
            "fix-running", "fix", (700,), "example-maintainer", "fix-frame-812", other, "origin/main", "a" * 40, NOW
        ),
    )
    [event] = events(read(conflicting(layout)), "conflict-fix")
    assert event["id"].endswith(":wait") and "waits behind fix-running" in event["do"]
    assert "task create --fix-of 741" in event["do"]


def test_the_fix_task_made_for_a_conflict_is_named_and_not_made_again(layout: Layout) -> None:
    # the coordinator ran the event's `task create --fix-of 741`: the fix is queued, and the conflict (whose hold may
    # have run out) must neither wait behind its own fix nor invite a second one: the line names the fix that exists
    # and asks only for the hold
    folder = layout.scratch / "741-fix-frame"
    folder.mkdir()
    (folder / "TASK.md").write_text(
        "---\ntask: 741-fix-frame\nissues: [741]\nkind: fix\nclaimed_at: 2026-10-29T10:00:00Z\n---\n",
        encoding="utf-8",
    )
    [event] = events(read(conflicting(layout)), "conflict-fix")
    assert event["id"].endswith(":fixing") and event["guard"] is True
    assert event["do"] == (
        "the fix task 741-fix-frame exists, do not create another; "
        "record it: coney-tools pr hold 812 --who <agent> --cause conflict"
    )


def test_a_place_that_frees_makes_the_waiting_conflict_a_new_event(layout: Layout) -> None:
    release(layout, "fix-new")
    Ledger(layout.scratch / RULES.conflicts_dir).first_seen("813", "eeeeeee", ago(hours=9))
    waiting = next(e for e in events(read(conflicting(layout, extra=(813,))), "conflict-fix") if e["ref"]["pr"] == 812)
    alone = events(read(conflicting(layout)), "conflict-fix")[0]
    assert waiting["id"] != alone["id"] and alone["id"].endswith(":go")


# --- a branch that predates units, and a unit never released --------------------------------------------------------


def test_a_branch_with_no_unit_record_is_listed_with_the_way_to_adopt_it(layout: Layout) -> None:
    (layout.scratch / "fix-frame-812" / "unit.json").unlink()
    task = layout.scratch / "fix-frame-812" / "TASK.md"
    task.write_text(task.read_text(encoding="utf-8").replace("kind: fix", "kind: feature"), encoding="utf-8")
    [event] = events(read(conflicting(layout)), "conflict-fix")
    assert event["do"].startswith("coney-tools task create 741 --name fix-frame-812; the engineer then runs")
    assert "coney-tools task start fix-frame-812 --adopt" in event["do"] and event["ref"]["task"] is None


def test_a_branch_with_no_task_file_asks_for_the_issue(layout: Layout) -> None:
    for name in ("unit.json", "TASK.md"):
        (layout.scratch / "fix-frame-812" / name).unlink()
    [event] = events(read(conflicting(layout)), "conflict-fix")
    assert "task create <issue> --name fix-frame-812" in event["do"]


def test_a_unit_never_released_is_fixed_in_its_own_task(layout: Layout) -> None:
    (layout.scratch / "fix-frame-812" / "stamps").rename(layout.scratch / "fix-frame-812" / "stamps-before")
    stamp_steps(layout.scratch, "fix-frame-812", [("assigned", "pass"), ("claimed", "pass")])
    [event] = events(read(conflicting(layout)), "conflict-fix")
    assert event["do"].startswith("start the engineer of the unit's own task fix-frame-812")


def test_a_ready_unit_main_moved_past_whose_rebase_conflicts_is_a_fix_for_its_own_task(layout: Layout) -> None:
    (layout.scratch / "fix-new" / "PR.md").write_text("x\n", encoding="utf-8")
    src = sources(layout, FakeGh(github_answers()), trial=answering())
    src.coord.health = lambda pulls, required, now: {"stuck": [], "other": [], "notices": []}
    report = read(src)
    assert events(report, "unit-ready") == []
    [event] = events(report, "conflict-fix")
    assert event["ref"] == {"pr": None, "task": "fix-new", "branch": "fix-new"} and event["guard"] is False
    assert event["id"] == f"conflict-fix:unit-fix-new:{TIP[:7]}:go"
    assert "unit's own task fix-new" in event["do"] and "pr open fix-new" in event["do"]


def test_a_ready_unit_whose_rebase_is_clean_stays_ready(layout: Layout) -> None:
    (layout.scratch / "fix-new" / "PR.md").write_text("x\n", encoding="utf-8")
    src = sources(layout, FakeGh(github_answers()), trial=answering(Trial(READY, change=CHANGE)))
    assert len(events(read(src), "unit-ready")) == 1 and events(read(src), "conflict-fix") == []


def test_a_ready_unit_whose_trial_could_not_be_made_stays_ready(layout: Layout) -> None:
    (layout.scratch / "fix-new" / "PR.md").write_text("x\n", encoding="utf-8")
    src = sources(layout, FakeGh(github_answers()), trial=answering(Trial(UNKNOWN, "no fetch")))
    assert len(events(read(src), "unit-ready")) == 1


# --- holds: no silent parking ---------------------------------------------------------------------------------------


def test_an_agents_hold_handles_the_conflict_and_the_trial_is_not_made(layout: Layout) -> None:
    calls: list[str] = []
    hold_on(layout, who="engineer-2")
    [event] = events(read(conflicting(layout, answering(calls=calls))), "pr-stuck", "conflict-fix")
    assert event["state"] == "handled" and event["handled_by"] == "engineer-2 until " + iso(NOW + timedelta(hours=4))
    assert calls == []


def test_a_waiver_without_a_decision_does_not_handle_a_conflict(layout: Layout) -> None:
    hold_on(layout, who=pr_hold.WAIVED, note="will do it later")
    [event] = events(read(conflicting(layout)), "pr-stuck", "conflict-fix")
    assert event["kind"] == "conflict-fix" and event["state"] == "open"


def test_a_waiver_that_names_a_decision_handles_a_young_conflict(layout: Layout) -> None:
    hold_on(layout, who=pr_hold.WAIVED, note=DECISION)
    [event] = events(read(conflicting(layout)), "pr-stuck", "conflict-fix")
    assert event["state"] == "handled" and event["handled_by"].startswith("waived until")


def test_a_waived_conflict_a_day_old_is_told_again_each_day(layout: Layout) -> None:
    ledger = Ledger(layout.scratch / RULES.conflicts_dir)
    ledger.first_seen("812", "bbbbbbb", ago(hours=25))
    hold_on(layout, who=pr_hold.WAIVED, note=DECISION)
    [first] = events(read(conflicting(layout)), "pr-stuck", "conflict-fix")
    assert first["state"] == "open" and first["guard"] is False and first["id"].endswith(":waived-1")
    assert DECISION in first["text"] and "hold 812 --release" in first["do"]
    assert events(read(conflicting(layout)), "pr-stuck")[0]["id"] == first["id"]
    path = layout.scratch / RULES.conflicts_dir / "812.json"
    path.write_text(path.read_text(encoding="utf-8").replace(iso(ago(hours=25)), iso(ago(hours=49))), encoding="utf-8")
    [second] = events(read(conflicting(layout)), "pr-stuck")
    assert second["id"].endswith(":waived-2") and second["id"] != first["id"]


def test_an_agents_hold_on_an_old_conflict_is_not_a_reminder(layout: Layout) -> None:
    Ledger(layout.scratch / RULES.conflicts_dir).first_seen("812", "bbbbbbb", ago(hours=60))
    hold_on(layout, who="engineer-2")
    [event] = events(read(conflicting(layout)), "pr-stuck")
    assert event["state"] == "handled"


def test_a_hold_that_ran_out_opens_the_conflict_again(layout: Layout) -> None:
    hold_on(layout, hours=-1)
    [event] = events(read(conflicting(layout)), "pr-stuck", "conflict-fix")
    assert event["kind"] == "conflict-fix" and event["state"] == "open"


# --- the ledger -----------------------------------------------------------------------------------------------------


def test_the_first_read_writes_when_the_conflict_was_seen_and_a_read_after_it_ends_forgets_it(layout: Layout) -> None:
    read(conflicting(layout))
    path = layout.scratch / RULES.conflicts_dir / "812.json"
    assert path.is_file() and iso(NOW) in path.read_text(encoding="utf-8")
    src = sources(layout, FakeGh(github_answers()), trial=answering())
    src.coord.health = lambda pulls, required, now: {"stuck": [], "other": [], "notices": []}
    read(src)
    assert not path.exists()


def test_an_unwritable_scratch_folder_never_fails_the_read(layout: Layout) -> None:
    (layout.scratch / RULES.conflicts_dir).write_text("a file where the folder goes", encoding="utf-8")
    report = read(conflicting(layout))
    assert report["events"]["error"] is None and len(events(report, "conflict-fix")) == 1


def test_a_pull_request_whose_mergeability_is_still_computed_keeps_its_ledger(layout: Layout) -> None:
    read(conflicting(layout))
    path = layout.scratch / RULES.conflicts_dir / "812.json"
    src = sources(layout, FakeGh(github_answers()), trial=answering())
    cast(FakeGh, src.coord.gh).answers[("pr", "list", "--state", "open")][0]["mergeable"] = "UNKNOWN"
    src.coord.health = lambda pulls, required, now: {"stuck": [], "other": [], "notices": []}
    read(src)
    assert path.is_file()
