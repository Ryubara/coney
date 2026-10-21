# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for coney_tools.recordings_make: an input script written as a PCSX2 `.p2m2` recording. No PCSX2 and no game
data: the savestates here are made-up bytes."""

from __future__ import annotations

from pathlib import Path

import pytest

from coney_tools import pcsx2_copies, recordings_make
from coney_tools.config import ConfigError
from coney_tools.debug_server import P2M2_FRAME_BYTES, parse_p2m2, savestate_of
from coney_tools.input_script import PadState, parse_input_script, stick_byte

HEADER = 570  # 1 + 50 + 255 + 255 + 4 + 4 + 1


def frame(data: bytes, index: int, port: int = 0) -> bytes:
    """The 18 bytes of one port in one frame of a recording's bytes."""
    start = HEADER + index * P2M2_FRAME_BYTES + 18 * port
    return data[start : start + 18]


def test_a_pad_at_rest_is_all_buttons_up_and_the_sticks_centred() -> None:
    assert recordings_make.pad_bytes(PadState()) == bytes([0xFF, 0xFF, 128, 128, 128, 128]) + bytes(12)


def test_a_pressed_button_clears_its_bit_and_has_full_pressure() -> None:
    # Cross is bit 6 of the second flag byte and the 7th pressure byte (right, left, up, down, triangle, circle, cross).
    cross = recordings_make.pad_bytes(PadState(buttons=0x0040))
    assert cross[0] == 0xFF and cross[1] == 0xFF & ~0x40
    assert cross[6 + 6] == 255 and sum(cross[6:]) == 255
    # R2 held with LEFT: R2 is bit 1 of byte 1 and the last pressure; LEFT is bit 7 of byte 0 and the second pressure.
    both = recordings_make.pad_bytes(PadState(buttons=0x0002 | 0x8000))
    assert both[0] == 0xFF & ~0x80 and both[1] == 0xFF & ~0x02
    assert both[6 + 1] == 255 and both[6 + 11] == 255 and sum(both[6:]) == 510


def test_start_and_select_have_flags_but_no_pressure() -> None:
    pad = recordings_make.pad_bytes(PadState(buttons=0x0800 | 0x0100))
    assert pad[0] == 0xFF & ~0x08 & ~0x01 and sum(pad[6:]) == 0


def test_the_sticks_keep_the_bytes_the_script_makes() -> None:
    pad = recordings_make.pad_bytes(PadState(sticks=[10, 20, 30, 40]))
    assert pad[2:6] == bytes([10, 20, 30, 40])


def test_a_script_becomes_frames_two_to_an_update() -> None:
    events = parse_input_script("2 tap cross\n3 stick left 0 100\n")
    data = recordings_make.build_recording(events, updates=5, author="me", game="SLUS-21215", from_savestate=True)
    header = parse_p2m2(data)
    assert (header.total_frames, header.from_savestate, header.author) == (10, True, "me")
    assert len(data) == HEADER + 10 * P2M2_FRAME_BYTES
    rest = recordings_make.pad_bytes(PadState())
    assert frame(data, 3) == rest  # update 1
    # The tap holds for the whole of update 2 (frames 4 and 5) and is gone in update 3.
    assert frame(data, 4)[1] == 0xFF & ~0x40 and frame(data, 5)[1] == 0xFF & ~0x40
    assert frame(data, 6)[1] == 0xFF
    # The stick moves in update 3 and stays: the left stick's y byte for 100 % up.
    up = bytes([128, stick_byte(-100)])
    assert frame(data, 6)[4:6] == up and frame(data, 9)[4:6] == up
    assert frame(data, 4, port=1) == rest  # player 2 stays at rest


def test_the_number_of_frames_per_update_can_change() -> None:
    events = parse_input_script("0 press cross\n")
    data = recordings_make.build_recording(events, updates=3, per_update=1, author="", game="", from_savestate=False)
    header = parse_p2m2(data)
    assert header.total_frames == 3 and not header.from_savestate
    with pytest.raises(ValueError, match="frames per update"):
        recordings_make.build_recording(events, updates=3, per_update=0, author="", game="", from_savestate=True)


def test_the_default_length_runs_a_second_past_the_last_line() -> None:
    events = parse_input_script("40 tap cross\n")
    assert recordings_make.default_updates(events) == 40 + 1 + recordings_make.TRAILING_UPDATES
    assert recordings_make.default_updates([]) == recordings_make.TRAILING_UPDATES


def test_make_writes_the_recording_and_its_savestate(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.setattr(pcsx2_copies, "scratch_recordings", lambda: tmp_path / "recordings")
    script = tmp_path / "walk.txt"
    script.write_text("0 stick left 0 60\n30 stick left 0 0\n", encoding="utf-8")
    state = tmp_path / "start.p2s"
    state.write_bytes(b"a made-up state")
    assert recordings_make.run_make("fights/walk", script, state, None, 2, "me", False) == 0
    out = tmp_path / "recordings" / "fights" / "walk.p2m2"
    header = parse_p2m2(out.read_bytes())
    assert header.total_frames == 2 * (30 + 1 + recordings_make.TRAILING_UPDATES) and header.from_savestate
    assert savestate_of(out).read_bytes() == b"a made-up state"
    # A second make refuses to replace it, and --force does.
    with pytest.raises(ConfigError, match="already exists"):
        recordings_make.run_make("fights/walk", script, state, None, 2, "me", False)
    assert recordings_make.run_make("fights/walk", script, state, 10, 2, "me", True) == 0
    assert parse_p2m2(out.read_bytes()).total_frames == 20


def test_a_power_on_recording_has_no_savestate(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.setattr(pcsx2_copies, "scratch_recordings", lambda: tmp_path / "recordings")
    script = tmp_path / "menu.txt"
    script.write_text("5 tap start\n", encoding="utf-8")
    recordings_make.run_make("boot", script, None, 8, 2, "", False)
    out = tmp_path / "recordings" / "boot.p2m2"
    assert not parse_p2m2(out.read_bytes()).from_savestate and not savestate_of(out).exists()


def test_make_refuses_a_missing_script_or_state(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.setattr(pcsx2_copies, "scratch_recordings", lambda: tmp_path / "recordings")
    with pytest.raises(ConfigError, match="input script"):
        recordings_make.run_make("x", tmp_path / "none.txt", None, None, 2, "", False)
    script = tmp_path / "s.txt"
    script.write_text("0 tap cross\n", encoding="utf-8")
    with pytest.raises(ConfigError, match="no such savestate"):
        recordings_make.run_make("x", script, tmp_path / "none.p2s", None, 2, "", False)
