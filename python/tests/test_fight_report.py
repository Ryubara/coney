# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for the gang-fight report (coney_tools.fight_report) on synthetic all-humans traces."""

from __future__ import annotations

from collections.abc import Callable
from pathlib import Path
from typing import Any

import pytest

from coney_tools import cli
from coney_tools.fight_report import (
    GOAL_NAMES,
    FightTraceError,
    analyse,
    compare_runs,
    goal_name,
    load_fight_trace,
    load_goal_names,
    parse_fight_trace,
    report_compare,
    report_run,
    scalars,
    set_target_cause,
)

REPO = Path(__file__).resolve().parents[2]


def numbers(*args: Any, **kwargs: Any) -> dict[str, Any]:
    """scalars(), typed loosely so the tests can compare its numbers."""
    return dict(scalars(*args, **kwargs))


HEADER = "step,human,gang,x,y,health,command,block,goal,stack,target"
#: A row maker: (step, human) to the fields that differ from a standing, healthy human with no goal.
Rule = Callable[[int, int], dict[str, object]]


def make_trace(steps: int, humans: dict[int, int], rule: Rule, positions: dict[int, tuple[float, float]]) -> str:
    """CSV text of `steps` updates for `humans` (number to gang); `rule` gives each row's changed fields."""
    lines = [HEADER]
    for step in range(1, steps + 1):
        for human, gang in humans.items():
            x, y = positions.get(human, (0.0, 0.0))
            row: dict[str, object] = {
                "x": x,
                "y": y,
                "health": 100,
                "command": 0,
                "block": 0,
                "goal": 15,
                "stack": "15",
                "target": -1,
            }
            row.update(rule(step, human))
            lines.append(
                f"{step},{human},{gang},{row['x']},{row['y']},{row['health']},{row['command']},{row['block']},"
                f"{row['goal']},{row['stack']},{row['target']}"
            )
    return "\n".join(lines) + "\n"


GANGS = {0: 1, 1: 1, 2: 2, 3: 2}
POSITIONS = {0: (0.0, 0.0), 1: (1.0, 0.0), 2: (4.0, 0.0), 3: (8.0, 0.0)}


def fight_rule(step: int, human: int) -> dict[str, object]:
    """Gang 1 (humans 0 and 1) beats gang 2 (2 and 3): 3 is down at 400, 2 at 500, which ends the match."""
    row: dict[str, object] = {}
    if human in (0, 1):
        row["target"] = 2 if step < 450 else 3
        if human == 0:
            if step in (31, 61, 91):
                row["command"] = 0x0F
            if 100 <= step <= 110:
                row["block"] = 1
            row["goal"], row["stack"] = (8, "8") if step <= 100 else (15, "15")
    else:
        row["target"] = 0 if human == 2 and step < 300 else 1
    if (human == 3 and step >= 400) or (human == 2 and step >= 500):
        row["health"] = 0
        row["goal"], row["stack"] = -1, "-"
        row["target"] = -1
    return row


@pytest.fixture
def fight() -> str:
    return make_trace(700, GANGS, fight_rule, POSITIONS)


def test_a_trace_without_the_identifying_columns_is_refused() -> None:
    with pytest.raises(FightTraceError, match="no human, gang column"):
        parse_fight_trace("step,x\n1,0\n", "t.csv")
    with pytest.raises(FightTraceError, match="no rows"):
        parse_fight_trace("step,human,gang\n", "t.csv")
    with pytest.raises(FightTraceError, match="human 0 twice at step 1"):
        parse_fight_trace("step,human,gang\n1,0,1\n1,0,1\n", "t.csv")
    with pytest.raises(FightTraceError, match="whole numbers"):
        parse_fight_trace("step,human,gang\n1,a,1\n", "t.csv")


def test_the_match_ends_when_one_gang_is_left_and_that_gang_wins(fight: str) -> None:
    analysis = analyse(parse_fight_trace(fight))
    assert analysis.end_step == 500
    assert analysis.winner_gang == 1
    flat = numbers(analysis, {1: "g0", 2: "g1"})
    assert flat["match.winner"] == "g0"
    assert flat["match.length_s"] == pytest.approx(500 / 30)


def test_a_match_nobody_won_has_no_winner() -> None:
    text = make_trace(100, GANGS, lambda step, human: {"target": 2 if human == 0 else -1}, POSITIONS)
    analysis = analyse(parse_fight_trace(text))
    assert analysis.end_step == 100
    assert not analysis.winner_known
    assert "match.winner" not in numbers(analysis)


def test_everyone_knocked_out_is_a_draw() -> None:
    text = make_trace(10, {0: 1, 1: 2}, lambda step, human: {"health": 0 if step >= 5 else 100}, POSITIONS)
    flat = numbers(analyse(parse_fight_trace(text)))
    assert flat["match.winner"] == "draw"
    assert flat["match.length_s"] == pytest.approx(5 / 30)


def test_time_on_each_goal_is_a_share_of_the_time_alive(fight: str) -> None:
    flat = numbers(analyse(parse_fight_trace(fight)), {1: "g0", 2: "g1"})
    # Human 0 and 1 (gang g0) are alive for 500 steps; human 0 melees for 100 of its 500, human 1 never.
    assert flat["g0.goal.melee"] == pytest.approx(100 / 1000)
    assert flat["g0.goal.fight"] == pytest.approx(900 / 1000)


