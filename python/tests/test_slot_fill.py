# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for the slot-fill check (slot_fill, slot_waivers): when a free engineer place is a failure and when it is not.

The scratch folder is a temporary one with task files, unit records and real stamp logs; GitHub is a stand-in
(`FakeSources`). The Stop hook and the `task slots` command have their own files (docs/guides/slot-fill.md).
"""

import json
from dataclasses import replace
from datetime import timedelta
from pathlib import Path

import pytest
from conventions_support import with_networking_place, with_renderer_place
from slot_fill_support import (
    FILL,
    NETWORKING,
    RENDERER,
    RULES,
    WORKFLOW,
    FakeSources,
    check,
    claimed,
    full_places,
    gate_on,
    issue,
    with_fill,
)
from test_task_slots import USER, Scene, reserve_minutes, set_slots, start_agent

from coney_tools import slot_fill, slot_fill_blocks, slot_waivers, task_slots
from coney_tools.config import ConfigError
from coney_tools.pr_hold import Hold
from coney_tools.slot_fill import FailedPull
from coney_tools.watched_drives import Drive


@pytest.fixture
def scene(tmp_path: Path) -> Scene:
    return Scene(tmp_path)


@pytest.fixture
def sources() -> FakeSources:
    return FakeSources()


def with_lapse(scene: Scene, tracks: tuple[str, ...]) -> None:
    """The scene with only `tracks` in `tasks.start.slots.lapse.tracks`."""
    slots = replace(scene.conventions.tasks.start.slots, lapse_tracks=tracks)
    start = replace(scene.conventions.tasks.start, slots=slots)
    scene.conventions = replace(scene.conventions, tasks=replace(scene.conventions.tasks, start=start))


def hold_claims(scene: Scene, sources: FakeSources) -> None:
    """The issues of every task in the scene as the maintainer's claims on GitHub (`workflow` as the card says)."""
    sources.claims = [
        scene.cards[number] for numbers in scene.issues_of.values() for number in numbers if number in scene.cards
    ]


# --- the conventions ------------------------------------------------------------------------------------------------


def test_the_committed_gate_is_off_and_names_the_workflow_label() -> None:
    assert not FILL.gate.enabled and FILL.gate.labels == (WORKFLOW,) and FILL.gate.issues == ()
    assert FILL.priority_order == ("P-critical", "P-high", "P-medium", "P-low")
    assert [code.name for code in FILL.waivers] == ["maintainer-pause", "maintainer-decision", "machine-load"]
    assert FILL.waiver_max_hours == 24 and FILL.waiver("machine-load").max_hours <= 4  # type: ignore[union-attr]
    assert FILL.waiver("maintainer-decision").needs_issues  # type: ignore[union-attr]
    assert (FILL.block_max, FILL.block_reset_minutes) == (3, 30)


def test_the_gate_labels_and_priorities_are_labels_of_the_repository() -> None:
    from audit_support import CONVENTIONS

    names = CONVENTIONS.labels.names()
    assert set(FILL.gate.labels) <= set(names) and set(FILL.priority_order) <= set(names)
    assert RULES.critical_label == FILL.priority_order[0]


# --- the free places -----------------------------------------------------------------------------------------------


def test_every_place_full_passes_without_reading_the_work(scene: Scene, sources: FakeSources) -> None:
    full_places(scene)
    sources.ready_issues = [issue(1)]
    hold_claims(scene, sources)
    result = check(scene, sources)
    assert not result.blocked and result.message.startswith("ok: every place is full")
    assert "ready" not in sources.calls and "pulls" not in sources.calls


def test_a_free_place_and_a_ready_issue_is_a_failure(scene: Scene, sources: FakeSources) -> None:
    scene.unit("a-one")
    sources.ready_issues = [issue(7, "P-high", title="tools: Add a thing")]
    result = check(scene, sources)
    assert result.blocked and result.message.startswith(slot_fill.BLOCKED)
    # Nothing to fix, yet the place kept for a fix stays kept: a pull request that fails must find it.
    assert "(free: 0 kept for game work, 1 kept for a fix, 2 open)" in result.message
    assert "The place kept for fix lapsed" not in result.message
    assert "#7 [P-high] tools: Add a thing" in result.message
    assert "1 of 4 places in use, reimplementation: a-one" in result.message
    assert "coney-tools task slots --waive <places> --reason <maintainer-pause|maintainer-decision|machine-load>" in (
        result.message
    )
    assert "docs/guides/slot-fill.md" in result.message


def test_a_workflow_unit_in_work_holds_no_place_so_the_places_stay_free(scene: Scene, sources: FakeSources) -> None:
    for name in ("a-flow", "b-flow", "c-flow"):
        scene.unit(name, kind="tooling", workflow=True)
    hold_claims(scene, sources)
    sources.ready_issues = [issue(7)]
    result = check(scene, sources)
    assert result.blocked and "(free: 1 kept for game work, 1 kept for a fix, 2 open)" in result.message


def test_the_machine_limit_follows_the_build_slots(scene: Scene, sources: FakeSources) -> None:
    for name in ("a-one", "b-two", "c-three"):
        scene.unit(name)
    scene.unit("d-four", kind="tooling")
    sources.ready_issues = [issue(7)]
    assert not check(scene, sources).blocked  # 4 of 4 places
    scene.build_slots = 5
    result = check(scene, sources)
    assert result.blocked and "4 of 10 places in use" in result.message


def test_the_place_kept_for_a_fix_alone_free_with_nothing_to_fix_passes(scene: Scene, sources: FakeSources) -> None:
    for name in ("a-one", "b-two", "c-three"):
        scene.unit(name)
    result = check(scene, sources)
    assert not result.blocked and "no open pull request has a failed required check without a fix unit" in (
        result.message
    )


def test_a_place_kept_for_game_work_is_free_while_only_tooling_runs(scene: Scene, sources: FakeSources) -> None:
    for name in ("a-tool", "b-tool"):
        scene.unit(name, kind="tooling")
    scene.unit("c-fix", kind="fix")
    sources.ready_issues = [issue(7, title="gameplay: Add a thing")]
    result = check(scene, sources)
    assert result.blocked and "(free: 1 kept for game work, 0 kept for a fix, 0 open)" in result.message
    assert "(a `feature` task), most urgent first:\n  - #7 " in result.message


