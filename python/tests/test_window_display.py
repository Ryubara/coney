# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for CONEY_WINDOW_DISPLAY in the tools: reading the value, picking the display, fitting a window, and the
placer that moves each PCSX2 window there once, against a fake desktop."""

from __future__ import annotations

import sys

import pytest

from coney_tools import pcsx2_proc, window_display
from coney_tools.pcsx2_proc import PlacerCalls, WindowPlacer
from coney_tools.window_display import Choice, Display, Rect, parse_choice, pick_display, place_window

# The desk this was made for, in SDL's order (the primary first): a 2560 x 1440 primary, a 1080 x 1920 portrait
# display left of it and a 1920 x 1080 one to the right. Usable areas leave room for a taskbar.
DESK = [
    Display(r"\\.\DISPLAY1 (DELL U2724D)", Rect(0, 0, 2560, 1440), Rect(0, 0, 2560, 1392)),
    Display(r"\\.\DISPLAY2 (LG HDR 4K)", Rect(-1080, 249, 1080, 1920), Rect(-1080, 249, 1080, 1872)),
    Display(r"\\.\DISPLAY10 (Generic PnP Monitor)", Rect(3840, 0, 1920, 1080), Rect(3840, 0, 1920, 1032)),
]


def test_an_empty_value_is_no_choice() -> None:
    assert parse_choice(None) is None
    assert parse_choice("") is None
    assert parse_choice(" \t") is None


@pytest.mark.parametrize(("text", "expected"), [(" 2 ", 1), ("1", 0), ("3", 2), ("0", None), ("4", None)])
def test_digits_are_a_one_based_index(text: str, expected: int | None) -> None:
    choice = parse_choice(text)
    assert choice is not None and choice.kind == "index"
    assert pick_display(choice, DESK) == expected


def test_a_huge_index_matches_nothing() -> None:
    choice = parse_choice("99999999999999999999")
    assert choice == Choice("index", index=0)
    assert pick_display(choice, DESK) is None


@pytest.mark.parametrize("text", ["portrait", "Portrait", "PORTRAIT"])
def test_portrait_picks_the_first_display_taller_than_wide(text: str) -> None:
    choice = parse_choice(text)
    assert choice == Choice("portrait")
    assert pick_display(choice, DESK) == 1
    assert pick_display(choice, [DESK[0], DESK[2]]) is None


def test_other_text_matches_part_of_a_name() -> None:
    assert pick_display(Choice("name", name="pnp"), DESK) == 2
    assert pick_display(parse_choice("DISPLAY2") or Choice("portrait"), DESK) == 1
    assert pick_display(parse_choice("lg hdr") or Choice("portrait"), DESK) == 1
    assert pick_display(Choice("name", name="Samsung"), DESK) is None


def test_a_window_that_fits_is_centred_at_its_own_size() -> None:
    assert place_window(DESK[1].usable, 960, 720) == Rect(-1020, 825, 960, 720)
    assert place_window(Rect(10, 20, 640, 448), 640, 448) == Rect(10, 20, 640, 448)


def test_a_window_too_big_is_scaled_down_keeping_its_shape() -> None:
    assert place_window(DESK[1].usable, 1920, 1080) == Rect(-1080, 881, 1080, 607)
    assert place_window(Rect(0, 0, 1080, 1000), 1000, 2000) == Rect(290, 0, 500, 1000)
    assert place_window(Rect(0, 0, 100, 100), 10000, 1) == Rect(0, 49, 100, 1)


def test_an_empty_area_keeps_the_size_and_a_sizeless_window_is_refused() -> None:
    assert place_window(Rect(5, 6, 0, 0), 960, 720) == Rect(5, 6, 960, 720)
    with pytest.raises(ValueError):
        place_window(DESK[1].usable, 0, 720)


class FakeDesktop:
    """The windows of one process; move() records each move and makes it real."""

    def __init__(self) -> None:
        """One PCSX2 window on the primary display."""
        self.windows: dict[int, Rect] = {0x10: Rect(100, 100, 1296, 798)}
        self.moves: list[tuple[int, Rect]] = []

    def calls(self) -> PlacerCalls:
        """The placer's calls on this desktop."""
        return PlacerCalls(top_windows=lambda pid: list(self.windows.items()), move=self.move)

    def move(self, hwnd: int, rect: Rect) -> None:
        """Move a window."""
        self.moves.append((hwnd, rect))
        self.windows[hwnd] = rect


def test_placer_moves_each_new_window_once() -> None:
    desktop = FakeDesktop()
    placer = WindowPlacer(desktop.calls(), DESK[1].usable)
    assert placer.check() == 0  # no process yet
    placer.pid = 4242
    assert placer.check() == 1
    assert desktop.moves == [(0x10, Rect(-1080, 852, 1080, 665))]  # 1296 x 798 shrunk to the 1080 width
    # The user drags it back: it stays. A second window (the game's) is moved when it appears.
    desktop.windows[0x10] = Rect(0, 0, 1296, 798)
    desktop.windows[0x20] = Rect(0, 0, 640, 480)
    assert placer.check() == 1
    assert desktop.moves[-1] == (0x20, Rect(-860, 945, 640, 480))
    assert desktop.windows[0x10] == Rect(0, 0, 1296, 798)


def test_no_placer_without_the_variable(monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.delenv(window_display.ENV, raising=False)
    assert pcsx2_proc.window_placer() is None


def test_placer_on_the_named_display(monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.setenv(window_display.ENV, "portrait")
    monkeypatch.setattr(sys, "platform", "win32")
    monkeypatch.setattr(pcsx2_proc, "list_displays", lambda: DESK)
    placer = pcsx2_proc.window_placer()
    assert placer is not None and placer.area == DESK[1].usable


def test_a_value_that_matches_nothing_warns_once(
    monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    monkeypatch.setenv(window_display.ENV, "Samsung")
    monkeypatch.setattr(sys, "platform", "win32")
    monkeypatch.setattr(pcsx2_proc, "list_displays", lambda: DESK)
    monkeypatch.setattr(pcsx2_proc, "_warned_display", False)
    assert pcsx2_proc.window_placer() is None
    assert pcsx2_proc.window_placer() is None
    err = capsys.readouterr().err
    assert err.count("matches no display") == 1
    assert '2 "\\\\.\\DISPLAY2 (LG HDR 4K)" 1080x1920' in err
