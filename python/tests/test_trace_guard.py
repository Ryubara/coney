# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for the standing-player guard of `trace coney` (#1680): a [coney] side that records a player who never moves
during a stick that should move him is refused, so a scenario cannot compare a standing player again.

The traces are synthetic CSVs; the scenario files are the repository's own."""

from __future__ import annotations

import subprocess
from collections.abc import Callable
from pathlib import Path

import pytest

from coney_tools import trace_cli
from coney_tools.input_script import parse_input_script
from coney_tools.scenario import ScenarioError, load_scenario
from coney_tools.trace_diff import load_trace
from coney_tools.trace_guard import first_moving_step, standing_player

REPO = Path(__file__).resolve().parents[2]

WALK = "40 stick left 0 60\n140 stick left 0 0\n"


def _trace(tmp_path: Path, positions: list[tuple[float, float, float]]) -> Path:
    """A trace CSV of `positions` (x, y, z), one row per step from step 1."""
    rows = [f"{step},{x:.4f},{y:.4f},{z:.4f}" for step, (x, y, z) in enumerate(positions, start=1)]
    path = tmp_path / "trace.csv"
    path.write_text("step,x,y,z\n" + "\n".join(rows) + "\n", encoding="utf-8")
    return path


def test_the_stick_that_should_move_him_is_the_first_left_stick_past_the_dead_zone() -> None:
    # Frame N is step N + 1; a stick at rest, a right stick and a button do not count.
    script = "5 stick left 0 0\n10 stick right 100 0\n12 tap cross\n20 stick left 10 10\n30 stick left -40 0\n"
    assert first_moving_step(parse_input_script(script)) == 31
    assert first_moving_step(parse_input_script("1 tap cross\n2 stick left 0 0\n")) is None
    assert first_moving_step([]) is None
    # Only player 1's pad moves him.
    assert first_moving_step(parse_input_script("3 p2 stick left 0 100\n")) is None


def test_a_player_who_stands_through_the_input_is_refused(tmp_path: Path) -> None:
    trace = load_trace(_trace(tmp_path, [(75.0, 41.0, 0.2)] * 170))
    message = standing_player(parse_input_script(WALK), trace)
    assert message is not None
    # It names the step the input starts at, how far he went, and the cause to look for.
    assert "step 41" in message
    assert "0.000 m" in message
    assert "wait_for_control" in message


def test_a_player_who_walks_passes(tmp_path: Path) -> None:
    standing = [(75.0, 41.0, 0.2)] * 40
    walking = [(75.0, 41.0 + 0.05 * step, 0.2) for step in range(1, 131)]
    trace = load_trace(_trace(tmp_path, standing + walking))
    assert standing_player(parse_input_script(WALK), trace) is None


def test_a_fall_counts_as_moving(tmp_path: Path) -> None:
    # A player dropped from a height and held still by the stick's dead zone still moves in z.
    positions = [(1.0, 2.0, 40.0)] * 40 + [(1.0, 2.0, 40.0 - 0.5 * step) for step in range(1, 61)]
    trace = load_trace(_trace(tmp_path, positions))
    assert standing_player(parse_input_script("10 stick left 0 100\n"), trace) is None


def test_a_player_who_stood_before_the_input_and_walks_after_it_passes(tmp_path: Path) -> None:
    # The run before the input started may stand still; only the stretch from the stick on is judged.
    positions = [(5.0, 5.0, 0.0)] * 40 + [(5.0 + 0.1 * step, 5.0, 0.0) for step in range(1, 21)]
    trace = load_trace(_trace(tmp_path, positions))
    assert standing_player(parse_input_script("40 stick left 100 0\n"), trace) is None


def test_a_player_who_moved_only_before_the_stick_is_refused(tmp_path: Path) -> None:
    # He walks in a scene, then stands for the whole input: the stretch before the stick must not count.
    positions = [(0.1 * step, 0.0, 0.0) for step in range(1, 41)] + [(4.0, 0.0, 0.0)] * 60
    trace = load_trace(_trace(tmp_path, positions))
    assert standing_player(parse_input_script("40 stick left 100 0\n"), trace) is not None


def test_a_script_with_no_stick_has_nothing_to_check(tmp_path: Path) -> None:
    trace = load_trace(_trace(tmp_path, [(0.0, 0.0, 0.0)] * 30))
    assert standing_player(parse_input_script("5 tap cross\n"), trace) is None


def test_a_trace_that_ends_before_the_input_is_refused(tmp_path: Path) -> None:
    trace = load_trace(_trace(tmp_path, [(0.0, 0.0, 0.0)] * 10))
    message = standing_player(parse_input_script("40 stick left 0 100\n"), trace)
    assert message is not None
    assert "ends" in message


def test_a_trace_without_positions_is_refused(tmp_path: Path) -> None:
    path = tmp_path / "trace.csv"
    path.write_text("step,speed\n1,0.0\n2,0.0\n", encoding="utf-8")
    message = standing_player(parse_input_script("0 stick left 0 100\n"), load_trace(path))
    assert message is not None
    assert "x, y and z" in message


# --- `trace coney` runs the guard ------------------------------------------------------------------------------------


def _fake_coney(
    positions: list[tuple[float, float, float]], seen: list[list[str]]
) -> Callable[..., subprocess.CompletedProcess[str]]:
    """A stand-in for `subprocess.run` that records the command and writes the trace Coney would write."""

    def run(command: list[str], **_: object) -> subprocess.CompletedProcess[str]:
        seen.append(command)
        out = Path(command[command.index("--trace") + 1])
        rows = [f"{step},{x},{y},{z}" for step, (x, y, z) in enumerate(positions, start=1)]
        out.write_text("step,x,y,z\n" + "\n".join(rows) + "\n", encoding="utf-8")
        return subprocess.CompletedProcess(command, 0, "", "")

    return run