def test_the_place_kept_for_game_work_does_not_take_studio_work(scene: Scene, sources: FakeSources) -> None:
    scene.unit("a-studio", studio=True)
    scene.unit("b-studio", studio=True)
    scene.unit("c-fix", kind="fix")
    hold_claims(scene, sources)
    sources.ready_issues = [issue(7, "studio")]
    result = check(scene, sources)  # 3 of 4 places in use: the last is kept for game work
    assert (
        not result.blocked
        and "kept for game work cannot take Coney Studio, networking, renderer, tooling or docs work" in result.message
    )
    sources.ready_issues.append(issue(8, "P-low"))
    result = check(scene, sources)
    assert result.blocked and "#8 [P-low]" in result.message and "#7 " not in result.message


def test_a_studio_issue_ranked_above_the_game_issues_does_not_hide_them(scene: Scene, sources: FakeSources) -> None:
    scene.unit("a-studio", studio=True)
    scene.unit("b-studio", studio=True)
    scene.unit("c-fix", kind="fix")
    hold_claims(scene, sources)
    sources.ready_issues = [issue(7, "studio", "P-high"), issue(8, "P-low")]
    result = check(scene, sources)  # the last place is kept for game work: #7 may not take it, #8 may
    assert result.blocked and "#8 [P-low]" in result.message and "#7 " not in result.message


def test_the_claim_limit_is_named_when_only_studio_work_is_eligible(scene: Scene, sources: FakeSources) -> None:
    scene.unit("a-studio", studio=True)
    scene.unit("b-studio", studio=True)
    scene.unit("c-fix", kind="fix")
    hold_claims(scene, sources)
    sources.claims += [claimed(n) for n in (21, 22, 23)]
    sources.ready_issues = [issue(7, "studio", "P-high"), issue(8, "P-low")]
    result = check(scene, sources)
    assert not result.blocked and "the open-claims limit is reached (" in result.message
    assert "kept for game work cannot take Coney Studio, networking, renderer, tooling or docs work" in result.message


def test_a_tooling_issue_does_not_fill_the_place_kept_for_game_work(scene: Scene, sources: FakeSources) -> None:
    scene.unit("a-tool", kind="tooling")
    scene.unit("b-tool", kind="tooling")
    scene.unit("c-fix", kind="fix")
    sources.ready_issues = [issue(7, "A-tools", "P-high")]
    result = check(scene, sources)  # `task_slots.room` refuses a tooling unit in the place kept for a feature unit
    assert (
        not result.blocked and "cannot take Coney Studio, networking, renderer, tooling or docs work" in result.message
    )
    sources.ready_issues.append(issue(8, "P-low"))
    result = check(scene, sources)
    assert result.blocked and "#8 [P-low]" in result.message and "#7 " not in result.message


def test_a_claimed_tooling_task_does_not_fill_the_place_kept_for_game_work(scene: Scene, sources: FakeSources) -> None:
    scene.unit("a-tool", kind="tooling")
    scene.unit("b-tool", kind="tooling")
    scene.unit("c-fix", kind="fix")
    scene.task("waiting-docs", kind="docs")
    hold_claims(scene, sources)
    result = check(scene, sources)
    assert not result.blocked and "claimed, not started" not in result.message


def test_a_tooling_issue_takes_an_open_place(scene: Scene, sources: FakeSources) -> None:
    scene.unit("a-game")
    scene.unit("b-fix", kind="fix")
    sources.ready_issues = [issue(7, "A-tools")]
    result = check(scene, sources)
    assert result.blocked and "#7 " in result.message and "(free: 0 kept for game work" in result.message


def test_studio_work_takes_an_open_place(scene: Scene, sources: FakeSources) -> None:
    scene.unit("a-game")
    scene.unit("b-fix", kind="fix")
    sources.ready_issues = [issue(7, "studio", "P-high")]
    result = check(scene, sources)
    assert result.blocked and "#7 [P-high]" in result.message and "Studio track" in result.message


def test_studio_units_in_work_hold_places_of_the_machine(scene: Scene, sources: FakeSources) -> None:
    for name in ("a-studio", "b-studio"):
        scene.unit(name, studio=True)
    hold_claims(scene, sources)
    sources.ready_issues = [issue(7, "studio")]
    sources.pulls = [FailedPull(40, "f-branch", ("python",), (900,))]
    result = check(scene, sources)
    # 2 of 4 places: one is kept for game work and one for a fix (a pull request failed), so Studio takes neither.
    assert result.blocked and "2 of 4 places in use, studio: a-studio, b-studio" in result.message
    assert "#40 (f-branch)" in result.message and "#7 " not in result.message


# --- the networking track ---------------------------------------------------------------------------------------------


def with_networking(scene: Scene) -> None:
    """The scene with the committed rule of the networking track: one place kept for it (the tests of the other tracks
    run with none, so the numbers of a machine of 4 places stay the same)."""
    scene.conventions = with_networking_place(scene.conventions, 1)


def test_a_place_kept_for_networking_is_free_while_no_networking_unit_runs(scene: Scene, sources: FakeSources) -> None:
    with_networking(scene)
    scene.unit("a-game")
    scene.unit("b-game")
    scene.unit("c-fix", kind="fix")
    sources.ready_issues = [issue(7, NETWORKING, "P-low")]
    result = check(scene, sources)  # 3 of 4 places: the last is the one the networking track keeps
    assert result.blocked and "1 kept for networking" in result.message
    assert "kept for networking (a networking issue):\n  - #7 " in result.message
    assert "networking track" in result.message


def test_the_place_kept_for_networking_does_not_take_game_or_studio_work(scene: Scene, sources: FakeSources) -> None:
    with_networking(scene)
    scene.unit("a-game")
    scene.unit("b-game")
    scene.unit("c-fix", kind="fix")
    sources.ready_issues = [issue(7, "P-high"), issue(8, "studio"), issue(9, NETWORKING, "P-low")]
    sources.blocked_by = {9: [1]}  # blocked for now
    with_lapse(scene, ())  # without the lapse, a kept place takes only its own track's work
    result = check(scene, sources)
    assert not result.blocked and "kept for networking cannot take game, Coney Studio" in result.message