def test_attacks_and_blocks_are_counted_per_minute_alive(fight: str) -> None:
    flat = numbers(analyse(parse_fight_trace(fight)), {1: "g0", 2: "g1"})
    minutes = 1000 / 30 / 60  # two humans alive for 500 steps
    assert flat["g0.attack.square_per_min"] == pytest.approx(3 / minutes)
    assert flat["g0.attack.total_per_min"] == pytest.approx(3 / minutes)
    assert flat["g0.block.held_per_min"] == pytest.approx(1 / minutes)  # the blocking column
    assert flat["g0.block.per_min"] == 0  # no Blocking goal (27) on top
    assert "g1.attack.square_per_min" not in flat


def test_a_command_held_for_several_updates_is_one_attack_and_a_chain_is_two() -> None:
    def rule(step: int, human: int) -> dict[str, object]:
        if human != 0:
            return {}
        return {"command": {10: 0x0F, 11: 0x0F, 12: 0x0F, 13: 0x11, 20: 0x10, 30: 0x04, 31: 0x04}.get(step, 0)}

    text = make_trace(60, {0: 1, 1: 2}, rule, POSITIONS)
    analysis = analyse(parse_fight_trace(text))
    human = analysis.humans[0]
    # 0x0f held three updates is one attack; 0x11 (a chain's square) straight after is a second.
    assert dict(human.attacks) == {"square": 2, "cross": 1}
    assert human.held_blocks == 1  # command 4, with no block column value, held two updates


def test_command_four_counts_as_a_block_when_the_trace_has_no_block_column() -> None:
    text = "step,human,gang,command\n" + "".join(f"{s},0,1,{4 if 5 <= s <= 9 else 0}\n" for s in range(1, 20))
    assert analyse(parse_fight_trace(text)).humans[0].held_blocks == 1


def test_attackers_on_one_target_are_counted_at_each_update(fight: str) -> None:
    flat = numbers(analyse(parse_fight_trace(fight)), {1: "g0", 2: "g1"})
    # Humans 0 and 1 go for 2 until 450 (2 attackers on 2; and 2's target 0 until 300 adds one on 0, and 3's target 1).
    assert flat["all.attackers.max"] == 2.0
    assert flat["g0.attackers.share2"] == pytest.approx(1.0 - flat["g0.attackers.share1"])
    assert flat["g0.attackers.mean"] > 1.0
    # Gang g1's humans never pair up on one target: 2 goes for 0 and 3 for 1, then both for 1.
    assert 0.0 < flat["g1.attackers.share1"] <= 1.0


def test_distance_to_the_target_comes_from_the_positions(fight: str) -> None:
    flat = numbers(analyse(parse_fight_trace(fight)), {1: "g0", 2: "g1"})
    # Human 0 to 2: 4 m; human 1 to 2: 3 m; later to 3: 8 m and 7 m (until the match ends at 500).
    assert 3.0 <= flat["g0.distance.p10"] <= 4.0
    assert flat["g0.distance.p90"] >= 4.0
    assert flat["g0.distance.p10"] <= flat["g0.distance.p50"] <= flat["g0.distance.p90"]


def test_target_switches_ignore_the_target_being_lost_and_found() -> None:
    targets = {1: -1, 2: 1, 3: 1, 4: 2, 5: 2, 6: -1, 7: 2, 8: 3}
    text = "step,human,gang,target\n" + "".join(
        f"{s},0,1,{t}\n{s},1,2,-1\n{s},2,2,-1\n{s},3,2,-1\n" for s, t in targets.items()
    )
    # 1 -> 2 (a switch), 2 -> lost -> 2 (none), 2 -> 3 (a switch).
    assert analyse(parse_fight_trace(text)).humans[0].target_switches == 2


def test_a_goal_pushed_over_another_or_replaced_is_an_interruption_but_a_pop_is_not() -> None:
    stacks = ["8", "8", "8>15", "8>15", "8", "41", "41"]
    text = "step,human,gang,stack\n" + "".join(f"{i},0,1,{stack}\n" for i, stack in enumerate(stacks, start=1))
    human = analyse(parse_fight_trace(text)).humans[0]
    # 8 -> 8>15 pushes over 8 (interruption); 8>15 -> 8 ends 15; 8 -> 41 replaces 8 (interruption).
    assert human.goal_changes == 3
    assert human.goal_interruptions == 2


def test_goal_changes_from_the_top_goal_alone_are_interruptions_unless_they_start_from_none() -> None:
    goals = [8, -1, 15, 15, 8]
    text = "step,human,gang,goal\n" + "".join(f"{i},0,1,{goal}\n" for i, goal in enumerate(goals, start=1))
    human = analyse(parse_fight_trace(text)).humans[0]
    assert human.goal_changes == 3
    assert human.goal_interruptions == 2  # 8 -> none and 15 -> 8 change one, none -> 15 starts one


def test_time_to_the_first_engagement_is_the_first_attack_or_a_target_in_reach() -> None:
    def rule(step: int, human: int) -> dict[str, object]:
        if human == 0:
            return {"target": 1, "command": 0x0F if step == 91 else 0}
        return {"target": 0}  # human 1 has a target, but is 20 m away and never swings

    positions = {0: (0.0, 0.0), 1: (20.0, 0.0)}
    analysis = analyse(parse_fight_trace(make_trace(120, {0: 1, 1: 2}, rule, positions)))
    assert analysis.humans[0].first_engage_step == 91
    flat = numbers(analysis)
    assert flat["all.engage.first_s"] == pytest.approx(90 / 30)
    assert flat["all.engage.share"] == pytest.approx(0.5)
    close = analyse(parse_fight_trace(make_trace(120, {0: 1, 1: 2}, rule, {0: (0.0, 0.0), 1: (2.5, 0.0)})))
    assert close.humans[0].first_engage_step == 1


