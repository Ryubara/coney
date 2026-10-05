# SPDX-License-Identifier: GPL-3.0-or-later
"""Coney's input scripts, read the way Coney reads them, so one script drives both Coney and the original.

The format and its rules are Coney's own (`src/core/input_script.cpp`, docs/guides/building.md#input-scripts): one
line per command, `FRAME [p1|p2] ACTION ARGS`, `#` comments, frames that never go down; `press`, `release` and `tap`
take button names, `stick left|right X Y` takes whole percentages from -100 to 100 (y up), `connect` and `disconnect`
take nothing. A frame is one fixed step of 1/30 s, so on the original it is one character update.

ScriptedPad plays a script frame by frame as Coney's ScriptedInput does (taps last one frame) and gives the raw pad
bytes the original's patched pad read takes: active-low buttons and the four stick bytes, made from a percentage
exactly as Coney makes them (`160 + round(0.95 * v)` above 0, `95 - round(0.95 * |v|)` below, 128 at rest), so both
games see the same stick (docs/guides/research-workflow.md#driving-pcsx2).
"""

from __future__ import annotations

from dataclasses import dataclass, field
from pathlib import Path

#: The bit of each button name, as Coney's pad::k* constants (the PS2 pad's own order, L2 in bit 0).
BUTTONS: dict[str, int] = {
    "l2": 0x0001,
    "r2": 0x0002,
    "l1": 0x0004,
    "r1": 0x0008,
    "triangle": 0x0010,
    "circle": 0x0020,
    "cross": 0x0040,
    "square": 0x0080,
    "select": 0x0100,
    "l3": 0x0200,
    "r3": 0x0400,
    "start": 0x0800,
    "up": 0x1000,
    "right": 0x2000,
    "down": 0x4000,
    "left": 0x8000,
}

STICK_CENTRE = 0x80
STICK_DEAD_LOW = 95
STICK_DEAD_HIGH = 160
STICK_SPAN = 95

ACTIONS = ("press", "release", "tap", "stick", "connect", "disconnect")


class InputScriptError(Exception):
    """A script that cannot be read or has a bad line; the message names the line, as Coney's does."""


@dataclass(frozen=True)
class InputEvent:
    """One line: from frame `frame` on, do `action` on `port` (0 or 1)."""

    frame: int
    port: int
    action: str
    buttons: int = 0
    stick: int = 0  # 0 the left stick, 1 the right
    x: int = 0
    y: int = 0


def stick_byte(value: int) -> int:
    """The raw stick byte for a deflection of `value` percent (-100 to 100), as Coney's stickByteFromPercent()."""
    if not -100 <= value <= 100:
        raise ValueError(f"a stick value must be from -100 to 100, not {value}")
    if value > 0:
        return STICK_DEAD_HIGH + (value * STICK_SPAN + 50) // 100
    if value < 0:
        return STICK_DEAD_LOW - (-value * STICK_SPAN + 50) // 100
    return STICK_CENTRE


def _parse_int(word: str) -> int | None:
    """A whole decimal number, optionally signed, or None (Coney's from_chars takes nothing else)."""
    digits = word[1:] if word[:1] == "-" else word
    return int(word) if digits.isascii() and digits.isdigit() else None


def _parse_arguments(action: str, args: list[str]) -> tuple[dict[str, int], str]:
    """The fields of an event from its arguments, and what is wrong ("" when nothing is)."""
    if action in ("press", "release", "tap"):
        if not args:
            return {}, "needs at least one button"
        buttons = 0
        for name in args:
            if name not in BUTTONS:
                return {}, f'unknown button "{name}"'
            buttons |= BUTTONS[name]
        return {"buttons": buttons}, ""
    if action == "stick":
        if len(args) != 3 or args[0] not in ("left", "right"):
            return {}, "stick needs left or right, then X and Y"
        x, y = _parse_int(args[1]), _parse_int(args[2])
        if x is None or y is None or not -100 <= x <= 100 or not -100 <= y <= 100:
            return {}, "stick X and Y must be whole numbers from -100 to 100"
        return {"stick": 0 if args[0] == "left" else 1, "x": x, "y": y}, ""
    return {}, "" if not args else "takes no arguments"