def test_the_place_kept_for_networking_lapses_while_no_networking_issue_can_start(
    scene: Scene, sources: FakeSources
) -> None:
    # Issue 1370: every networking issue was S-blocked, and the place it kept held back two fix tasks for hours.
    with_networking(scene)
    scene.unit("a-game")
    scene.unit("b-game")
    scene.unit("c-fix", kind="fix")
    sources.ready_issues = [issue(7, "P-high"), issue(9, NETWORKING, "P-low")]
    sources.blocked_by = {9: [1]}
    result = check(scene, sources)
    assert result.blocked and "The place kept for networking lapsed, so it is open: no networking" in result.message
    assert "0 kept for networking" not in result.message and "1 open" in result.message and "#7 " in result.message
    assert task_slots.read_lapses(scene.scratch, scene.conventions, scene.now()) == {
        "networking": "no networking task or issue can start (none is claimed and waiting, ready and unblocked)"
    }
    sources.blocked_by = {}  # #9 can start again: the place is kept again at the next check
    result = check(scene, sources)
    assert result.blocked and "kept for networking (a networking issue):\n  - #9 " in result.message
    assert task_slots.read_lapses(scene.scratch, scene.conventions, scene.now()) == {}


def test_a_kept_place_does_not_lapse_when_the_issues_cannot_be_read(scene: Scene, sources: FakeSources) -> None:
    with_networking(scene)
    scene.unit("a-game")
    scene.unit("b-game")
    scene.unit("c-fix", kind="fix")
    sources.down = {"ready"}
    check(scene, sources)
    assert "networking" not in task_slots.read_lapses(scene.scratch, scene.conventions, scene.now())


def test_the_place_kept_for_game_work_does_not_lapse(scene: Scene, sources: FakeSources) -> None:
    # Only the tracks of tasks.start.slots.lapse.tracks lapse: the committed list leaves game work's floor alone.
    for name in ("a-tool", "b-tool"):
        scene.unit(name, kind="tooling")
    scene.unit("c-fix", kind="fix")
    sources.ready_issues = [issue(8, "studio")]
    result = check(scene, sources)
    assert not result.blocked and "kept for game work cannot take Coney Studio" in result.message


def test_a_networking_issue_does_not_take_the_place_kept_for_game_work(scene: Scene, sources: FakeSources) -> None:
    with_networking(scene)
    scene.unit("a-net", networking=True)
    scene.unit("b-tool", kind="tooling")
    scene.unit("c-fix", kind="fix")
    hold_claims(scene, sources)
    sources.ready_issues = [issue(7, NETWORKING, "P-high")]
    result = check(scene, sources)  # the last place is kept for game work, and the networking track is at its cap
    assert not result.blocked and "the networking track is full" in result.message


def test_a_networking_issue_does_not_take_an_open_place_while_a_networking_unit_runs(
    scene: Scene, sources: FakeSources
) -> None:
    with_networking(scene)
    scene.unit("a-net", networking=True)
    scene.unit("b-game")
    scene.unit("c-fix", kind="fix")
    hold_claims(scene, sources)
    sources.ready_issues = [issue(7, NETWORKING)]
    result = check(scene, sources)  # one place is open, but the track holds at most one unit
    assert not result.blocked and "the networking track is full" in result.message
    sources.ready_issues.append(issue(8, "P-low"))
    result = check(scene, sources)
    assert result.blocked and "#8 [P-low]" in result.message and "#7 " not in result.message


def test_a_claimed_networking_task_fills_the_networking_place(scene: Scene, sources: FakeSources) -> None:
    with_networking(scene)
    scene.unit("a-game")
    scene.unit("b-game")
    scene.unit("c-fix", kind="fix")
    scene.task("n-ids", networking=True)
    hold_claims(scene, sources)
    result = check(scene, sources)
    assert (
        result.blocked and "claimed, not started: n-ids" in result.message and "kept for networking" in result.message
    )


def test_a_networking_issue_is_outside_the_claim_limit(scene: Scene, sources: FakeSources) -> None:
    with_networking(scene)
    scene.unit("a-game")
    scene.unit("b-game")
    scene.unit("c-fix", kind="fix")
    hold_claims(scene, sources)
    sources.claims += [claimed(n) for n in (21, 22, 23)]  # the open-claims limit is reached
    sources.ready_issues = [issue(7, NETWORKING)]
    result = check(scene, sources)
    assert result.blocked and "#7 " in result.message


def test_networking_units_in_work_are_counted_in_the_summary(scene: Scene, sources: FakeSources) -> None:
    with_networking(scene)
    scene.unit("a-net", networking=True)
    hold_claims(scene, sources)
    result = check(scene, sources)
    assert "networking: a-net" in result.message


# --- the renderer track ---------------------------------------------------------------------------------------------


def with_renderer(scene: Scene) -> None:
    """The scene with the committed rule of the renderer track: one place kept for it (the tests of the other tracks
    run with none, so the numbers of a machine of 4 places stay the same)."""
    scene.conventions = with_renderer_place(scene.conventions, 1)


def test_a_place_kept_for_the_renderer_is_free_while_no_renderer_unit_runs(scene: Scene, sources: FakeSources) -> None:
    with_renderer(scene)
    scene.unit("a-game")
    scene.unit("b-game")
    scene.unit("c-fix", kind="fix")
    sources.ready_issues = [issue(7, RENDERER, "P-low")]
    result = check(scene, sources)  # 3 of 4 places: the last is the one the renderer track keeps
    assert result.blocked and "1 kept for the renderer" in result.message
    assert "kept for the renderer (a renderer issue):\n  - #7 " in result.message
    assert "renderer track" in result.message


def test_the_place_kept_for_the_renderer_does_not_take_game_or_studio_work(scene: Scene, sources: FakeSources) -> None:
    with_renderer(scene)
    scene.unit("a-game")
    scene.unit("b-game")
    scene.unit("c-fix", kind="fix")
    sources.ready_issues = [issue(7, "P-high"), issue(8, "studio"), issue(9, NETWORKING)]
    with_lapse(scene, ())  # without the lapse, a kept place takes only its own track's work
    result = check(scene, sources)
    assert not result.blocked and "kept for the renderer cannot take game, Coney Studio" in result.message
    with_lapse(scene, ("renderer",))  # with it, no renderer issue can start, so the place is open
    result = check(scene, sources)
    assert result.blocked and "The place kept for renderer lapsed" in result.message


def test_a_renderer_issue_does_not_take_the_place_kept_for_game_work(scene: Scene, sources: FakeSources) -> None:
    with_renderer(scene)
    scene.unit("a-gpu", renderer=True)
    scene.unit("b-tool", kind="tooling")
    scene.unit("c-fix", kind="fix")
    hold_claims(scene, sources)
    sources.ready_issues = [issue(7, RENDERER, "P-high")]
    result = check(scene, sources)  # the last place is kept for game work, and the renderer track is at its cap
    assert not result.blocked and "the renderer track is full" in result.message