def test_knock_out_times_per_gang_and_for_all(fight: str) -> None:
    flat = numbers(analyse(parse_fight_trace(fight)), {1: "g0", 2: "g1"})
    assert flat["g0.ko.count"] == 0
    assert flat["g1.ko.count"] == 2
    assert flat["g1.ko.first_s"] == pytest.approx(399 / 30)
    assert flat["g1.ko.last_s"] == pytest.approx(499 / 30)
    assert flat["g1.ko.median_s"] == pytest.approx((399 + 499) / 2 / 30)
    assert flat["all.ko.count"] == 2


def test_excluded_humans_are_left_out_of_everything(fight: str) -> None:
    trace = parse_fight_trace(fight)
    analysis = analyse(trace, exclude=frozenset({0}))
    assert 0 not in analysis.humans
    flat = numbers(analysis, {1: "g0", 2: "g1"})
    assert flat["g0.humans"] == 1
    assert "g0.attack.square_per_min" not in flat


def test_the_updates_a_second_scales_every_rate() -> None:
    def rule(step: int, human: int) -> dict[str, object]:
        return {"command": 0x0F if step == 5 else 0}

    text = make_trace(60, {0: 1}, rule, POSITIONS)
    at_30 = numbers(analyse(parse_fight_trace(text), hz=30))["all.attack.total_per_min"]
    at_60 = numbers(analyse(parse_fight_trace(text), hz=60))["all.attack.total_per_min"]
    assert at_60 == pytest.approx(2 * at_30)  # the same 60 updates are half the time


def test_a_missing_column_leaves_its_metrics_out() -> None:
    text = "step,human,gang\n" + "".join(f"{s},{h},{h + 1}\n" for s in range(1, 4) for h in range(2))
    flat = numbers(analyse(parse_fight_trace(text)))
    assert "all.distance.p50" not in flat
    assert not any(key.startswith(("all.goal.", "all.attack.", "all.block.", "all.target.")) for key in flat)
    assert flat["all.humans"] == 2
    assert "match.winner" not in flat


def test_the_run_report_names_the_match_the_gangs_and_with_humans_each_human(fight: str) -> None:
    analysis = analyse(parse_fight_trace(fight, "fight.csv"))
    text = report_run(analysis)
    assert text.splitlines()[0].startswith("fight.csv: 4 humans, 700 steps, match 16.667 s, winner g1")
    assert "gang g1:" in text and "gang g2:" in text and "all humans:" in text
    assert "goal.fight" in text
    assert "human 0 (gang 1):" not in text
    assert "human 0 (gang 1):" in report_run(analysis, per_human=True)


# --- comparing runs ----------------------------------------------------------------------------------------------


def runs(*values: dict[str, float | str]) -> list[dict[str, float | str]]:
    return [dict(value) for value in values]


def test_a_metric_inside_the_originals_spread_is_not_flagged_and_one_outside_is() -> None:
    original = runs({"g0.block.per_min": 10.0}, {"g0.block.per_min": 12.0}, {"g0.block.per_min": 14.0})
    assert [c.flag for c in compare_runs(original, runs({"g0.block.per_min": 11.0}))] == [""]
    assert [c.flag for c in compare_runs(original, runs({"g0.block.per_min": 14.0}))] == [""]
    assert [c.flag for c in compare_runs(original, runs({"g0.block.per_min": 20.0}))] == ["above"]
    assert [c.flag for c in compare_runs(original, runs({"g0.block.per_min": 2.0}))] == ["below"]


def test_coneys_median_over_its_runs_is_what_is_compared_and_the_margin_widens_the_spread() -> None:
    original = runs({"all.distance.p50": 2.0}, {"all.distance.p50": 4.0})
    coney = runs({"all.distance.p50": 1.0}, {"all.distance.p50": 4.5}, {"all.distance.p50": 4.4})
    assert [c.flag for c in compare_runs(original, coney)] == ["above"]
    assert [c.flag for c in compare_runs(original, coney, margin=0.25)] == [""]


def test_a_goal_or_attack_one_side_never_uses_counts_as_zero() -> None:
    original = runs({"g0.attack.cross_per_min": 3.0}, {"g0.attack.cross_per_min": 5.0})
    assert [c.flag for c in compare_runs(original, runs({"g0.all": 1.0}))] == ["below"]
    assert [c.flag for c in compare_runs(runs({"g0.goal.block": 0.0}), runs({"g0.goal.block": 0.2}))] == ["above"]
    # The original never used it and Coney does: that is above its spread of zeros.
    only_coney = compare_runs(runs({"g0.goal.fight": 1.0}), runs({"g0.goal.fight": 0.5, "g0.goal.block": 0.5}))
    assert [(c.name, c.flag) for c in only_coney] == [("g0.goal.block", "above"), ("g0.goal.fight", "below")]


def test_a_metric_coney_lacks_is_missing_and_one_only_coney_has_is_not_compared() -> None:
    original = runs({"g0.ko.first_s": 20.0, "g0.humans": 3.0})
    result = compare_runs(original, runs({"g0.humans": 3.0, "g0.ko.last_s": 30.0}))
    assert {c.name: c.flag for c in result} == {"g0.humans": "", "g0.ko.first_s": "missing"}


def test_the_winner_is_different_when_coneys_never_won_in_the_original() -> None:
    original = runs({"match.winner": "g0"}, {"match.winner": "g0"}, {"match.winner": "g1"})
    assert [c.flag for c in compare_runs(original, runs({"match.winner": "g1"}))] == [""]
    assert [c.flag for c in compare_runs(original, runs({"match.winner": "draw"}))] == ["different"]


