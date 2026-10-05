# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for the trace tools' Coney side: input scripts read as Coney reads them, and the trace diff, on synthetic
CSVs."""

from __future__ import annotations

import math
from pathlib import Path

import pytest

from coney_tools import cli, trace_cli
from coney_tools.input_script import (
    BUTTONS,
    InputScriptError,
    ScriptedPad,
    parse_input_script,
    stick_byte,
)
from coney_tools.scenario import load_scenario
from coney_tools.trace_diff import TraceError, compare, load_trace, report, to_start_frame

REPO = Path(__file__).resolve().parents[2]

# --- input scripts ---------------------------------------------------------------------------------------------------


def test_stick_bytes_match_coneys() -> None:
    # As tests/core/input_script_test.cpp: the ends, the centre, and the steps beside the dead zone.
    assert stick_byte(100) == 255
    assert stick_byte(-100) == 0
    assert stick_byte(0) == 0x80
    assert stick_byte(60) == 160 + 57
    assert stick_byte(-60) == 95 - 57
    assert stick_byte(1) == 161
    with pytest.raises(ValueError):
        stick_byte(101)


def test_scripts_parse_as_coney_parses_them() -> None:
    events = parse_input_script(
        "# a comment\n\n10 p2 press cross l2  # held\n20 stick right -50 100\r\n30 disconnect\n"
    )
    assert [(e.frame, e.port, e.action) for e in events] == [(10, 1, "press"), (20, 0, "stick"), (30, 0, "disconnect")]
    assert events[0].buttons == BUTTONS["cross"] | BUTTONS["l2"]
    assert (events[1].stick, events[1].x, events[1].y) == (1, -50, 100)


@pytest.mark.parametrize(
    ("text", "message"),
    [
        ("x tap cross", "not a frame number"),
        ("-1 tap cross", "not a frame number"),
        ("10", "needs an action"),
        ("10 jump", 'unknown action "jump"'),
        ("10 tap", "at least one button"),
        ("10 tap x", 'unknown button "x"'),
        ("10 stick left 0", "left or right, then X and Y"),
        ("10 stick left 0 101", "from -100 to 100"),
        ("10 stick left +5 0", "from -100 to 100"),
        ("10 connect now", "takes no arguments"),
        ("20 tap cross\n10 tap cross", "line 2: frame 10 comes after frame 20"),
    ],
)
def test_bad_lines_are_named(text: str, message: str) -> None:
    with pytest.raises(InputScriptError, match=message):
        parse_input_script(text)


def test_the_pad_plays_taps_for_one_frame_and_turns_y_down() -> None:
    pad = ScriptedPad(parse_input_script("1 tap cross\n2 tap cross\n3 stick left 0 60\n5 press r1\n"))
    held = [pad.advance(frame).buttons for frame in range(5)]
    assert held == [0, BUTTONS["cross"], BUTTONS["cross"], 0, 0]
    state = pad.advance(5)
    assert state.buttons == BUTTONS["r1"]
    # Right x, right y, left x, left y; 60 % up is a small raw y.
    assert state.sticks == [0x80, 0x80, 0x80, 95 - 57]
    # Buttons active low: R1 is bit 3 of the second byte.
    assert state.raw() == bytes([0xFF, 0xFF & ~0x08, 0x80, 0x80, 0x80, 95 - 57])
    with pytest.raises(ValueError):
        pad.advance(4)


def test_a_skipped_frame_still_applies_its_lines() -> None:
    pad = ScriptedPad(parse_input_script("1 press square\n2 release square\n2 stick right 100 0\n"))
    assert pad.advance(4).buttons == 0
    assert pad.ports[0].sticks[0] == 255


def test_coneys_own_test_scripts_parse() -> None:
    # The scripts Coney's tests play: the reader must take every line Coney takes.
    for path in sorted((REPO / "tests/support").glob("*.txt")):
        parse_input_script(path.read_text(encoding="utf-8"))


# --- the diff --------------------------------------------------------------------------------------------------------


def _csv(path: Path, header: str, rows: list[str]) -> Path:
    """Write a trace CSV and return its path."""
    path.write_text(header + "\n" + "\n".join(rows) + "\n", encoding="utf-8")
    return path


def test_identical_traces_compare_clean(tmp_path: Path) -> None:
    rows = [f"{s},{s * 0.1:.4f},388" for s in range(1, 11)]
    a = load_trace(_csv(tmp_path / "a.csv", "step,x,clip", rows))
    b = load_trace(_csv(tmp_path / "b.csv", "step,x,clip,traversal", [r + ",none" for r in rows]))
    comparison = compare(a, b)
    assert comparison.ok
    assert [c.name for c in comparison.columns] == ["x", "clip"]
    assert (comparison.start, comparison.end) == (1, 10)


def test_the_first_divergence_and_the_largest_error_are_found(tmp_path: Path) -> None:
    a = load_trace(_csv(tmp_path / "a.csv", "step,speed,clip", [f"{s},1.0,388" for s in range(1, 21)]))
    b_rows = [
        f"{s},{1.0 + (0.5 if s >= 12 else 0.0) + (0.2 if s == 15 else 0.0)},{413 if s >= 8 else 388}"
        for s in range(1, 21)
    ]
    b = load_trace(_csv(tmp_path / "b.csv", "step,speed,clip", b_rows))
    comparison = compare(a, b, tolerances={"speed": 0.1})
    speed, clip = comparison.columns
    assert (speed.first, speed.max_step) == (12, 15)
    assert math.isclose(speed.max_error, 0.7)
    assert clip.first == 8 and clip.tolerance == 0
    assert [step for step, _, _ in clip.around] == [5, 6, 7, 8, 9, 10, 11]
    text = report(comparison)
    assert "step 12" in text and "clip: first divergence at step 8" in text
    assert not comparison.ok
    # A wider tolerance, a later start or a shift change the verdict.
    assert compare(a, b, ["speed"], {"speed": 1.0}).ok
    assert compare(a, b, ["clip"], start=1, end=7).ok


def test_a_shift_aligns_a_late_trace(tmp_path: Path) -> None:
    a = load_trace(_csv(tmp_path / "a.csv", "step,clip", [f"{s},{413 if s >= 5 else 388}" for s in range(1, 11)]))
    b = load_trace(_csv(tmp_path / "b.csv", "step,clip", [f"{s},{413 if s >= 7 else 388}" for s in range(1, 13)]))
    assert not compare(a, b).ok
    assert compare(a, b, shift=2).ok


def test_headings_compare_round_the_circle(tmp_path: Path) -> None:
    a = load_trace(_csv(tmp_path / "a.csv", "step,heading", ["1,179.5"]))
    b = load_trace(_csv(tmp_path / "b.csv", "step,heading", ["1,-179.5"]))
    assert math.isclose(compare(a, b).columns[0].max_error, 1.0)


def test_missing_steps_are_skipped_and_counted(tmp_path: Path) -> None:
    a = load_trace(_csv(tmp_path / "a.csv", "step,x", ["1,0", "2,0", "4,0"]))
    b = load_trace(_csv(tmp_path / "b.csv", "step,x", ["1,0", "2,0", "3,9", "4,0"]))
    comparison = compare(a, b)
    assert comparison.ok and comparison.missing == [3]


def test_the_start_frame_turns_each_walk_into_forward_metres(tmp_path: Path) -> None:
    # The original walks along -x from (75, 41) facing 90 degrees; Coney along +y from (-284, 120) facing 0.
    header = "step,x,y,z,heading,cam_x,cam_y,cam_z"
    a = _csv(tmp_path / "a.csv", header, [f"{s},{75 - s:.1f},41,0.2,90,{80 - s:.1f},41,2.0" for s in range(1, 6)])
    b = _csv(tmp_path / "b.csv", header, [f"{s},-284,{120 + s:.1f},0.3,0,-284,{115 + s:.1f},2.1" for s in range(1, 6)])
    original, coney = to_start_frame(load_trace(a), 1), to_start_frame(load_trace(b), 1)
    comparison = compare(original, coney, tolerances={"*": 1e-6})
    assert comparison.ok, report(comparison)
    assert math.isclose(original.rows[5]["y"] or 0.0, 4.0)
    assert math.isclose(original.rows[5]["cam_y"] or 0.0, -1.0, abs_tol=1e-9)
    with pytest.raises(TraceError, match="start frame"):
        to_start_frame(load_trace(a), 99)


def test_bad_traces_are_named(tmp_path: Path) -> None:
    with pytest.raises(TraceError, match="no step column"):
        load_trace(_csv(tmp_path / "a.csv", "x,y", ["1,2"]))
    with pytest.raises(TraceError, match="appears twice"):
        load_trace(_csv(tmp_path / "b.csv", "step,x", ["1,2", "1,3"]))
    good = load_trace(_csv(tmp_path / "c.csv", "step,x", ["1,2"]))
    with pytest.raises(TraceError, match="no column y"):
        compare(good, good, ["y"])


def test_the_command_exits_1_outside_tolerance(tmp_path: Path, capsys: pytest.CaptureFixture[str]) -> None:
    a = _csv(tmp_path / "a.csv", "step,speed", ["1,1.0", "2,1.0"])
    b = _csv(tmp_path / "b.csv", "step,speed", ["1,1.0", "2,1.5"])
    assert cli.main(["trace", "diff", str(a), str(b)]) == 1
    assert "step 2" in capsys.readouterr().out
    assert cli.main(["trace", "diff", str(a), str(b), "--tolerance", "speed=0.6"]) == 0
    assert cli.main(["trace", "diff", str(a), str(b), "--tolerance", "*=0.6", "--to", "1"]) == 0
    assert cli.main(["trace", "diff", str(a), str(tmp_path / "missing.csv")]) == 2


def test_a_scenario_gives_coneys_command_line() -> None:
    scenario = load_scenario(REPO / "research/traces/scenarios/walk60.toml", REPO)
    command = trace_cli.coney_command(scenario, Path("coney"), "H:/", Path("out.csv"))
    assert command[:5] == ["coney", "--disc", "H:/", "--play-level", "level99"]
    assert command[command.index("--frames") + 1] == str(scenario.updates)
    assert command[command.index("--input-script") + 1] == str(scenario.input_path)
    assert scenario.first_input_step() == scenario.events[0].frame + 1