def test_a_renderer_issue_does_not_take_an_open_place_while_a_renderer_unit_runs(
    scene: Scene, sources: FakeSources
) -> None:
    with_renderer(scene)
    scene.unit("a-gpu", renderer=True)
    scene.unit("b-game")
    scene.unit("c-fix", kind="fix")
    hold_claims(scene, sources)
    sources.ready_issues = [issue(7, RENDERER)]
    result = check(scene, sources)  # one place is open, but the track holds at most one unit
    assert not result.blocked and "the renderer track is full" in result.message
    sources.ready_issues.append(issue(8, "P-low"))
    result = check(scene, sources)
    assert result.blocked and "#8 [P-low]" in result.message and "#7 " not in result.message


def test_a_claimed_renderer_task_fills_the_renderer_place(scene: Scene, sources: FakeSources) -> None:
    with_renderer(scene)
    scene.unit("a-game")
    scene.unit("b-game")
    scene.unit("c-fix", kind="fix")
    scene.task("r-gpu", renderer=True)
    hold_claims(scene, sources)
    result = check(scene, sources)
    assert (
        result.blocked and "claimed, not started: r-gpu" in result.message and "kept for the renderer" in result.message
    )


def test_a_renderer_issue_is_outside_the_claim_limit(scene: Scene, sources: FakeSources) -> None:
    with_renderer(scene)
    scene.unit("a-game")
    scene.unit("b-game")
    scene.unit("c-fix", kind="fix")
    hold_claims(scene, sources)
    sources.claims += [claimed(n) for n in (21, 22, 23)]  # the open-claims limit is reached
    sources.ready_issues = [issue(7, RENDERER)]
    result = check(scene, sources)
    assert result.blocked and "#7 " in result.message


def test_renderer_units_in_work_are_counted_in_the_summary(scene: Scene, sources: FakeSources) -> None:
    with_renderer(scene)
    scene.unit("a-gpu", renderer=True)
    hold_claims(scene, sources)
    result = check(scene, sources)
    assert "renderer: a-gpu" in result.message


def test_networking_and_the_renderer_each_keep_their_own_place(scene: Scene, sources: FakeSources) -> None:
    # 4 places, one kept each for game work, a fix, networking and the renderer: a networking issue takes only the
    # networking place, a renderer issue only the renderer's.
    with_networking(scene)
    with_renderer(scene)
    scene.unit("a-game")
    scene.unit("c-fix", kind="fix")
    sources.ready_issues = [issue(7, NETWORKING), issue(8, RENDERER)]
    result = check(scene, sources)
    assert result.blocked
    assert "kept for networking (a networking issue):\n  - #7 " in result.message
    assert "kept for the renderer (a renderer issue):\n  - #8 " in result.message
    scene.unit("a-net", networking=True)
    hold_claims(scene, sources)
    result = check(scene, sources)
    assert result.blocked and "#7 " not in result.message and "  - #8 " in result.message


# --- the start gate and the order -----------------------------------------------------------------------------------


def test_no_ready_issue_is_a_reason(scene: Scene, sources: FakeSources) -> None:
    scene.unit("a-one")
    result = check(scene, sources)
    assert not result.blocked and "no issue is ready to start" in result.message


def test_issues_outside_the_gate_are_a_reason(scene: Scene, sources: FakeSources) -> None:
    gate_on(scene, 1030, 1031)
    sources.ready_issues = [issue(5, "P-high"), issue(6, "P-medium")]
    result = check(scene, sources)
    assert not result.blocked
    assert "the start gate leaves none of the 2 ready issue(s)" in result.message
    assert "labelled workflow, #1030, #1031 and P-critical" in result.message


def test_a_listed_issue_number_passes_the_gate(scene: Scene, sources: FakeSources) -> None:
    gate_on(scene, 1030, 1031)
    sources.ready_issues = [issue(1030, "P-medium"), issue(6, "P-high")]
    result = check(scene, sources)
    assert result.blocked and "#1030 [P-medium]" in result.message and "#6 " not in result.message


def test_a_critical_issue_passes_the_gate_and_comes_first_alone(scene: Scene, sources: FakeSources) -> None:
    gate_on(scene)
    sources.ready_issues = [issue(2, "P-high"), issue(9, "P-critical"), issue(3, "P-high")]
    result = check(scene, sources)
    assert result.blocked and "#9 [P-critical]" in result.message
    assert "#2 " not in result.message and "#3 " not in result.message  # the claim Action refuses them meanwhile


def test_with_the_gate_off_every_ready_issue_is_eligible(scene: Scene, sources: FakeSources) -> None:
    sources.ready_issues = [issue(5, "P-medium")]
    assert check(scene, sources).blocked
    gate_on(scene)
    assert not check(scene, sources).blocked


def test_the_issues_are_listed_by_priority_then_number_up_to_the_top(scene: Scene, sources: FakeSources) -> None:
    with_fill(scene, top=4)
    scene.build_slots = 5  # 10 places: more are free than the top lists
    claims = scene.conventions.claims
    scene.conventions = replace(scene.conventions, claims=replace(claims, limits=replace(claims.limits, maintainer=20)))
    for number, priority in [
        (30, "P-low"),
        (20, "P-medium"),
        (11, "P-high"),
        (12, "P-high"),
        (10, "P-medium"),
        (40, ""),
    ]:
        sources.ready_issues.append(issue(number, *([priority] if priority else [])))
    result = check(scene, sources)
    listed = [line.split()[1] for line in result.message.splitlines() if line.startswith("  - #")]
    assert listed == ["#11", "#12", "#10", "#20", "#30"]  # the place kept for game work, then the top 4 of the rest
    assert "  - ... and 1 more" in result.message


def test_a_long_title_is_cut(scene: Scene, sources: FakeSources) -> None:
    sources.ready_issues = [issue(5, title="tools: " + "word " * 40)]
    line = next(line for line in check(scene, sources).message.splitlines() if "#5 " in line)
    assert "..." in line and len(line) < 200


def test_an_assigned_ready_issue_is_not_startable(scene: Scene, sources: FakeSources) -> None:
    sources.ready_issues = [issue(5, assignees=("alice",))]
    result = check(scene, sources)
    assert not result.blocked and "no issue is ready to start" in result.message


def test_a_blocked_issue_is_skipped_and_a_reason_when_all_are(scene: Scene, sources: FakeSources) -> None:
    sources.ready_issues = [issue(5, "P-high"), issue(6)]
    sources.blocked_by = {5: [4]}
    result = check(scene, sources)
    assert result.blocked and "#6 " in result.message and "#5 " not in result.message
    sources.blocked_by = {5: [4], 6: [4, 3]}
    result = check(scene, sources)
    assert not result.blocked and "blocked by an unfinished one: #5 (by #4), #6 (by #4, #3)" in result.message


