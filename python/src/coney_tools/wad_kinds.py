# SPDX-License-Identifier: GPL-3.0-or-later
"""Tell what a WARRIORS.WAD entry is from its bytes alone: the structural tests of the WAD survey.

Every entry falls into exactly one kind; each test must account for the whole entry (a container parses to its
size, a RenderWare stream walks to its end, a manifest's count fits its size), so no entry is guessed from a few
bytes. The kinds and their counts on the NTSC-U disc are on docs/research/formats/wad-contents.md#kinds-of-entry.

Research: docs/research/formats/wad-contents.md, docs/research/world.md (the streamed world's files),
docs/research/formats/audio.md#banks (sound banks)
"""

from __future__ import annotations

import itertools
import struct
from dataclasses import dataclass

from coney_tools import lua4, rw
from coney_tools.chunks import PACK_MARKER, Container, parse_container

#: Kinds, in the order the survey lists them.
PACK = "pack"
RESOURCE = "resource"
SCENE = "scene"
SECTOR_PARTS = "sector-parts"
WORLD_STREAM = "world-stream"
MANIFEST = "world-manifest"
LUA = "lua"
OBJECT_LIST = "object-list"
BANK_SAMPLES = "bank-samples"
BANK_INDEX = "bank-index"
LEGACY_RW = "legacy-rw"
METRICS = "font-metrics"
ICON = "icon"
BITMAP = "bitmap"
UNKNOWN = "unknown"

KINDS = (
    PACK,
    RESOURCE,
    SCENE,
    SECTOR_PARTS,
    WORLD_STREAM,
    MANIFEST,
    LUA,
    OBJECT_LIST,
    BANK_SAMPLES,
    BANK_INDEX,
    LEGACY_RW,
    METRICS,
    ICON,
    BITMAP,
    UNKNOWN,
)

#: The memory card icon's first word (docs/research/formats/wad-contents.md#kinds-of-entry).
ICON_MAGIC = b"\x00\x00\x01\x00"


@dataclass(frozen=True)
class Classified:
    """An entry's kind, and its parsed container when it is a pack or a resource."""

    kind: str
    container: Container | None = None


def _is_legacy_rw(data: bytes) -> bool:
    """A file that starts with a texture dictionary stamped by the older library (3.6.0.3), at offset 0 or after a
    `u32` count: the five leftovers the game never names."""
    for start in (0, 4):
        if len(data) >= start + rw.HEADER:
            kind, _, stamp = struct.unpack_from("<III", data, start)
            if kind == rw.TEX_DICTIONARY and stamp == rw.STAMPS[1]:
                return True
    return False


def _is_sector_parts(data: bytes) -> bool:
    """`{1, 0, 0, hash}`, a texture dictionary, `u32 n`, then `n` times `{u32 sector, atomic}` to the end."""
    if len(data) < 16 + rw.HEADER or struct.unpack_from("<III", data, 0) != (1, 0, 0):
        return False
    try:
        dictionary = rw.section_at(data, 16)
        if dictionary.type != rw.TEX_DICTIONARY:
            return False
        at = dictionary.end
        (count,) = struct.unpack_from("<I", data, at)
        at += 4
        for _ in range(count):
            at += 4
            atomic = rw.section_at(data, at)
            if atomic.type != rw.ATOMIC:
                return False
            at = atomic.end
    except (rw.RwError, struct.error):
        return False
    return not any(data[at:])


def _is_world_stream(data: bytes) -> bool:
    """`u32 n`, a texture dictionary, then a world section."""
    if len(data) < 4 + 2 * rw.HEADER:
        return False
    try:
        dictionary = rw.section_at(data, 4)
        if dictionary.type != rw.TEX_DICTIONARY:
            return False
        world = rw.section_at(data, dictionary.end)
    except rw.RwError:
        return False
    return world.type == rw.WORLD and not any(data[world.end :])


def _is_manifest(data: bytes) -> bool:
    """`{worldSize, worldHeap, n}` then `n` pairs, padded with zeros only."""
    if len(data) < 12:
        return False
    world, heap, count = struct.unpack_from("<III", data, 0)
    end = 12 + 8 * count
    return 0 < count < 1000 and world > 0 and heap >= world // 2 and end <= len(data) and not any(data[end:])


def is_bank_index(data: bytes) -> bool:
    """A `.msd`: `{hash, offset}` pairs from offset 0, rising and 16-aligned, ended by a zero pair, then zeros."""
    if len(data) < 16 or len(data) % 8 or len(data) > 1 << 16:
        return False
    pairs = []
    for at in range(0, len(data), 8):
        key, offset = struct.unpack_from("<II", data, at)
        if key == 0 and offset == 0:
            break
        pairs.append((key, offset))
    else:
        return False
    if not pairs or pairs[0][1] != 0:
        return False
    rising = all(a[1] < b[1] for a, b in itertools.pairwise(pairs))
    return rising and all(o % 16 == 0 for _, o in pairs) and not any(data[len(pairs) * 8 :])


def _is_object_list(data: bytes) -> bool:
    """Text: a count line, then object lines (printable ASCII with line breaks, zero padding at the end)."""
    text = data.rstrip(b"\0")
    if not text or not text[:1].isdigit():
        return False
    return all(32 <= b < 127 or b in (9, 10, 13) for b in text)


def classify(data: bytes) -> Classified:
    """The kind of one entry, from its bytes. A bank's samples are headerless ADPCM and come out UNKNOWN here: the
    entry just before a bank index is its samples (`mark_bank_samples`)."""
    if len(data) >= 16:
        container = parse_container(data)
        if container is not None:
            return Classified(PACK if container.is_pack else RESOURCE, container)
    if data.startswith(lua4.HEADER[:5]):
        return Classified(LUA)
    if len(data) >= 8 and struct.unpack_from("<I", data, 0)[0] == len(data):
        return Classified(SCENE)
    if _is_legacy_rw(data):
        return Classified(LEGACY_RW)
    if _is_sector_parts(data):
        return Classified(SECTOR_PARTS)
    if _is_world_stream(data):
        return Classified(WORLD_STREAM)
    if data.startswith(b"METRICS1"):
        return Classified(METRICS)
    if data.startswith(b"BM") and len(data) >= 6 and struct.unpack_from("<I", data, 2)[0] == len(data):
        return Classified(BITMAP)
    if data.startswith(ICON_MAGIC):
        return Classified(ICON)
    if is_bank_index(data):
        return Classified(BANK_INDEX)
    if _is_object_list(data):
        return Classified(OBJECT_LIST)
    if _is_manifest(data):
        return Classified(MANIFEST)
    return Classified(UNKNOWN)


def is_pack_header(head: bytes) -> bool:
    """Whether 16 bytes are a pack's header."""
    return len(head) >= 16 and struct.unpack_from("<I", head, 12)[0] == PACK_MARKER


def mark_bank_samples(kinds: list[str]) -> list[str]:
    """Turn each UNKNOWN entry that sits just before a bank index into BANK_SAMPLES (the WAD's name order puts
    `<bank>.msb` before `<bank>.msd`)."""
    marked = list(kinds)
    for index in range(1, len(marked)):
        if marked[index] == BANK_INDEX and marked[index - 1] == UNKNOWN:
            marked[index - 1] = BANK_SAMPLES
    return marked
