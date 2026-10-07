# SPDX-License-Identifier: GPL-3.0-or-later
"""RenderWare binary streams: section headers and the walk over a section's children.

A RenderWare stream is a tree of sections, each `{u32 type, u32 size, u32 libraryStamp}` then `size` bytes. A
container section's data is its children back to back; a `0x01` struct section holds plain fields. The game's
streams are stamped `0x1C02000A` (RenderWare 3.7.0.2), a few older ones `0x1803FFFF` (3.6.0.3).

Research: docs/research/formats/renderware.md
"""

from __future__ import annotations

import struct
from collections.abc import Iterator
from dataclasses import dataclass

HEADER = 12

STRUCT = 0x01
STRING = 0x02
EXTENSION = 0x03
TEXTURE = 0x06
MATERIAL = 0x07
MATERIAL_LIST = 0x08
WORLD = 0x0B
FRAME_LIST = 0x0E
GEOMETRY = 0x0F
CLUMP = 0x10
ATOMIC = 0x14
TEXTURE_NATIVE = 0x15
TEX_DICTIONARY = 0x16
GEOMETRY_LIST = 0x1A
HANIM = 0x11E
SKIN = 0x116
NATIVE_DATA = 0x510
FRAME_NAME = 0x253F2FE

#: The library stamps seen on the disc: 3.7.0.2 build 0x000a, and 3.6.0.3 build 0xffff.
STAMPS = (0x1C02000A, 0x1803FFFF)


class RwError(ValueError):
    """A RenderWare stream that does not hold together."""


@dataclass(frozen=True)
class Section:
    """One section: its type, the library stamp, and where its data lies in the buffer."""

    type: int
    stamp: int
    start: int  # of the data, after the 12-byte header
    size: int

    @property
    def end(self) -> int:
        """The offset just past the section's data."""
        return self.start + self.size


def section_at(data: bytes | memoryview, offset: int, limit: int | None = None) -> Section:
    """The section whose header is at `offset`; raises RwError when it runs past `limit` (default: the buffer)."""
    end = len(data) if limit is None else limit
    if offset + HEADER > end:
        raise RwError(f"section header at {offset} runs past {end}")
    kind, size, stamp = struct.unpack_from("<III", data, offset)
    if offset + HEADER + size > end:
        raise RwError(f"section 0x{kind:x} at {offset} ({size} bytes) runs past {end}")
    return Section(kind, stamp, offset + HEADER, size)


def children(data: bytes | memoryview, parent: Section) -> Iterator[Section]:
    """The sections inside a container section, in order."""
    at = parent.start
    while at + HEADER <= parent.end:
        child = section_at(data, at, parent.end)
        yield child
        at = child.end


def child(data: bytes | memoryview, parent: Section, kind: int, nth: int = 0) -> Section | None:
    """The `nth` child of type `kind`, or None."""
    for section in children(data, parent):
        if section.type == kind:
            if nth == 0:
                return section
            nth -= 1
    return None


def need(section: Section | None, what: str) -> Section:
    """`section`, or a RwError naming what is missing."""
    if section is None:
        raise RwError(f"no {what} section")
    return section


def string(data: bytes | memoryview, section: Section) -> str:
    """A string section's text: up to the first NUL, as Latin-1."""
    raw = bytes(data[section.start : section.end])
    return raw.split(b"\0", 1)[0].decode("latin-1")


def find(data: bytes | memoryview, kind: int, start: int = 0) -> Section | None:
    """The first top-level section of type `kind` in a run of sections from `start`, skipping others; None when the
    run ends or stops holding together first."""
    at = start
    while at + HEADER <= len(data):
        try:
            section = section_at(data, at)
        except RwError:
            return None
        if section.stamp not in STAMPS:
            return None
        if section.type == kind:
            return section
        at = section.end
    return None


def version_of(stamp: int) -> int:
    """The library version a stamp encodes, as RenderWare's `0x3vvvv` number (`0x37002` for 3.7.0.2)."""
    if stamp & 0xFFFF0000 == 0:
        return stamp << 8
    return ((stamp >> 14 & 0x3FF00) + 0x30000) | (stamp >> 16 & 0x3F)