def test_a_blocker_that_cannot_be_read_does_not_block_the_issue_and_warns(scene: Scene, sources: FakeSources) -> None:
    sources.ready_issues = [issue(5)]
    sources.down = {"blockers"}
    result = check(scene, sources)
    assert (
        result.blocked and "#5 " in result.message and any("#5 is blocked was not read" in w for w in result.warnings)
    )


def test_blockers_that_cannot_be_read_for_one_reason_make_one_warning(scene: Scene, sources: FakeSources) -> None:
    """A dozen issues that failed the same way (the budget spent) are one line, not a dozen."""
    sources.ready_issues = [issue(number) for number in range(5, 17)]
    sources.down = {"blockers"}
    result = check(scene, sources)
    unread = [w for w in result.warnings if "not read" in w]
    assert len(unread) == 1 and "#5, #6" in unread[0] and unread[0].count("blockers is down") == 1
    assert "are blocked" not in result.message  # reading them failed, so none is said to be blocked


# --- the claim limit ------------------------------------------------------------------------------------------------


def test_the_claim_limit_reached_is_a_reason_for_an_issue_outside_the_gate_of_workflow(
    scene: Scene, sources: FakeSources
) -> None:
    sources.claims = [claimed(n) for n in (1, 2, 3)]
    sources.ready_issues = [issue(5, "P-high")]
    result = check(scene, sources)
    assert not result.blocked and "the open-claims limit is reached (3 of 3" in result.message


def test_a_workflow_issue_is_not_work_for_a_place_so_the_game_issue_behind_it_is(
    scene: Scene, sources: FakeSources
) -> None:
    scene.unit("a-studio", studio=True)
    scene.unit("b-studio", studio=True)
    scene.unit("c-fix", kind="fix")
    hold_claims(scene, sources)
    sources.ready_issues = [issue(10, WORKFLOW, "P-high"), issue(11, "P-low")]
    result = check(scene, sources)  # a workflow unit holds no place, so starting #10 would leave the place free
    assert result.blocked and "#11 [P-low]" in result.message and "#10 " not in result.message
    sources.ready_issues = [issue(10, WORKFLOW, "P-high")]
    result = check(scene, sources)
    assert not result.blocked and "#10 carries workflow, whose units hold no place" in result.message


def test_a_claimed_workflow_task_is_not_work_for_a_place(scene: Scene, sources: FakeSources) -> None:
    for name in ("a-one", "b-two"):
        scene.unit(name)
    scene.task("waiting", kind="tooling", workflow=True)
    hold_claims(scene, sources)
    assert "claimed, not started: waiting" not in check(scene, sources).message


def test_a_workflow_issue_counts_as_work_while_workflow_units_hold_places(scene: Scene, sources: FakeSources) -> None:
    set_slots(scene, exempt_workflow=False)
    sources.ready_issues = [issue(10, WORKFLOW)]
    assert check(scene, sources).blocked


def test_a_claim_whose_place_is_free_leaves_the_count(scene: Scene, sources: FakeSources) -> None:
    sources.claims = [claimed(1), claimed(2), claimed(3, freed=True)]
    sources.ready_issues = [issue(5)]
    result = check(scene, sources)
    assert result.blocked and "#5 " in result.message


@pytest.mark.parametrize("paused", ["S-on-hold", "S-blocked", "S-needs-info"])
def test_a_claim_paused_by_a_label_still_counts_as_the_claim_action_counts_it(
    scene: Scene, sources: FakeSources, paused: str
) -> None:
    from coney_tools.issue_claim_rules import open_task_keys

    assert paused in RULES.paused_labels
    sources.claims = [claimed(1), claimed(2), claimed(3, paused=paused)]
    sources.ready_issues = [issue(5, "P-high")]
    # The Action's own count of these cards is 3 of 3: it would refuse a fourth claim.
    assert len(open_task_keys(sources.claims, USER, 0)) == 3
    result = check(scene, sources)
    assert not result.blocked and "the open-claims limit is reached (3 of 3" in result.message
    sources.claims[2] = claimed(3, freed=True)  # the same claim with its place free does not count
    assert check(scene, sources).blocked


def test_a_paused_claim_with_no_agent_is_not_work_to_start(scene: Scene, sources: FakeSources) -> None:
    for name in ("a-one", "b-two"):
        scene.unit(name)
    scene.task("paused", kind="tooling")
    hold_claims(scene, sources)
    number = scene.issues_of["paused"][0]
    sources.claims = [c if c.issue != number else replace(c, waiting="S-on-hold") for c in sources.claims]
    assert "claimed, not started" not in check(scene, sources).message


def test_a_studio_issue_is_outside_the_claim_limit(scene: Scene, sources: FakeSources) -> None:
    sources.claims = [claimed(n) for n in (1, 2, 3)]
    sources.ready_issues = [issue(5, "studio")]
    assert check(scene, sources).blocked
    sources.ready_issues = [issue(5)]
    assert not check(scene, sources).blocked


def test_a_claim_in_the_workflow_or_studio_does_not_count_and_issues_of_one_task_count_once(
    scene: Scene, sources: FakeSources
) -> None:
    scene.unit("big", issues=(11, 12, 13))
    sources.claims = [
        claimed(11, task="big"),
        claimed(12, task="big"),
        claimed(13, task="big"),
        claimed(2, exempt=True),
        claimed(6, studio=True),
        claimed(3),
    ]
    sources.ready_issues = [issue(5)]
    assert check(scene, sources).blocked  # big + issue-3 = 2 of 3
    sources.claims.append(claimed(4))
    assert not check(scene, sources).blocked  # 3 of 3


def test_the_limit_leaves_room_for_the_rest_of_the_headroom_only(scene: Scene, sources: FakeSources) -> None:
    with_fill(scene, top=5)
    sources.claims = [claimed(1), claimed(2)]
    sources.ready_issues = [issue(5, "P-high"), issue(6, "P-medium"), issue(7, "studio", "P-low")]
    listed = [line.split()[1] for line in check(scene, sources).message.splitlines() if line.startswith("  - #")]
    assert listed == ["#5", "#7"]  # one claim of headroom for #5; #6 waits; the Studio issue is outside the limit


# --- claimed tasks that wait for an agent ---------------------------------------------------------------------------