def test_the_comparison_table_shows_each_spread_and_counts_the_outliers() -> None:
    original = runs({"match.length_s": 60.0, "match.winner": "g0"}, {"match.length_s": 80.0, "match.winner": "g0"})
    coney = runs({"match.length_s": 120.0, "match.winner": "g0"})
    text = report_compare(compare_runs(original, coney), (2, 1))
    assert "original: 2 runs, Coney: 1 runs" in text
    assert "match.length_s" in text and "60 / 70 / 80" in text and "120 / 120 / 120" in text
    assert "<< above" in text
    assert "g0 x2" in text
    assert text.splitlines()[-1] == "1 of 2 metrics outside the original's spread"


# --- the command -------------------------------------------------------------------------------------------------


def write(tmp_path: Path, name: str, text: str) -> Path:
    path = tmp_path / name
    path.write_text(text, encoding="utf-8")
    return path


def test_the_command_reports_a_trace(tmp_path: Path, capsys: pytest.CaptureFixture[str], fight: str) -> None:
    trace = write(tmp_path, "a.csv", fight)
    assert cli.main(["trace", "fight-report", str(trace), "--humans"]) == 0
    out = capsys.readouterr().out
    assert "winner g1" in out and "human 3 (gang 2):" in out
    assert cli.main(["trace", "fight-report", str(trace), "--exclude", "0", "1", "2", "3"]) == 0
    assert cli.main(["trace", "fight-report", str(trace), "--hz", "60"]) == 0


def test_the_command_compares_runs_and_exits_1_outside_the_spread(
    tmp_path: Path, capsys: pytest.CaptureFixture[str], fight: str
) -> None:
    same = [write(tmp_path, f"o{i}.csv", fight) for i in range(2)]
    # Coney's run: nobody ever blocks, which the original's does.
    quiet = make_trace(
        700, GANGS, lambda step, human: {k: v for k, v in fight_rule(step, human).items() if k != "block"}, POSITIONS
    )
    assert cli.main(["trace", "fight-report", "--original", *map(str, same), "--coney", str(same[0])]) == 0
    assert "0 of" in capsys.readouterr().out.splitlines()[-1]
    other = write(tmp_path, "c.csv", quiet)
    assert cli.main(["trace", "fight-report", "--original", *map(str, same), "--coney", str(other)]) == 1
    assert "g0.block.held_per_min" in capsys.readouterr().out


def test_the_command_pairs_gangs_in_the_order_given(
    tmp_path: Path, capsys: pytest.CaptureFixture[str], fight: str
) -> None:
    # Coney's gangs have other ids (7 for the original's 1, 9 for 2): g0 and g1 still line up.
    original = write(tmp_path, "o.csv", fight)
    coney_text = make_trace(700, {0: 7, 1: 7, 2: 9, 3: 9}, fight_rule, POSITIONS)
    coney = write(tmp_path, "c.csv", coney_text)
    args = ["trace", "fight-report", "--original", str(original), "--coney", str(coney)]
    assert cli.main([*args, "--original-gangs", "1,2", "--coney-gangs", "7,9"]) == 0
    capsys.readouterr()
    # Paired the other way round, Coney's winner (gang 7) is the original's loser (gang 2).
    assert cli.main([*args, "--original-gangs", "1,2", "--coney-gangs", "9,7"]) == 1
    assert "match.winner" in capsys.readouterr().out


def test_the_command_refuses_unfinished_requests(tmp_path: Path, fight: str) -> None:
    trace = write(tmp_path, "a.csv", fight)
    assert cli.main(["trace", "fight-report"]) == 2
    assert cli.main(["trace", "fight-report", "--original", str(trace)]) == 2
    assert cli.main(["trace", "fight-report", str(trace), "--original", str(trace), "--coney", str(trace)]) == 2
    assert cli.main(["trace", "fight-report", str(tmp_path / "missing.csv")]) == 2
    assert cli.main(["trace", "fight-report", str(write(tmp_path, "b.csv", "step,x\n1,2\n"))]) == 2


def test_the_goal_stack_and_block_come_from_the_games_own_columns_too() -> None:
    # The original's and Coney's names: goal0.. for the stack and blocking for the block.
    rows = [(8, -1, 0), (8, 15, 0), (8, -1, 1), (41, -1, 1), (41, -1, 0)]
    text = "step,human,gang,blocking,goal,goal0,goal1\n" + "".join(
        f"{i},0,1,{blocking},{goal1 if goal1 >= 0 else goal0},{goal0},{goal1}\n"
        for i, (goal0, goal1, blocking) in enumerate(rows, start=1)
    )
    human = analyse(parse_fight_trace(text)).humans[0]
    assert human.goal_changes == 3  # 8 -> 8>15 -> 8 -> 41
    assert human.goal_interruptions == 2  # pushed over, then replaced; the pop only ends a goal
    assert human.held_blocks == 1  # the blocking column; no Blocking goal (27) on top


def test_a_gang_that_never_fights_is_left_out_and_does_not_hold_up_the_end_of_the_match() -> None:
    # Gang 9 is an audience of humans on their feet and in no fight; gang 1 beats gang 2.
    humans = {**GANGS, 4: 9, 5: 9}

    def with_audience(step: int, human: int) -> dict[str, object]:
        return fight_rule(step, human) if human < 4 else {}

    analysis = analyse(parse_fight_trace(make_trace(700, humans, with_audience, POSITIONS)))
    assert 4 not in analysis.humans and 5 not in analysis.humans
    assert analysis.end_step == 500
    flat = numbers(analysis)
    assert flat["match.winner"] == "g1"
    assert flat["all.humans"] == 4
    assert not any(key.startswith("g9.") for key in flat)


