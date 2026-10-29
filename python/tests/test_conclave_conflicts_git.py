# SPDX-License-Identifier: GPL-3.0-or-later
"""The conflict events on a synthetic repository (docs/guides/conclave.md#conflicts): real git, the real trial rebase
and the real proof, with only `gh` made up.

`pr_support` builds a unit on a branch `work` with a bare `origin` on disk; a second clone moves `main`. One
`collect` is one watch cycle: a conflicting pull request whose rebase is clean and whose stamps hold must be a
`unit-ready` in it, with no engineer started, and a real conflict must be one `conflict-fix` event."""

import itertools
from datetime import timedelta
from pathlib import Path
from typing import Any

from conclave_support import NOW, FakeGh, Layout, github_answers, sources
from git_support import git
from pr_support import PARAGRAPH, Built, make
from test_conflict_work import main_moves

from coney_tools import pr_hold, pr_ready
from coney_tools.conclave_snapshot import Sources, collect
from coney_tools.pr_hold import Hold
from coney_tools.unit_record import UnitRecord, write_record

_WORKSPACES = itertools.count()
PR = 5


def over(built: Built, tmp_path: Path, released: bool = True) -> Sources:
    """Conclave's sources over the made-up unit, whose branch `work` has pull request 5 in conflict with `main`."""
    answers = github_answers()
    answers[("pr", "list", "--state", "all")] = [
        {"number": PR, "headRefName": "work", "state": "OPEN", "closedAt": None}
    ]
    answers[("pr", "list", "--state", "open")] = [
        {
            "number": PR,
            "headRefName": "work",
            "baseRefName": "main",
            "isDraft": False,
            "mergeable": "CONFLICTING",
            "autoMergeRequest": None,
            "headRefOid": built.tip,
            "statusCheckRollup": [],
            "updatedAt": "2026-10-23T12:00:00Z",
        }
    ]
    base = sources(Layout(tmp_path / f"conclave-{next(_WORKSPACES)}"), FakeGh(answers))
    base.coord.scratch = built.unit.dir.parent
    base.coord.worktrees = lambda: [(built.root, built.task)]
    base.coord.health = lambda pulls, required, now: {
        "stuck": [{"number": PR, "branch": "work", "cause": "conflict", "detail": "with main", "checks": []}],
        "other": [],
        "notices": [],
    }
    base.ready = lambda root, unit, text, conventions, now: pr_ready.readiness(root, unit, text, conventions, now=now)
    base.trial = None
    base.tip_of = lambda root: built.tip
    if released:
        built.stamp("pr-opened", detail={"pr": PR})
        write_record(
            built.unit.dir / "unit.json",
            UnitRecord("work", built.kind, (757,), "alice", "work", built.root, "origin/main", built.base, NOW),
        )
    return base


def work_events(built: Built, tmp_path: Path, **changes: Any) -> list[dict[str, Any]]:
    """The events of one GitHub snapshot of the made-up unit about its conflict."""
    report = collect(over(built, tmp_path, **changes), github=True)
    assert report["events"]["error"] is None and report["events"]["unread"] == []
    found: list[dict[str, Any]] = report["events"]["events"]
    return [e for e in found if e["kind"] in ("unit-ready", "conflict-fix", "pr-stuck")]


def test_a_conflicting_pull_request_whose_rebase_is_clean_and_whose_stamps_hold_is_a_unit_ready(tmp_path: Path) -> None:
    built = make(tmp_path / "unit", shape="tooling")
    before = built.diff
    main_moves(built, "other.txt", "main moved\n")
    tip = built.tip
    [event] = work_events(built, tmp_path)
    assert event["kind"] == "unit-ready" and event["state"] == "open" and event["guard"] is False
    assert event["id"] == f"unit:work:PR.md:{before[:12]}" and event["do"] == "coney-tools pr open work"
    assert event["ref"] == {"pr": PR, "task": "work", "branch": "work"}
    # nothing of the unit was touched: the branch, the worktree and the list of worktrees are as they were
    assert built.tip == tip and git(built.root, "status", "--porcelain") == ""
    assert len([line for line in git(built.root, "worktree", "list").splitlines() if line]) == 1


def test_a_real_conflict_is_one_conflict_fix_event_that_names_the_command(tmp_path: Path) -> None:
    built = make(tmp_path / "unit", shape="tooling")
    main_moves(built, "python/src/coney_tools/widget.py", '"""A widget that main wrote differently."""' + chr(10))
    [event] = work_events(built, tmp_path)
    assert event["kind"] == "conflict-fix" and event["state"] == "open" and event["guard"] is True
    assert event["id"] == f"conflict-fix:{PR}:{built.tip[:7]}:go"
    assert event["do"].startswith("coney-tools task create --fix-of 757; brief: rebase work onto origin/main")
    assert "python/src/coney_tools/widget.py" in event["text"] and event["ref"]["pr"] == PR


def test_a_conflict_fix_is_handled_when_an_agent_holds_it_and_open_again_when_the_hold_runs_out(tmp_path: Path) -> None:
    built = make(tmp_path / "unit", shape="tooling")
    main_moves(built, "python/src/coney_tools/widget.py", '"""A widget that main wrote differently."""' + chr(10))
    src = over(built, tmp_path)
    folder = pr_hold.holds_folder(src.coord.scratch, src.conventions.conclave.watch)
    pr_hold.write(folder, Hold(PR, "engineer-2", "conflict", NOW + timedelta(hours=4), "rebasing", NOW, built.tip[:7]))
    [held] = [e for e in collect(src, github=True)["events"]["events"] if e["ref"].get("pr") == PR]
    assert held["state"] == "handled" and held["handled_by"].startswith("engineer-2 until")
    pr_hold.write(folder, Hold(PR, "engineer-2", "conflict", NOW - timedelta(hours=1), "rebasing", NOW, built.tip[:7]))
    [again] = work_events(built, tmp_path, released=False)
    assert again["kind"] == "conflict-fix" and again["state"] == "open"


def test_a_clean_rebase_whose_stamps_went_stale_is_a_conflict_fix_not_a_unit_ready(tmp_path: Path) -> None:
    built = make(tmp_path / "unit", shape="tooling")
    main_moves(built, "other.txt", "main moved\n")
    path = built.unit.dir / "PR.md"
    path.write_text(
        path.read_text(encoding="utf-8").replace(PARAGRAPH, PARAGRAPH + " It also waits."), encoding="utf-8"
    )
    [event] = work_events(built, tmp_path)
    assert event["kind"] == "conflict-fix" and "rebases clean" in event["text"]
    assert "redo the stamps it made stale" in event["do"]


def test_a_branch_main_has_not_moved_past_is_not_tried(tmp_path: Path) -> None:
    built = make(tmp_path / "unit", shape="tooling")
    # GitHub says conflict but the branch holds `main` already (it computes late): nothing to rebase, so no fix task
    [event] = work_events(built, tmp_path)
    assert event["kind"] == "pr-stuck"