def test_a_claimed_task_with_no_agent_is_work_to_start(scene: Scene, sources: FakeSources) -> None:
    for name in ("a-one", "b-two"):
        scene.unit(name)
    scene.task("waiting", kind="tooling")
    hold_claims(scene, sources)
    result = check(scene, sources)
    assert result.blocked and "claimed, not started: waiting (tooling)" in result.message
    assert "`Task id: waiting`" in result.message


def test_a_task_whose_claim_is_gone_is_not_work(scene: Scene, sources: FakeSources) -> None:
    for name in ("a-one", "b-two", "c-three"):
        scene.unit(name)
    scene.scratch.joinpath("d-fix").mkdir()
    scene.task("waiting")
    sources.claims = []  # GitHub no longer shows the claim
    result = check(scene, sources)
    assert not result.blocked


def test_a_reserved_task_holds_a_place_and_is_not_waiting(scene: Scene, sources: FakeSources) -> None:
    for name in ("a-one", "b-two"):
        scene.unit(name)
    scene.task("reserved")
    from coney_tools import task_slots
    from coney_tools.task_file import parse_task_file

    task = parse_task_file(
        (scene.scratch / "reserved" / "TASK.md").read_text(encoding="utf-8"), scene.conventions.tasks
    )
    task_slots.write_reservation(scene.scratch / "reserved", task, "reimplementation", "engineer", scene.now())
    hold_claims(scene, sources)
    result = check(scene, sources)
    assert not result.blocked and "3 of 4 places in use" in result.message


def test_a_finished_task_is_not_waiting(scene: Scene, sources: FakeSources) -> None:
    for name in ("a-one", "b-two"):
        scene.unit(name)
    scene.unit("done", end="finished")
    hold_claims(scene, sources)
    assert not check(scene, sources).blocked


# --- the place kept for a fix ----------------------------------------------------------------------------------------


def pull(number: int, *closes: int, checks: tuple[str, ...] = ("build-ok",), head: str = "abc1234def") -> FailedPull:
    return FailedPull(number, f"branch-{number}", checks, closes, head)


def three_units(scene: Scene) -> None:
    for name in ("a-one", "b-two", "c-three"):
        scene.unit(name)


def test_a_failed_pull_request_with_no_fix_unit_is_a_failure(scene: Scene, sources: FakeSources) -> None:
    three_units(scene)
    sources.pulls = [pull(60, 11, checks=("build-ok", "python-ok"))]
    result = check(scene, sources)
    assert result.blocked and "place kept for a fix" in result.message
    assert "#60 (branch-60) failed required check(s) build-ok, python-ok and has no fix unit" in result.message
    assert "task create --fix-of" in result.message


def test_a_queued_fix_task_is_work_for_the_fix_slot(scene: Scene, sources: FakeSources) -> None:
    three_units(scene)
    scene.task("queued-fix", kind="fix", parent="a-one", issues=(11,))
    hold_claims(scene, sources)
    result = check(scene, sources)
    assert result.blocked and "fix task queued-fix is queued" in result.message


def test_a_fix_task_with_an_agent_and_no_unit_holds_the_fix_place_and_covers_its_pull_request(
    scene: Scene, sources: FakeSources
) -> None:
    three_units(scene)
    scene.task("slot-fix", kind="fix", parent="a-one", issues=(11,))
    start_agent(scene, "slot-fix", minutes_ago=reserve_minutes(scene) * 20)
    sources.pulls = [pull(60, 11)]
    hold_claims(scene, sources)
    result = check(scene, sources)
    assert not result.blocked and result.message.startswith("ok: every place is full")
    assert "fix: slot-fix" in result.message and "is queued" not in result.message


def test_a_queued_fix_task_whose_pull_request_is_over_is_not_work(scene: Scene, sources: FakeSources) -> None:
    three_units(scene)
    scene.task("queued-fix", kind="fix", parent="a-one", issues=(11,))
    hold_claims(scene, sources)
    assert check(scene, sources).blocked
    sources.over = {11}
    result = check(scene, sources)
    assert not result.blocked and "queued-fix" not in result.message


def test_a_github_that_cannot_be_read_does_not_drop_a_queued_fix(scene: Scene, sources: FakeSources) -> None:
    three_units(scene)
    scene.task("queued-fix", kind="fix", parent="a-one", issues=(11,))
    hold_claims(scene, sources)
    sources.over = {11}
    sources.down = {"claimed"}
    assert not check(scene, sources).blocked and sources.asked_over == []


def test_a_fix_task_covers_the_pull_request_that_closes_its_issue(scene: Scene, sources: FakeSources) -> None:
    three_units(scene)
    scene.unit("live-fix", kind="fix", issues=(11,))
    scene.task("queued-fix", kind="fix", parent="b-two", issues=(12,))
    sources.pulls = [pull(60, 11), pull(61, 12)]
    assert not check(scene, sources).blocked  # one fix is in work (the places are full), one queued: the queued is work
    sources.pulls = [pull(60, 11)]
    assert not check(scene, sources).blocked


def test_a_pull_request_closing_no_fixed_issue_is_not_covered(scene: Scene) -> None:
    pulls = [pull(60, 99), pull(61, 12), pull(62)]
    left = slot_fill.uncovered_pulls(pulls, {12}, [], scene.now())
    assert [p.number for p in left] == [60, 62]


def test_a_running_hold_covers_the_pull_request(scene: Scene, sources: FakeSources) -> None:
    three_units(scene)
    sources.pulls = [pull(60, 11)]
    held = Hold(60, "engineer-a", None, scene.now() + timedelta(hours=2), "fixing", scene.now(), "abc1234")
    assert not check(scene, sources, holds=[held]).blocked
    other_head = replace(held, head="fffffff")
    assert check(scene, sources, holds=[other_head]).blocked
    expired = replace(held, until=scene.now() - timedelta(minutes=1))
    assert check(scene, sources, holds=[expired]).blocked


def test_full_places_read_no_pull_requests(scene: Scene, sources: FakeSources) -> None:
    full_places(scene)
    sources.pulls = [pull(60, 11)]
    check(scene, sources)
    assert "pulls" not in sources.calls


# --- the machine ----------------------------------------------------------------------------------------------------


def test_low_disk_is_a_reason(scene: Scene, sources: FakeSources) -> None:
    sources.ready_issues = [issue(5)]
    result = check(scene, sources, free_gb=12.0)
    assert not result.blocked
    assert "free disk is 12 GB on C: (the workspace drive), under its 30 GB floor" in result.message


def test_the_floor_itself_is_not_low(scene: Scene, sources: FakeSources) -> None:
    sources.ready_issues = [issue(5)]
    floor = float(scene.conventions.workspace.disk_min_free_gb)
    assert check(scene, sources, free_gb=floor).blocked
    assert not check(scene, sources, free_gb=floor - 0.1).blocked