@pytest.mark.reads("docs_only")
def test_trace_coney_exits_1_for_a_standing_player(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    path = REPO / "research/traces/scenarios/walk60.toml"
    scenario = load_scenario(path, REPO)
    seen: list[list[str]] = []
    standing = [(1.0, 2.0, 0.2)] * (scenario.updates + scenario.coney_settle)
    monkeypatch.setattr("coney_tools.trace_cli.subprocess.run", _fake_coney(standing, seen))
    out = tmp_path / "walk60.csv"
    code = trace_cli.run_coney(path, out, Path("coney"), "H:/")
    assert code == 1
    error = capsys.readouterr().err
    assert "walk60" in error
    assert "never moves" in error
    # The trace stays, for the person who reads why.
    assert out.is_file()


@pytest.mark.reads("docs_only")
def test_trace_coney_accepts_a_walking_player(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    scenario = load_scenario(REPO / "research/traces/scenarios/walk60.toml", REPO)
    walking = [(1.0, 2.0 + 0.05 * step, 0.2) for step in range(scenario.updates + scenario.coney_settle)]
    seen: list[list[str]] = []
    monkeypatch.setattr("coney_tools.trace_cli.subprocess.run", _fake_coney(walking, seen))
    out = tmp_path / "walk60.csv"
    assert trace_cli.run_coney(scenario.path, out, Path("coney"), "H:/") == 0


# --- waiting for the player's control ---------------------------------------------------------------------------------


def _scenario_file(tmp_path: Path, coney_table: str) -> Path:
    """A minimal scenario whose [coney] table is `coney_table`, beside the input script it names."""
    (tmp_path / "walk.txt").write_text(WALK, encoding="utf-8")
    path = tmp_path / "scenario.toml"
    path.write_text(
        'description = "test"\ninput = "walk.txt"\nupdates = 170\nfields = ["player"]\n\n[original]\nslot = 1\n\n'
        f"[coney]\n{coney_table}\n",
        encoding="utf-8",
    )
    return path


@pytest.mark.reads("docs_only")
def test_a_scenario_can_wait_for_the_players_control(tmp_path: Path) -> None:
    path = _scenario_file(tmp_path, 'level = "level99"\nargs = ["--checkpoint", "3"]\nwait_for_control = true')
    scenario = load_scenario(path, REPO)
    assert scenario.coney_wait is True
    command = trace_cli.coney_command(scenario, Path("coney"), "H:/", Path("out.csv"))
    assert "--wait-for-control" in command
    # The option comes with the level's own arguments, before the run's frames.
    assert command.index("--play-level") < command.index("--wait-for-control") < command.index("--frames")


@pytest.mark.reads("docs_only")
def test_a_scenario_does_not_wait_unless_it_says_so(tmp_path: Path) -> None:
    scenario = load_scenario(_scenario_file(tmp_path, 'level = "level99"'), REPO)
    assert scenario.coney_wait is False
    assert "--wait-for-control" not in trace_cli.coney_command(scenario, Path("coney"), "H:/", Path("out.csv"))


@pytest.mark.reads("docs_only")
def test_wait_for_control_is_a_true_or_false(tmp_path: Path) -> None:
    path = _scenario_file(tmp_path, 'level = "level99"\nwait_for_control = "yes"')
    with pytest.raises(ScenarioError, match="wait_for_control"):
        load_scenario(path, REPO)


@pytest.mark.reads("docs_only")
def test_wait_for_control_needs_a_level(tmp_path: Path) -> None:
    with pytest.raises(ScenarioError, match="wait_for_control"):
        load_scenario(_scenario_file(tmp_path, "wait_for_control = true"), REPO)


# --- the repository's own scenarios ------------------------------------------------------------------------------


@pytest.mark.reads("docs_only")
def test_every_level99_scenario_that_moves_the_player_waits_for_control() -> None:
    # Level99's checkpoints 1, 2 and 3 open with a scene (l99_c1, l99_c2, l99_c3) that holds the pad, and checkpoint 3's
    # overrides --start and leaves the pad off until about update 750, so a scenario there that does not wait for
    # control records a standing player (#1680). Checkpoint 4 and the levels after it open with no such scene.
    checked = 0
    for path in sorted((REPO / "research/traces/scenarios").glob("*.toml")):
        try:
            scenario = load_scenario(path, REPO)
        except ScenarioError:
            continue  # not a trace scenario (a mission course, a patch file)
        args = list(scenario.coney_args)
        checkpoint = args[args.index("--checkpoint") + 1] if "--checkpoint" in args else "1"
        if scenario.coney_level != "level99" or checkpoint not in ("1", "2", "3"):
            continue
        # A scenario that only watches (no stick, no --start) may record the opening; one that places him with --start
        # (the fall scenarios have no stick) or moves him needs him free.
        if first_moving_step(scenario.events) is None and "--start" not in args:
            continue
        checked += 1
        assert scenario.coney_wait, (
            f"{path.name}: level99 checkpoint {checkpoint} needs wait_for_control = true (#1680)"
        )
        assert scenario.coney_settle != 300, f"{path.name}: a settle of 300 is the old stand-in for waiting for control"
    assert checked >= 28
