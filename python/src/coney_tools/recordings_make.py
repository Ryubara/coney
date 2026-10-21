# SPDX-License-Identifier: GPL-3.0-or-later
"""`coney-tools pcsx2 recordings make`: write a Coney input script as a PCSX2 input recording (`.p2m2`).

A recording is PCSX2's own record of every frame's pad, so the original replays it exactly, frame by frame, the same on
every run; that is the way to drive the original for runtime evidence (docs/guides/research-workflow.md, "Input
recordings"). Making one by hand in PCSX2 needs a person at the pad, and an input script already says what Coney's pad
does, so the same script can drive both games from one file.

The file layout is PCSX2's (`pcsx2/Recording/InputRecordingFile.h`, version 1): a header of the version byte, the
emulator version (50 bytes), the author (255) and the game (255) as NUL-padded text, the total frame count and the undo
count as `u32`, and one byte that says whether the recording starts from a savestate; then 36 bytes a frame, two ports
of 18: two active-low flag bytes (SELECT, L3, R3, START, UP, RIGHT, DOWN, LEFT, then L2, R2, L1, R1, TRIANGLE, CIRCLE,
CROSS, SQUARE, lowest bit first), the right and left sticks' x and y, and twelve pressure bytes (right, left, up, down,
triangle, circle, cross, square, L1, R1, L2, R2). A recording that starts from a savestate keeps it beside it as
`<recording>.p2m2_SaveState.p2s`.

A script's frame is one character update of 1/30 s, and PCSX2 takes the pad every 1/60 s, so an update is two frames
(`--frames-per-update`).
"""

from __future__ import annotations

import struct
from pathlib import Path

from coney_tools import pcsx2_copies
from coney_tools.config import ConfigError
from coney_tools.debug_server import P2M2_FRAME_BYTES, savestate_of
from coney_tools.input_script import InputEvent, InputScriptError, PadState, ScriptedPad, load_input_script

#: The header's layout, as debug_server's reader has it.
_HEADER = struct.Struct("<B50s255s255sIIB")

#: Updates a recording runs past its script's last line when the length is not given (one second).
TRAILING_UPDATES = 30

#: PCSX2 frames in one character update: the pad is taken at 60 Hz and the game updates its characters at 30 Hz.
FRAMES_PER_UPDATE = 2

#: The button bit (Coney's, the pad word's) of each pressure byte, in the file's order.
_PRESSURE_ORDER = (0x2000, 0x8000, 0x1000, 0x4000, 0x0010, 0x0020, 0x0040, 0x0080, 0x0004, 0x0008, 0x0001, 0x0002)

#: The pressure PCSX2 reads for a held digital button.
FULL_PRESSURE = 255

#: The text written into the header's emulator field.
EMULATOR_NAME = "coney-tools"


def pad_bytes(state: PadState) -> bytes:
    """The 18 bytes of one port in one frame for `state`: the active-low flag bytes, the four stick bytes and the
    twelve pressure bytes (255 for a held button with a pressure, 0 otherwise)."""
    low = ~state.buttons & 0xFFFF
    flags = bytes([low >> 8, low & 0xFF])
    pressures = bytes(FULL_PRESSURE if state.buttons & bit else 0 for bit in _PRESSURE_ORDER)
    return flags + bytes(state.sticks) + pressures


def default_updates(events: list[InputEvent]) -> int:
    """How many updates a recording of `events` runs when the length is not given: through the last line's update and
    TRAILING_UPDATES more, so the last input has played out."""
    return (events[-1].frame + 1 if events else 0) + TRAILING_UPDATES


def build_recording(
    events: list[InputEvent],
    *,
    updates: int,
    per_update: int = FRAMES_PER_UPDATE,
    author: str,
    game: str,
    from_savestate: bool,
) -> bytes:
    """The bytes of a recording that plays `events` for `updates` updates of `per_update` frames each: port 1 follows
    the script, port 2 stays at rest. Raises ValueError for a count below 1."""
    if per_update < 1:
        raise ValueError("frames per update must be at least 1")
    if updates < 1:
        raise ValueError("a recording needs at least 1 update")
    pad = ScriptedPad(events)
    frames = bytearray()
    # Port 2 of a script is not played in the original (its patched pad read takes port 1 only), so it stays at rest.
    rest = pad_bytes(PadState())
    for update in range(updates):
        pad.advance(update)
        frames += (pad_bytes(pad.ports[0]) + rest) * per_update
    header = _HEADER.pack(
        1,
        EMULATOR_NAME.encode("latin-1")[:49],
        author.encode("latin-1", "replace")[:254],
        game.encode("latin-1", "replace")[:254],
        updates * per_update,
        0,
        1 if from_savestate else 0,
    )
    return header + bytes(frames)


def run_make(
    name: str,
    script: Path,
    state: Path | None,
    updates: int | None,
    per_update: int,
    author: str,
    force: bool,
) -> int:
    """`pcsx2 recordings make`: write the recording `name` (below the scratch folder's `recordings/`) from the input
    script `script`, starting from the savestate `state` (copied beside it; none: from power-on). Raises ConfigError
    for a script or state that cannot be read, a name PCSX2 would refuse, or a recording already there without
    `force`."""
    relative = pcsx2_copies.recording_path(name)
    try:
        events = load_input_script(script)
    except InputScriptError as error:
        raise ConfigError(str(error)) from error
    if state is not None and not state.is_file():
        raise ConfigError(f"{state}: no such savestate")
    try:
        data = build_recording(
            events,
            updates=updates if updates is not None else default_updates(events),
            per_update=per_update,
            author=author,
            game="",
            from_savestate=state is not None,
        )
    except ValueError as error:
        raise ConfigError(str(error)) from error
    out = pcsx2_copies.scratch_recordings() / relative
    if out.exists() and not force:
        raise ConfigError(f"{out} already exists; pass --force to replace it")
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_bytes(data)
    print(f"{out}: {(len(data) - _HEADER.size) // P2M2_FRAME_BYTES} frames from {script.name}")
    if state is not None:
        digest = pcsx2_copies.copy_checked(state, savestate_of(out), force=True)
        print(f"{savestate_of(out)}: sha256 {digest}")
    return 0