def test_the_report_can_start_after_an_intro() -> None:
    def rule(step: int, human: int) -> dict[str, object]:
        return {"target": 1 if human == 0 else 0, "command": 0x0F if human == 0 and step == 40 else 0}

    trace = parse_fight_trace(make_trace(100, {0: 1, 1: 2}, rule, {0: (0.0, 0.0), 1: (20.0, 0.0)}))
    from_start = numbers(analyse(trace))["all.engage.first_s"]
    after_intro = numbers(analyse(trace, start=31))["all.engage.first_s"]
    assert from_start == pytest.approx(39 / 30)
    assert after_intro == pytest.approx(9 / 30)


def test_goal_names_come_from_the_reference_list_in_snake_case(tmp_path: Path) -> None:
    reference = tmp_path / "goals.yaml"
    reference.write_text(
        "entries:\n  - {id: 200, name: MoveToFlagNetFlag}\n  - {id: 201, name: Idle}\n", encoding="utf-8"
    )
    before = dict(GOAL_NAMES)
    try:
        load_goal_names(reference)
        assert goal_name(200) == "move_to_flag_net_flag"
        assert goal_name(201) == "idle"
        assert goal_name(-1) == "none"
        assert goal_name(250) == "goal_0xfa"
    finally:
        GOAL_NAMES.clear()
        GOAL_NAMES.update(before)
    with pytest.raises(FightTraceError):
        load_goal_names(tmp_path / "missing.yaml")


def test_the_repository_reference_list_names_the_fight_goal() -> None:
    before = dict(GOAL_NAMES)
    try:
        load_goal_names(REPO / "research" / "references" / "goal-types.yaml")
        assert goal_name(15) == "fight"
        assert goal_name(27) == "blocking"
    finally:
        GOAL_NAMES.clear()
        GOAL_NAMES.update(before)


# --- the gang battle (docs/research/rumble.md#gang-battle-metrics) ------------------------------------------------

#: The gang-battle columns (research/traces/fields.toml `[[humans]]`) and Coney's help counts.
GB_COLUMNS = [
    *["step", "human", "gang", "btype", "x", "y", "health", "clip", "state", "phase", "down", "target"],
    *["goal", "goal0", "goal1", "goal2", "goal3", "react", "active", "fight_updates", "fight_active"],
    *["g_tactic", "g_teamups", "g_order", "g_chosen", "brain_off", "help_calls", "helpers_sent"],
]
#: Crowd 0 and 1 (gang 0, brain type 4), side 1 humans 10 and 11 (gang 3), side 2 humans 20 and 21 (gang 4).
GB_GANGS = {0: 0, 1: 0, 10: 3, 11: 3, 20: 4, 21: 4}
GB_POSITIONS = {0: (30.0, 30.0), 1: (31.0, 30.0), 10: (0.0, 0.0), 11: (0.0, 3.0), 20: (1.0, 0.0), 21: (5.0, 0.0)}
#: The sides' labels, as the command gives them (side 1 is `g0`).
GB_LABELS = {3: "g0", 4: "g1"}
WARRIOR_ATTACK, CONFRONT, ATTACK = 0x00544160, 0x00543740, 0x00543320
#: Standing updates of each side in the battle below: side 1 389 + 389, side 2 239 (20) + 189 (21).
SIDE1_MINUTES = 778 / 30 / 60
SIDE2_MINUTES = 428 / 30 / 60


def battle_row(step: int, human: int) -> dict[str, object] | None:
    """One row of a synthetic Rumble battle; None when the human is gone from the trace.

    The intro is steps 1-10 (brains off); the brains go on at 11 and off again at 400 (RumbleOver at 399); the trace
    runs on to 450. 21 is knocked out at 200 (clip 349) and deleted at 300; 20 is wounded at 250 (state 0x10000).
    """
    if human == 21 and step >= 300:
        return None
    x, y = GB_POSITIONS[human]
    side1 = GB_GANGS[human] == 3
    row: dict[str, object] = {
        "gang": GB_GANGS[human], "btype": 4 if GB_GANGS[human] == 0 else 2, "x": x, "y": y, "health": 1000,
        "clip": 358, "state": 0, "phase": 0, "down": 0, "target": -1, "react": -1, "active": 0,
        "g_tactic": 0, "g_teamups": 0, "g_order": -1, "g_chosen": -1, "help_calls": 0, "helpers_sent": 0,
    }  # fmt: skip
    stack: list[int] = []
    fight_updates = -1
    fight_active = -1
    brains_on = 11 <= step < 400
    row["brain_off"] = 0 if (brains_on or GB_GANGS[human] == 0) else 1
    if GB_GANGS[human] != 0 and brains_on:
        stack = [65, 8, 15]
        fight_updates = step - 11
        fight_active = 1 if human == 10 else 0
        row["g_tactic"] = WARRIOR_ATTACK if side1 else (CONFRONT if step <= 40 else ATTACK)
        if human == 10:
            row["target"] = 20 if step < 150 else 21
            fight_updates = step - 11 if step < 150 else step - 150
            if step in (20, 21, 22, 40, 41):
                row["phase"] = 1  # two wind-ups: 20-22 and 40-41
            if step == 23:
                row["phase"] = 2  # the chain window is not a new attack
        elif human == 11:
            row["target"] = -1 if step in (120, 121) else 20
            if 50 <= step <= 55:
                stack = [65, 8, 15, 27]  # one block; the fight goal goes on under it
            if 100 <= step <= 110:
                row["clip"] = 603  # one taunt
            if 300 <= step <= 309:
                stack = []  # idle for 10 updates; a new fight goal after
            elif step >= 310:
                fight_updates = step - 310
        elif human == 20:
            row["target"] = 10
            row["active"] = 1
            if 60 <= step <= 65:
                row["react"] = 21
            row["health"] = 1000 if step < 60 else 900 if step < 90 else 800
            row["help_calls"] = 0 if step < 61 else 1 if step < 91 else 2
            if step == 30:
                row["phase"] = 1
        else:
            row["target"] = 10
            row["active"] = 1
            if 70 <= step <= 80:
                row["down"] = 1
                row["state"] = 0x80000
            if 65 <= step <= 70:
                stack = [65, 8, 15, 29]  # sent to help
            row["helpers_sent"] = 1 if step >= 65 else 0
        out = (human == 21 and step >= 200) or (human == 20 and step >= 250)
        if out:
            stack, fight_updates, fight_active, row["target"] = [], -1, -1, -1
            row["clip"] = 349 if human == 21 else 200
            row["state"] = 0x10000 if human == 20 else 0
    if stack and stack[-1] != 15:
        fight_updates = fight_active = -1
    row["goal"] = stack[-1] if stack else -1
    for index in range(4):
        row[f"goal{index}"] = stack[index] if index < len(stack) else -1
    row["fight_updates"] = fight_updates
    row["fight_active"] = fight_active
    return row