def _parse_line(words: list[str]) -> tuple[InputEvent | None, str]:
    """One non-empty line's event, or what is wrong with it."""
    if not (words[0].isascii() and words[0].isdigit()):
        return None, f'"{words[0]}" is not a frame number'
    frame, at, port = int(words[0]), 1, 0
    if at < len(words) and words[at] in ("p1", "p2"):
        port = 0 if words[at] == "p1" else 1
        at += 1
    if at == len(words):
        return None, "needs an action after the frame"
    action = words[at]
    if action not in ACTIONS:
        return None, f'unknown action "{action}"'
    fields, problem = _parse_arguments(action, words[at + 1 :])
    if problem:
        return None, problem
    return InputEvent(frame=frame, port=port, action=action, **fields), ""


def parse_input_script(text: str) -> list[InputEvent]:
    """Every event of a script, in order. Raises InputScriptError naming the first bad line."""
    events: list[InputEvent] = []
    for number, line in enumerate(text.split("\n"), start=1):
        words = line.split("#", 1)[0].replace("\r", " ").split()
        if not words:
            continue
        event, problem = _parse_line(words)
        if event is None:
            raise InputScriptError(f"input script line {number}: {problem}")
        # Frames in order, as Coney requires: the script reads as a timeline and plays in one pass.
        if events and event.frame < events[-1].frame:
            raise InputScriptError(
                f"input script line {number}: frame {event.frame} comes after frame {events[-1].frame}"
            )
        events.append(event)
    return events


def load_input_script(path: Path) -> list[InputEvent]:
    """Read and parse the script at `path`. Raises InputScriptError when it cannot be read or has a bad line."""
    try:
        text = path.read_text(encoding="utf-8")
    except (OSError, UnicodeDecodeError) as error:
        raise InputScriptError(f"cannot read the input script {path}: {error}") from error
    try:
        return parse_input_script(text)
    except InputScriptError as error:
        raise InputScriptError(f"{path}: {error}") from error


@dataclass
class PadState:
    """What one port holds: the buttons (Coney's bits, 1 held) and the raw sticks (right x, right y, left x, left y)."""

    buttons: int = 0
    sticks: list[int] = field(default_factory=lambda: [STICK_CENTRE] * 4)

    def raw(self) -> bytes:
        """The six bytes the patched pad read takes from the spare buffer: the buttons active low (SELECT ... LEFT,
        then L2 ... SQUARE), then the sticks."""
        low = ~self.buttons & 0xFFFF
        return bytes([low >> 8, low & 0xFF, *self.sticks])


class ScriptedPad:
    """Plays a script frame by frame, as Coney's ScriptedInput does; only port 1 reaches the original."""

    def __init__(self, events: list[InputEvent]) -> None:
        """Play `events` (in frame order, as parse_input_script() gives them)."""
        self._events = events
        self._next = 0
        self._tapped = [0, 0]
        self.ports = [PadState(), PadState()]
        self._next_frame = 0

    def advance(self, frame: int) -> PadState:
        """Apply every line up to and including `frame` (frames asked for in increasing order) and return port 1.

        A tap from the frame before ends first, so a tap on two frames in a row stays held; a frame skipped still has
        its lines applied, in order.
        """
        if frame < self._next_frame:
            raise ValueError(f"frame {frame} asked for after frame {self._next_frame - 1}")
        self._next_frame = frame + 1
        for port, tapped in enumerate(self._tapped):
            self.ports[port].buttons &= ~tapped
            self._tapped[port] = 0
        while self._next < len(self._events) and self._events[self._next].frame <= frame:
            self._apply(self._events[self._next])
            self._next += 1
        return self.ports[0]

    def _apply(self, event: InputEvent) -> None:
        """Apply one line to its port."""
        port = self.ports[event.port]
        if event.action in ("press", "tap"):
            port.buttons |= event.buttons
            if event.action == "tap":
                self._tapped[event.port] |= event.buttons
        elif event.action == "release":
            port.buttons &= ~event.buttons
        elif event.action == "stick":
            # The raw order is right x, right y, left x, left y; a script's y is up, the byte's is down.
            first = 2 if event.stick == 0 else 0
            port.sticks[first] = stick_byte(event.x)
            port.sticks[first + 1] = stick_byte(-event.y)
        # connect and disconnect have no meaning for the original's port 1, which is always plugged in.

    def last_frame(self) -> int:
        """The frame of the script's last line (-1 for an empty script)."""
        return self._events[-1].frame if self._events else -1

    def first_frame(self) -> int | None:
        """The frame of the script's first line, or None for an empty script."""
        return self._events[0].frame if self._events else None
