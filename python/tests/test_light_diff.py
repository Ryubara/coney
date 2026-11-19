# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for coney_tools.light_diff (`trace diff` on two selections files): the original's light selections from
`trace lights` against a frame of Coney's lighting lab (*Dump this frame*), matched by caller and sphere centre and
compared list by list (docs/guides/realtime-lighting.md#test-selection). Every light and number is made up; the
callers' return addresses are the ones docs/research/lighting.md#select names."""

from __future__ import annotations

from pathlib import Path

import pytest

from coney_tools import light_diff
from coney_tools.cli import main
from coney_tools.light_trace import Call, Light, selections_csv

OBJECT = 0x001803A4
SECTOR = 0x00411AA8
HUMAN = 0x001746F8

AMBIENT = Light(2, 0.0, (0.25, 0.25, 0.25, 1.0), (0.0, 0.0, 0.0))
MOON = Light(1, 0.0, (0.25, 0.25, 0.375, 1.0), (0.0, 0.0, 0.0))
LAMP = Light(0x80, 3.0, (0.75, 0.75, 0.5, 1.0), (3.0, 1.0, 0.0))
OTHER_LAMP = Light(0x80, 6.0, (0.5, 0.5, 0.5, 1.0), (-8.0, 1.0, 4.0))


def call(caller: int, centre: tuple[float, float, float], chosen: tuple[Light, ...]) -> Call:
    """A call of `caller` for an atomic at `centre` that chose `chosen`; its lists play no part in the diff."""
    return Call(caller, 0.0, (*centre, 1.0), 1, True, False, caller == HUMAN, None, 0, {}, chosen)


def write(tmp_path: Path, name: str, calls: list[Call]) -> Path:
    """The selections file of `calls`, in the columns both games write."""
    path = tmp_path / name
    path.write_text(selections_csv(calls), encoding="utf-8")
    return path


def original_calls() -> list[Call]:
    """Three atomics: a sector, an object and a human."""
    return [
        call(SECTOR, (0.0, 0.0, 0.0), (AMBIENT, LAMP)),
        call(OBJECT, (10.0, 0.0, 5.0), (MOON, AMBIENT, LAMP)),
        call(HUMAN, (2.0, 1.0, 2.0), (MOON, AMBIENT)),
    ]


def test_atomics_are_matched_by_caller_and_sphere_centre_within_5_cm(tmp_path: Path) -> None:
    original = original_calls()
    # Coney draws them in another order, 3 cm off, and one more atomic the original's trace did not reach.
    coney = [
        call(HUMAN, (2.03, 1.0, 2.0), (MOON, AMBIENT)),
        call(OBJECT, (10.0, 0.0, 5.03), (MOON, AMBIENT, LAMP)),
        call(SECTOR, (0.0, 0.03, 0.0), (AMBIENT, LAMP)),
        call(OBJECT, (40.0, 0.0, 0.0), (MOON, AMBIENT)),
    ]
    result = light_diff.compare(
        light_diff.load(write(tmp_path, "a.csv", original)), light_diff.load(write(tmp_path, "b.csv", coney))
    )
    assert (result.original, result.coney, result.matched) == (3, 4, 3)
    assert result.equal == 3
    assert result.differences == []
    assert result.ok


def test_a_sphere_further_than_5_cm_or_another_caller_does_not_match(tmp_path: Path) -> None:
    original = original_calls()
    coney = [
        call(SECTOR, (0.0, 0.06, 0.0), (AMBIENT, LAMP)),  # 6 cm away
        call(HUMAN, (10.0, 0.0, 5.0), (MOON, AMBIENT, LAMP)),  # the object's place, another caller
    ]
    result = light_diff.compare(
        light_diff.load(write(tmp_path, "a.csv", original)), light_diff.load(write(tmp_path, "b.csv", coney))
    )
    assert result.matched == 0
    assert not result.ok


def test_each_matched_list_that_differs_is_reported_with_its_first_slot(tmp_path: Path) -> None:
    original = original_calls()
    moved = Light(0x80, 3.0, (0.75, 0.75, 0.5, 1.0), (3.02, 1.0, 0.0))  # 2 cm off: beyond 1 cm
    dimmer = Light(0x80, 3.0, (0.74, 0.75, 0.5, 1.0), (3.0, 1.0, 0.0))  # 0.01 darker: beyond 1/255
    coney = [
        call(SECTOR, (0.0, 0.0, 0.0), (AMBIENT, moved)),
        call(OBJECT, (10.0, 0.0, 5.0), (MOON, AMBIENT, dimmer)),
        call(HUMAN, (2.0, 1.0, 2.0), (MOON, AMBIENT, OTHER_LAMP)),
    ]
    result = light_diff.compare(
        light_diff.load(write(tmp_path, "a.csv", original)), light_diff.load(write(tmp_path, "b.csv", coney))
    )
    assert result.matched == 3
    assert result.equal == 0
    assert [(d.row, d.slot) for d in result.differences] == [(0, 1), (1, 2), (2, 2)]
    text = light_diff.report(result)
    assert "atomics: original 3, Coney 3, matched 3; lists equal 0, different 3" in text
    assert "row 0 (sector) slot 1: point (3, 1, 0) r 3 against point (3.02, 1, 0) r 3" in text
    assert "row 2 (human) slot 2: none against point (-8, 1, 4) r 6" in text


def test_lights_within_1_cm_and_half_a_colour_step_are_the_same() -> None:
    near = Light(0x80, 3.004, (0.75 + 0.4 / 255, 0.75, 0.5, 1.0), (3.008, 1.0, 0.0))
    assert light_diff.same_light(LAMP, near)
    assert not light_diff.same_light(LAMP, Light(0x81, 3.0, LAMP.colour, LAMP.position))


def test_a_file_in_other_columns_is_refused(tmp_path: Path) -> None:
    path = tmp_path / "steps.csv"
    path.write_text("step,x,y\n1,0,0\n", encoding="utf-8")
    with pytest.raises(light_diff.LightDiffError, match="not a selections file"):
        light_diff.load(path)
    assert not light_diff.is_selections(path)
    assert light_diff.is_selections(write(tmp_path, "a.csv", original_calls()))


def test_trace_diff_compares_two_selections_files_and_exits_1_on_a_difference(
    tmp_path: Path, capsys: pytest.CaptureFixture[str]
) -> None:
    original = write(tmp_path, "original.csv", original_calls())
    same = write(tmp_path, "same.csv", original_calls())
    assert main(["trace", "diff", str(original), str(same)]) == 0
    assert "lists equal 3, different 0" in capsys.readouterr().out

    other = original_calls()
    other[1] = call(OBJECT, (10.0, 0.0, 5.0), (MOON, AMBIENT))
    assert main(["trace", "diff", str(original), str(write(tmp_path, "other.csv", other))]) == 1
    assert "row 1 (object) slot 2: point (3, 1, 0) r 3 against none" in capsys.readouterr().out