def battle_text(steps: int = 450, rule: Callable[[int, int], dict[str, object] | None] = battle_row) -> str:
    """The CSV of the synthetic battle."""
    lines = [",".join(GB_COLUMNS)]
    for step in range(1, steps + 1):
        for human in GB_GANGS:
            row = rule(step, human)
            if row is not None:
                cells = {"step": step, "human": human, **row}
                lines.append(",".join(str(cells[name]) for name in GB_COLUMNS))
    return "\n".join(lines) + "\n"


@pytest.fixture
def battle() -> dict[str, Any]:
    return numbers(analyse(parse_fight_trace(battle_text())), GB_LABELS)


def test_the_battle_runs_from_the_brains_going_on_to_rumble_over_and_leaves_the_crowd_out() -> None:
    analysis = analyse(parse_fight_trace(battle_text()))
    assert analysis.first_step == 11
    assert analysis.end_step == 399
    assert set(analysis.humans) == {10, 11, 20, 21}
    flat = numbers(analysis)
    assert flat["match.length_s"] == pytest.approx(389 / 30)
    assert not any(key.startswith("g2.") for key in flat)  # only the two sides are labelled


def test_out_is_clip_349_the_wound_or_gone_and_the_side_left_standing_wins(battle: dict[str, Any]) -> None:
    assert battle["match.winner"] == "g0"
    assert battle["g1.out.at_end"] == 2
    assert battle["g0.out.at_end"] == 0
    assert battle["g1.ko.count"] == 2
    assert battle["g1.ko.first_s"] == pytest.approx((200 - 11) / 30)
    assert battle["g0.goal.fight"] > 0.9  # shares of standing time


def test_attacks_are_wind_up_starts_with_the_targets_distance(battle: dict[str, Any]) -> None:
    assert battle["g0.attack.total_per_min"] == pytest.approx(2 / SIDE1_MINUTES)
    assert battle["g1.attack.total_per_min"] == pytest.approx(1 / SIDE2_MINUTES)
    assert battle["g0.attack.distance_p50"] == pytest.approx(1.0)


def test_blocks_reactions_knock_downs_and_health_losses(battle: dict[str, Any]) -> None:
    assert battle["g0.block.per_min"] == pytest.approx(1 / SIDE1_MINUTES)  # the Blocking goal on top
    assert battle["g1.block.per_min"] == 0
    assert battle["g1.react.per_min"] == pytest.approx(1 / SIDE2_MINUTES)
    assert battle["g1.down.per_min"] == pytest.approx(1 / SIDE2_MINUTES)
    assert battle["g1.health.losses_per_min"] == pytest.approx(2 / SIDE2_MINUTES)


def test_target_changes_are_split_into_switches_sets_and_clears(battle: dict[str, Any]) -> None:
    assert battle["g0.target.switch_per_min"] == pytest.approx(1 / SIDE1_MINUTES)
    assert battle["g0.target.set_per_min"] == pytest.approx(1 / SIDE1_MINUTES)
    assert battle["g0.target.clear_per_min"] == pytest.approx(1 / SIDE1_MINUTES)
    assert battle["g0.target.changes_per_min"] == pytest.approx(3 / SIDE1_MINUTES)
    assert battle["g1.target.changes_per_min"] == 0


def test_fight_goals_are_runs_of_fight_on_top_split_where_its_updates_start_again(battle: dict[str, Any]) -> None:
    # Human 10: 11-149 and 150-399; human 11: 11-299 (the block above it does not split it) and 310-399.
    runs = sorted([139, 250, 289, 90])
    assert battle["g0.fight_goal.count"] == 4
    assert battle["g0.fight_goal.median_s"] == pytest.approx((runs[1] + runs[2]) / 2 / 30)
    assert battle["g0.fight_goal.share_4s"] == pytest.approx(3 / 4)


def test_fighters_on_one_enemy_and_active_places(battle: dict[str, Any]) -> None:
    assert battle["g0.attackers.max"] == 2
    # 137 updates with 10 and 11 both on 20 (11-149 less 120 and 121), 502 with one fighter on an enemy.
    assert battle["g0.attackers.share2"] == pytest.approx(137 / 639)
    assert battle["g0.attackers.share1"] + battle["g0.attackers.share2"] == pytest.approx(1.0)
    assert battle["g0.attackers.share4"] == 0
    assert battle["g1.active.share1"] == pytest.approx(1.0)
    assert battle["g0.active.share0"] == pytest.approx(1.0)