def gb(name: str, free: float, holds: str, floor: int = 30) -> Drive:
    """A watched drive with this much free space and floor."""
    return Drive(name, (holds,), int(free * 1024**3), floor, 10)


def test_a_low_build_root_drive_is_a_reason_though_the_workspace_drive_has_room(
    scene: Scene, sources: FakeSources
) -> None:
    sources.ready_issues = [issue(5)]
    drives = [gb("C:", 61, "the workspace drive"), gb("E:", 3.7, "the build root (E:/coney_builds)")]
    result = check(scene, sources, drives=drives)
    assert not result.blocked
    assert "free disk is 3.7 GB on E: (the build root (E:/coney_builds)), under its 30 GB floor" in result.message
    assert "C:" not in result.message


def test_drives_with_room_are_no_reason(scene: Scene, sources: FakeSources) -> None:
    sources.ready_issues = [issue(5)]
    drives = [gb("C:", 61, "the workspace drive"), gb("E:", 90, "the build root (E:/b)"), gb("F:", 31, "x")]
    assert check(scene, sources, drives=drives).blocked


def test_an_extra_watched_drive_under_its_floor_is_a_reason(scene: Scene, sources: FakeSources) -> None:
    sources.ready_issues = [issue(5)]
    drives = [gb("C:", 61, "the workspace drive"), gb("F:", 8, "the watched folder (F:/scratch)")]
    result = check(scene, sources, drives=drives)
    assert not result.blocked and "8 GB on F: (the watched folder (F:/scratch))" in result.message


def test_a_drive_is_low_by_its_own_floor(scene: Scene, sources: FakeSources) -> None:
    sources.ready_issues = [issue(5)]
    assert check(scene, sources, drives=[gb("F:", 12, "the watched folder (F:/x)", floor=10)]).blocked
    result = check(scene, sources, drives=[gb("F:", 12, "the watched folder (F:/x)", floor=15)])
    assert not result.blocked and "under its 15 GB floor" in result.message


def test_every_low_drive_is_named(scene: Scene, sources: FakeSources) -> None:
    sources.ready_issues = [issue(5)]
    result = check(scene, sources, drives=[gb("C:", 20, "the workspace drive"), gb("E:", 3, "the build root (E:/b)")])
    assert "20 GB on C:" in result.message and "3 GB on E:" in result.message


def test_an_unknown_free_space_is_no_reason(scene: Scene, sources: FakeSources) -> None:
    sources.ready_issues = [issue(5)]
    assert check(scene, sources, free_gb=None).blocked


def test_a_github_that_cannot_be_read_passes_with_a_warning(scene: Scene, sources: FakeSources) -> None:
    sources.down = {"all"}
    sources.ready_issues = [issue(5)]
    result = check(scene, sources)
    assert not result.blocked and result.message.startswith("ok, not checked")
    assert result.warnings and "GitHub could not be read" in result.warnings[0]


def test_ready_issues_that_cannot_be_read_pass_with_a_warning(scene: Scene, sources: FakeSources) -> None:
    sources.down = {"ready"}
    result = check(scene, sources)
    assert not result.blocked and any("ready issues could not be read" in w for w in result.warnings)


def test_pull_requests_that_cannot_be_read_do_not_block_and_warn(scene: Scene, sources: FakeSources) -> None:
    three_units(scene)
    sources.down = {"pulls"}
    result = check(scene, sources)
    assert not result.blocked and any("pull requests could not be read" in w for w in result.warnings)


# --- waivers --------------------------------------------------------------------------------------------------------


def waive(scene: Scene, slots: int = 2, reason: str = "maintainer-pause", hours: float = 4, **changes: object) -> None:
    values: dict[str, object] = {
        "note": "the maintainer is away until the evening",
        "issues": (),
        "who": "coordinator abc",
        **changes,
    }
    slot_waivers.add_waiver(
        scene.scratch,
        scene.conventions.tasks.start.fill,
        4,
        scene.now(),
        slots,
        reason,
        hours,
        **values,  # type: ignore[arg-type]
    )


def test_a_waiver_for_the_free_places_passes_and_names_itself(scene: Scene, sources: FakeSources) -> None:
    scene.unit("a-one")
    sources.ready_issues = [issue(5), issue(6)]
    waive(scene, 2)
    result = check(scene, sources)
    assert not result.blocked and result.message.startswith("ok: waived")
    assert "maintainer-pause" in result.message and "2 place(s) could start work" in result.message


def test_a_waiver_for_fewer_places_than_could_start_still_fails(scene: Scene, sources: FakeSources) -> None:
    scene.unit("a-one")
    sources.ready_issues = [issue(5), issue(6)]
    waive(scene, 1)
    result = check(scene, sources)
    assert result.blocked and "Active waivers cover 1 place(s); 2 could start work." in result.message


def test_waivers_add_up(scene: Scene, sources: FakeSources) -> None:
    scene.unit("a-one")
    sources.ready_issues = [issue(5), issue(6)]
    waive(scene, 1)
    waive(scene, 1, "machine-load", 2)
    assert not check(scene, sources).blocked


def test_a_waiver_ends_when_it_expires(scene: Scene, sources: FakeSources) -> None:
    scene.unit("a-one")
    sources.ready_issues = [issue(5)]
    waive(scene, 2, hours=4)
    assert not check(scene, sources).blocked
    assert not check(scene, sources, now=scene.now() + timedelta(hours=3, minutes=59)).blocked
    assert check(scene, sources, now=scene.now() + timedelta(hours=4)).blocked


def test_a_waiver_made_by_a_later_clock_is_not_active_yet(scene: Scene) -> None:
    waive(scene, 1)
    (waiver,) = slot_waivers.read_waivers(scene.scratch)
    assert not waiver.active(scene.now() - timedelta(minutes=1)) and waiver.active(scene.now())


def test_the_log_keeps_every_waiver_and_numbers_them(scene: Scene) -> None:
    waive(scene, 1)
    waive(scene, 2, "machine-load", 1)
    lines = (scene.scratch / slot_waivers.WAIVER_FILE).read_text(encoding="utf-8").splitlines()
    assert [json.loads(line)["id"] for line in lines] == [1, 2]
    assert json.loads(lines[1])["reason"] == "machine-load" and json.loads(lines[0])["who"] == "coordinator abc"


