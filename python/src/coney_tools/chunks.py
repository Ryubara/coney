# SPDX-License-Identifier: GPL-3.0-or-later
"""The chunk container shared by the PS2 and Xbox archives: packs of resources, resources of chunks.

Every level starts with a 16-byte header of four little-endian words:

* a **pack**: `{u32 resourceCount, u32 size, u32 0, u32 0xDE686795}`, where `size` is the entry's size less 16;
* a **resource**: `{u32 chunkCount, u32 dataSize, u32 0, u32 resourceHash}`, `dataSize` being the sum of its chunks'
  sizes (their headers not counted);
* a **chunk**: `{u32 type, u32 size, u32 0, u32 hash}`, then `size` bytes of data, a multiple of 16.

There is no padding between them, so a pack or resource entry parses to exactly its size; anything else is not a
container.

Research: docs/research/formats/wad-contents.md (Chunk container), docs/research/xbox-assets.md (Formats)
"""

from __future__ import annotations

import struct
from dataclasses import dataclass

#: A pack's fourth header word, `crc32("package")`.
PACK_MARKER = 0xDE686795
HEADER = 16


@dataclass(frozen=True)
class Chunk:
    """One chunk: its type, where its data starts in the entry, its data size and its hash."""

    type: int
    offset: int  # of the data (after the header), from the start of the entry
    size: int
    hash: int


@dataclass(frozen=True)
class Resource:
    """One resource: the hash of its name, where its header is in the entry, and its chunks in order."""

    hash: int
    offset: int
    chunks: tuple[Chunk, ...]

    @property
    def types(self) -> frozenset[int]:
        """The set of chunk types the resource holds."""
        return frozenset(chunk.type for chunk in self.chunks)


@dataclass(frozen=True)
class Container:
    """A parsed pack or standalone resource entry."""

    is_pack: bool
    resources: tuple[Resource, ...]


def _parse_resource(data: bytes | memoryview, pos: int) -> tuple[Resource, int] | None:
    """Parse the resource whose header is at `pos`; return it and the offset after it, or None if it does not fit."""
    start = pos
    if pos + HEADER > len(data):
        return None
    count, data_size, zero, resource_hash = struct.unpack_from("<IIII", data, pos)
    if zero != 0 or count == 0:
        return None
    chunks = []
    total = 0
    pos += HEADER
    for _ in range(count):
        if pos + HEADER > len(data):
            return None
        chunk_type, size, chunk_zero, chunk_hash = struct.unpack_from("<IIII", data, pos)
        if chunk_zero != 0 or size % 16 or pos + HEADER + size > len(data):
            return None
        chunks.append(Chunk(chunk_type, pos + HEADER, size, chunk_hash))
        total += size
        pos += HEADER + size
    if total != data_size:
        return None
    return Resource(resource_hash, start, tuple(chunks)), pos


def parse_container(data: bytes | memoryview) -> Container | None:
    """Parse a whole archive entry as a pack or a standalone resource; None when it is neither, exactly."""
    if len(data) < HEADER:
        return None
    count, size, zero, marker = struct.unpack_from("<IIII", data, 0)
    if marker == PACK_MARKER and zero == 0:
        if size != len(data) - HEADER:
            return None
        resources = []
        pos = HEADER
        for _ in range(count):
            parsed = _parse_resource(data, pos)
            if parsed is None:
                return None
            resources.append(parsed[0])
            pos = parsed[1]
        return Container(True, tuple(resources)) if pos == len(data) else None
    parsed = _parse_resource(data, 0)
    if parsed is None or parsed[1] != len(data):
        return None
    return Container(False, (parsed[0],))


def looks_like_container(head: bytes, size: int) -> bool:
    """A cheap test on an entry's first 16 bytes: could it be a pack or a resource of `size` bytes?

    Lets a scan skip reading entries that cannot be containers; `parse_container` decides.
    """
    if len(head) < HEADER:
        return False
    count, second, zero, marker = struct.unpack_from("<IIII", head, 0)
    if zero != 0 or count == 0:
        return False
    if marker == PACK_MARKER:
        return second == size - HEADER
    return second + HEADER * (count + 1) == size