def test_waiting_distance_is_split_by_whether_the_target_fights_him(battle: dict[str, Any]) -> None:
    # 11 waits (no active place) on 20, who fights 10: about 3.16 m. 20 waits on 10, who fights 20 until 150.
    assert battle["g0.wait.other_p50"] == pytest.approx(10**0.5, abs=1e-3)
    assert "g0.wait.mutual_p50" not in battle
    assert battle["g0.wait.other_band"] == 0
    assert battle["g1.wait.mutual_p50"] == pytest.approx(1.0)


def test_taunts_help_and_idle(battle: dict[str, Any]) -> None:
    assert battle["g0.taunt.per_min"] == pytest.approx(1 / SIDE1_MINUTES)
    assert battle["g1.help.calls_per_min"] == pytest.approx(2 / SIDE2_MINUTES)
    assert battle["g1.help.helpers_per_min"] == pytest.approx(1 / SIDE2_MINUTES)
    assert battle["g1.help.respond_per_min"] == pytest.approx(1 / SIDE2_MINUTES)
    assert battle["g0.idle.share"] == pytest.approx(10 / 778)


def test_each_sides_tactic_and_when_it_changes(battle: dict[str, Any]) -> None:
    assert battle["g0.tactic.first"] == "warrior_attack"
    assert battle["g0.tactic.changes"] == 0
    assert battle["g1.tactic.first"] == "confront"
    assert battle["g1.tactic.last"] == "attack"
    assert battle["g1.tactic.changes"] == 1
    assert battle["g1.tactic.first_updates"] == 30
    assert battle["g0.teamups.max"] == 0
    assert battle["g0.order.share"] == 0
    assert battle["g0.chosen.share"] == 0


def test_without_brain_off_the_fight_ends_when_every_stack_is_empty_for_30_updates() -> None:
    def rule(step: int, human: int) -> dict[str, object] | None:
        row = battle_row(step, human)
        if row is not None and GB_GANGS[human] != 0 and step >= 400:
            row["goal"] = row["goal0"] = row["goal1"] = row["goal2"] = row["goal3"] = -1
        return row

    columns = [c for c in GB_COLUMNS if c != "brain_off"]
    text = battle_text(rule=rule)
    lines = text.splitlines()
    keep = [GB_COLUMNS.index(c) for c in columns]
    stripped = "\n".join(",".join(line.split(",")[i] for i in keep) for line in lines) + "\n"
    analysis = analyse(parse_fight_trace(stripped))
    assert analysis.end_step == 399
    # The first goal pushed starts the fight (the intro's stacks are empty).
    assert analysis.first_step == 11


def test_the_original_help_calls_come_from_the_hook_logs_beside_the_trace(tmp_path: Path) -> None:
    columns = [c for c in GB_COLUMNS if c not in ("help_calls", "helpers_sent")]
    lines = battle_text().splitlines()
    keep = [GB_COLUMNS.index(c) for c in columns]
    trace = write(tmp_path, "gb.csv", "\n".join(",".join(line.split(",")[i] for i in keep) for line in lines) + "\n")
    human20 = 0x00640C80 + 20 * 0x6D0
    brain21 = 0x006D53F0 + 21 * 0x2F0
    write(
        tmp_path,
        "gb.help-call.csv",
        f"seq,step,ticks,ra,human,a1,a2,a3\n1,61,0,0,{human20:#x},0,0,0\n2,500,0,0,0,0,0,0\n",
    )
    write(tmp_path, "gb.send-helper.csv", f"seq,step,ticks,ra,a0,a1,a2,a3\n1,65,0,0,{brain21:#x},0,0,0\n")
    flat = numbers(analyse(load_fight_trace(trace)), GB_LABELS)
    assert flat["g1.help.calls_per_min"] == pytest.approx(1 / SIDE2_MINUTES)  # the call at step 500 is after the end
    assert flat["g1.help.helpers_per_min"] == pytest.approx(1 / SIDE2_MINUTES)
    assert "g0.help.calls_per_min" in flat and flat["g0.help.calls_per_min"] == 0


def test_the_command_compares_each_orientation_against_its_own_spread(
    tmp_path: Path, capsys: pytest.CaptureFixture[str], fight: str
) -> None:
    one = write(tmp_path, "one.csv", fight)
    other = write(tmp_path, "other.csv", battle_text())
    args = ["trace", "fight-report", "--original", str(one), "--original", str(other)]
    assert cli.main([*args, "--coney", str(one), "--coney", str(other)]) == 0
    out = capsys.readouterr().out
    assert "orientation 1:" in out and "orientation 2:" in out
    # Each orientation against the other's spread differs.
    assert cli.main([*args, "--coney", str(other), "--coney", str(one)]) == 1
    capsys.readouterr()
    assert cli.main([*args, "--coney", str(one)]) == 2  # one Coney group for two orientations


def test_the_command_can_start_after_an_intro(tmp_path: Path, capsys: pytest.CaptureFixture[str], fight: str) -> None:
    trace = write(tmp_path, "a.csv", fight)
    assert cli.main(["trace", "fight-report", str(trace), "--from", "31"]) == 0
    assert "670 steps" in capsys.readouterr().out
    assert cli.main(["trace", "fight-report", str(trace), "--from", "9999"]) == 2


