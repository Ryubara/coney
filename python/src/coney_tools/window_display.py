# SPDX-License-Identifier: GPL-3.0-or-later
"""Which display a window the tools open goes on: `CONEY_WINDOW_DISPLAY`, read the same way as Coney reads it
(`src/core/window_display.h`), so one setting keeps agents' Coney and PCSX2 windows off the display a person works
on (docs/guides/running.md#window-display).

Pure logic only; `pcsx2_proc` lists the displays and moves the windows.
"""

from __future__ import annotations

import os
from collections.abc import Sequence
from dataclasses import dataclass
from typing import Literal

#: The environment variable that names the display.
ENV = "CONEY_WINDOW_DISPLAY"


@dataclass(frozen=True)
class Rect:
    """A rectangle on the desktop, in the window system's coordinates (negative left of or above the primary)."""

    x: int
    y: int
    width: int
    height: int


@dataclass(frozen=True)
class Display:
    """One display: its name (what a name value is matched against), the whole display and the part windows may use
    (less the taskbar)."""

    name: str
    bounds: Rect
    usable: Rect


@dataclass(frozen=True)
class Choice:
    """A display asked for: `index` (1-based; 0 matches nothing), `name` (part of a display's name, case ignored) or
    `portrait` (the first display taller than wide)."""

    kind: Literal["index", "name", "portrait"]
    index: int = 0
    name: str = ""


def parse_choice(text: str | None) -> Choice | None:
    """Read a value of `CONEY_WINDOW_DISPLAY`, spaces round it ignored: digits are an index, `portrait` (any case) the
    first portrait display, anything else part of a name. None for no value or an empty one (no placement)."""
    text = (text or "").strip(" \t")
    if not text:
        return None
    if text.isascii() and text.isdigit():
        index = int(text)
        return Choice("index", index=index if index <= 0xFFFF else 0)
    if text.lower() == "portrait":
        return Choice("portrait")
    return Choice("name", name=text)


def choice_from_environment() -> Choice | None:
    """The choice `CONEY_WINDOW_DISPLAY` holds, or None when it is unset or empty."""
    return parse_choice(os.environ.get(ENV))


def pick_display(choice: Choice, displays: Sequence[Display]) -> int | None:
    """The position in `displays` of the display `choice` names, or None when none matches."""
    if choice.kind == "index":
        return choice.index - 1 if 1 <= choice.index <= len(displays) else None
    for i, display in enumerate(displays):
        if choice.kind == "portrait" and display.bounds.height > display.bounds.width:
            return i
        if choice.kind == "name" and choice.name.lower() in display.name.lower():
            return i
    return None


def place_window(area: Rect, width: int, height: int) -> Rect:
    """Where a `width` x `height` window goes in `area`: centred, at its own size when it fits, else scaled down to
    the largest size of the same shape that does (each side rounded down, never under 1). An empty area keeps the
    size, at the area's corner. Raises ValueError for a size that is not positive."""
    if width <= 0 or height <= 0:
        raise ValueError(f"a window of {width} x {height} has no size")
    if area.width <= 0 or area.height <= 0:
        return Rect(area.x, area.y, width, height)
    fit_w, fit_h = width, height
    if width > area.width or height > area.height:
        if width * area.height <= area.width * height:  # the height runs out first
            fit_h, fit_w = area.height, max(1, width * area.height // height)
        else:
            fit_w, fit_h = area.width, max(1, height * area.width // width)
    return Rect(area.x + (area.width - fit_w) // 2, area.y + (area.height - fit_h) // 2, fit_w, fit_h)


def describe(displays: Sequence[Display]) -> str:
    """The displays as a warning lists them: `1 "name" 2560x1440, 2 ...`."""
    return ", ".join(f'{i} "{d.name}" {d.bounds.width}x{d.bounds.height}' for i, d in enumerate(displays, start=1))