def test_a_damaged_log_line_excuses_nothing_and_loses_nothing_else(scene: Scene) -> None:
    waive(scene, 1)
    with (scene.scratch / slot_waivers.WAIVER_FILE).open("a", encoding="utf-8") as log:
        log.write("not json\n" + json.dumps({"v": 9, "id": 3}) + "\n")
    assert [w.id for w in slot_waivers.read_waivers(scene.scratch)] == [1]


@pytest.mark.parametrize(
    ("changes", "message"),
    [
        ({"reason": "because"}, "not a waiver code"),
        ({"hours": 25}, "runs more than 0 and at most 24 hours"),
        ({"hours": 0}, "runs more than 0 and at most 24 hours"),
        ({"hours": 4, "reason": "machine-load"}, "at most 3 hours"),
        ({"slots": 0}, "waive 1 to 4"),
        ({"slots": 5}, "waive 1 to 4"),
        ({"note": "short"}, "at least 12 characters"),
        ({"reason": "maintainer-decision"}, "name the blocked issues"),
    ],
)
def test_a_waiver_that_is_not_allowed_is_refused_and_not_logged(
    scene: Scene, changes: dict[str, object], message: str
) -> None:
    values: dict[str, object] = {"slots": 1, "reason": "maintainer-pause", "hours": 2, **changes}
    with pytest.raises(ConfigError, match=message):
        waive(scene, **values)  # type: ignore[arg-type]
    assert not (scene.scratch / slot_waivers.WAIVER_FILE).exists()


def test_a_maintainer_decision_names_the_blocked_issues(scene: Scene) -> None:
    waive(scene, 1, "maintainer-decision", 24, issues=(1030, 1031))
    (waiver,) = slot_waivers.read_waivers(scene.scratch)
    assert waiver.issues == (1030, 1031) and "blocked: #1030, #1031" in waiver.line(scene.now())


def test_the_report_lists_active_waivers_and_counts_the_ended(scene: Scene) -> None:
    waive(scene, 1, hours=1)
    waive(scene, 2, "machine-load", 2)
    lines = slot_waivers.report(slot_waivers.read_waivers(scene.scratch), scene.now() + timedelta(hours=1, minutes=30))
    assert lines[0] == "waivers: 1 active" and "#2 2 place(s), machine-load" in lines[1]
    assert lines[-1] == "  (1 ended, in slot-waivers.jsonl)"
    assert slot_waivers.report([], scene.now()) == ["waivers: none active"]


def test_a_waiver_that_names_issues_ends_when_they_all_close(scene: Scene, sources: FakeSources) -> None:
    # Issue 1370: waiver #17 kept two places waived for hours after the issues it waited on had merged.
    scene.unit("a-one")
    sources.ready_issues = [issue(5), issue(6)]
    waive(scene, 2, "maintainer-pause", 24, issues=(1360, 1361), note="paused until the gate issues merge")
    sources.closed_issues = {1360}
    assert not check(scene, sources).blocked  # one of them is still open
    sources.closed_issues = {1360, 1361}
    result = check(scene, sources)
    assert result.blocked and "Active waivers" not in result.message
    assert "waiver #1 ended: every issue it names is closed (#1360, #1361)" in result.message
    (waiver,) = slot_waivers.read_waivers(scene.scratch)
    assert not waiver.active(scene.now()) and waiver.ended == "every issue it names is closed (#1360, #1361)"
    assert slot_waivers.report([waiver], scene.now()) == ["waivers: none active", "  (1 ended, in slot-waivers.jsonl)"]
    sources.closed_issues = set()  # ended for good: a reopened issue does not bring it back
    assert check(scene, sources).blocked


def test_a_waiver_without_issues_runs_its_hours_and_reads_nothing(scene: Scene, sources: FakeSources) -> None:
    scene.unit("a-one")
    sources.ready_issues = [issue(5), issue(6)]
    waive(scene, 2)
    sources.closed_issues = {5, 6}
    assert not check(scene, sources).blocked and "closed" not in sources.calls


def test_a_waiver_whose_issues_cannot_be_read_stays(scene: Scene, sources: FakeSources) -> None:
    scene.unit("a-one")
    sources.ready_issues = [issue(5), issue(6)]
    waive(scene, 2, "maintainer-decision", 24, issues=(1360,))
    sources.closed_issues = {1360}
    sources.down = {"closed"}
    result = check(scene, sources)
    assert not result.blocked and any("whether the issues of waiver #1 are closed" in w for w in result.warnings)
    assert slot_waivers.read_waivers(scene.scratch)[0].ended == ""


# --- the Stop hook's cap on blocks in a row -------------------------------------------------------------------------


def test_a_session_is_blocked_up_to_the_cap_then_let_through(scene: Scene) -> None:
    results = [slot_fill_blocks.may_block(scene.scratch, scene.conventions, "s1", scene.now()) for _ in range(5)]
    assert results == [True, True, True, False, False]


def test_the_count_starts_again_after_the_reset(scene: Scene) -> None:
    for _ in range(3):
        slot_fill_blocks.may_block(scene.scratch, scene.conventions, "s1", scene.now())
    later = scene.now() + timedelta(minutes=FILL.block_reset_minutes + 1)
    assert slot_fill_blocks.may_block(scene.scratch, scene.conventions, "s1", later)


def test_a_pass_clears_the_count(scene: Scene) -> None:
    for _ in range(3):
        slot_fill_blocks.may_block(scene.scratch, scene.conventions, "s1", scene.now())
    slot_fill_blocks.clear_blocks(scene.scratch, "s1")
    assert slot_fill_blocks.may_block(scene.scratch, scene.conventions, "s1", scene.now())


def test_sessions_are_counted_apart(scene: Scene) -> None:
    for _ in range(3):
        slot_fill_blocks.may_block(scene.scratch, scene.conventions, "s1", scene.now())
    assert slot_fill_blocks.may_block(scene.scratch, scene.conventions, "s2", scene.now())


def test_a_counter_that_cannot_be_written_never_blocks(scene: Scene) -> None:
    (scene.scratch / slot_fill_blocks.BLOCKS_FILE).mkdir()  # a directory where the file goes
    assert not slot_fill_blocks.may_block(scene.scratch, scene.conventions, "s1", scene.now())


def test_a_damaged_counter_is_an_empty_one(scene: Scene) -> None:
    (scene.scratch / slot_fill_blocks.BLOCKS_FILE).write_text("{not json", encoding="utf-8")
    assert slot_fill_blocks.may_block(scene.scratch, scene.conventions, "s1", scene.now())
    assert USER  # the scene's claimant is the maintainer