def test_out_is_the_down_or_dead_mask_dying_wounded_knocked_out_or_dead_and_not_down() -> None:
    # Rule 3: state & 0x80050000 or state_hi & 0x1 (Human_IsDownOrDead); a knock-down (0x80000) is not out.
    states = {1: (0x80000, 0), 2: (0x80000000, 0), 3: (0x10000, 0), 4: (0x40000, 0), 5: (0, 0x41)}
    text = "step,human,gang,state,state_hi\n" + "".join(
        f"{s},{h},1,{states[h][0] if s >= 5 else 0},{states[h][1] if s >= 5 else 0}\n"
        for s in range(1, 11)
        for h in states
    )
    trace = parse_fight_trace(text)
    out = {h: any(sample.ko for sample in trace.by_human[h]) for h in states}
    assert out == {1: False, 2: True, 3: True, 4: True, 5: True}


def test_the_set_target_hooks_callers_name_the_causes() -> None:
    # docs/research/gang-fights.md#set-target-causes
    assert set_target_cause(0x0029F298, 0x1234, 0) == "pick"
    assert set_target_cause(0x002B3CA8, None, 1) == "front"
    assert set_target_cause(0x0028D330, 0x00305200, 1) == "hit_back"
    assert set_target_cause(0x0028D330, 0x00291C6C, 0) == "help"
    assert set_target_cause(0x0028D330, None, 1) == "hit_back"  # ra2 not logged: by the force
    assert set_target_cause(0x0028D330, None, 0) == "help"
    assert set_target_cause(0x00226FD8, 0x0028FB30, 0) == "reaction"
    assert set_target_cause(0x00226FD8, 0x0030678C, 0) == "order"
    assert set_target_cause(0x00226FD8, None, 0) == "unknown"  # Human_DropTarget without ra2: any of five
    assert set_target_cause(0x0028E0FC, None, 0) == "displaced"
    assert set_target_cause(0x002FAD04, None, 0) == "hit_back"  # the attack action
    assert set_target_cause(0x002DCE08, None, 0) == "order"  # the item fight
    assert set_target_cause(0x002DCB08, None, 0) == "order"  # the item's holder
    assert set_target_cause(0x002B437C, None, 0) == "pick"  # Spectate
    assert set_target_cause(0x002B4310, None, 0) == "pick"
    assert set_target_cause(0x00292DFC, None, 0) == "order"  # the reset
    assert set_target_cause(0x002B6160, None, 0) == "reaction"  # a grab goal
    assert set_target_cause(0x00305220, 0x1234, 1) == "hit_back"  # a row for any ra2
    assert set_target_cause(0x002AEC88, None, 0) == "pick"  # Melee's own clear
    assert set_target_cause(0x00226CBC, 0x1234, 0) == "unknown"  # Human_SetTarget from a player's control
    assert set_target_cause(0x00123450, None, 0) == "unknown"  # a caller the table does not name yet


def test_target_changes_by_cause_come_from_coneys_column() -> None:
    cells = {1: "", 2: "pick=1", 3: "pick=2", 4: "pick=2;front=1", 5: "pick=2;front=1"}
    text = "step,human,gang,target,target_causes\n" + "".join(
        f"{s},0,1,{1 if s >= 2 else -1},{cell}\n{s},1,2,0,\n" for s, cell in cells.items()
    )
    flat = numbers(analyse(parse_fight_trace(text)), {1: "g0", 2: "g1"})
    minutes = 5 / 30 / 60
    assert flat["g0.target.cause.pick_per_min"] == pytest.approx(2 / minutes)
    assert flat["g0.target.cause.front_per_min"] == pytest.approx(1 / minutes)
    assert flat["g0.target.cause.help_per_min"] == 0
    assert flat["g1.target.cause.pick_per_min"] == 0


def test_the_original_target_causes_come_from_the_set_target_log_and_only_changes_count(tmp_path: Path) -> None:
    trace = write(tmp_path, "gb.csv", battle_text())
    brain10, brain20 = 0x006D53F0 + 10 * 0x2F0, 0x006D53F0 + 20 * 0x2F0
    human10, human20, human21 = (0x00640C80 + h * 0x6D0 for h in (10, 20, 21))
    rows: list[tuple[int, int, int, int, int, int | None]] = [
        (20, 0x0029F368, brain10, human20, 0, 0),  # pick: a change
        (21, 0x00226CBC, brain10, human20, 0, 0x00226F5C),  # the same target again: none
        (150, 0x0028D330, brain10, human21, 1, 0x00305200),  # hit back
        (30, 0x0028D330, brain20, human10, 0, None),  # help (force 0, ra2 not logged)
        (35, 0x00226FD8, brain20, 0, 0, 0x0030678C),  # a tactic starting drops it
        (500, 0x0029F298, brain10, 0, 0, 0),  # after the end
    ]
    log = "seq,step,ticks,ra,brain,human,force,ra2\n" + "".join(
        f"{i},{step},0,{ra:#x},{brain:#x},{human:#x},{force},{'' if ra2 is None else hex(ra2)}\n"
        for i, (step, ra, brain, human, force, ra2) in enumerate(rows, start=1)
    )
    write(tmp_path, "gb.set-target.csv", log)
    flat = numbers(analyse(load_fight_trace(trace)), GB_LABELS)
    assert flat["g0.target.cause.pick_per_min"] == pytest.approx(1 / SIDE1_MINUTES)
    assert flat["g0.target.cause.hit_back_per_min"] == pytest.approx(1 / SIDE1_MINUTES)
    assert flat["g0.target.cause.other_per_min"] == 0
    assert flat["g0.target.cause.unknown_per_min"] == 0
    assert flat["g1.target.cause.help_per_min"] == pytest.approx(1 / SIDE2_MINUTES)
    assert flat["g1.target.cause.order_per_min"] == pytest.approx(1 / SIDE2_MINUTES)
