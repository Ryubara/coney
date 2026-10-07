# SPDX-License-Identifier: GPL-3.0-or-later
"""The Xbox texture map (`xbox-textures.bin`): which Xbox texture replaces which PS2 texture.

Matching an Xbox texture to a PS2 one means decoding both (xbox_match), far too slow for loading a level, so it is
done once, at install, and the result kept in this file. A reader looks a PS2 texture up by the dictionary it was
loaded from and its name, and finds where the Xbox texture to use instead lies in the archive volumes. Its layout is
the specification for the engine's port (docs/research/xbox-assets.md#texture-map); all values little-endian:

    header, 56 bytes:
        char magic[4]        "CXTM"
        u32  version         1
        u32  recordCount
        u32  reserved        0
        u8   ps2DirSha1[20]  SHA-1 of the PS2 disc's WARRIORS.DIR it was made from
        u8   xboxIdxSha1[20] SHA-1 of the Xbox disc's XBoxWad.idx it was made from
    recordCount records, 32 bytes each, sorted by (kind, ps2Key, nameHash):
        u32  ps2Key          kind 0: the resource hash of the PS2 texture dictionary's resource;
                             kind 1: the WARRIORS.DIR name hash of the streamed-world file holding the dictionary
        u32  nameHash        CRC-32 of the PS2 texture's name, lower-cased (as the name hash, without a folder)
        u16  kind            0 resource dictionary, 1 streamed-world dictionary
        u16  textureNumber   the texture's position among the Xbox resource image's texture headers
        u16  volume          index into XBoxWad.idx's volume list
        u16  reserved        0
        u64  imageOffset     byte offset of the Xbox resource image in that volume
        u32  imageSize       bytes of the resource image
        u16  width, height   the Xbox texture's size

A key that two PS2 textures share with different Xbox matches (two variants of one resource, or a name used twice
in one dictionary) is left out, so a reader never takes the wrong one; that texture stays the PS2 one.

Research: docs/research/xbox-assets.md#texture-map
"""

from __future__ import annotations

import struct
import zlib
from dataclasses import dataclass

from coney_tools.config import ConfigError

MAGIC = b"CXTM"
VERSION = 1
RESOURCE = 0  # kind: a resource's texture dictionary (chunk 0x2a)
WORLD = 1  # kind: a streamed-world file's dictionary
_HEADER = struct.Struct("<4sIII20s20s")
_RECORD = struct.Struct("<IIHHHHQIHH")
HEADER_SIZE = _HEADER.size  # 56
RECORD_SIZE = _RECORD.size  # 32


def texture_name_hash(name: str) -> int:
    """The map's hash of a PS2 texture name: CRC-32 of the lower-cased name."""
    return zlib.crc32(name.lower().encode("latin-1"))


@dataclass(frozen=True)
class Replacement:
    """One record: the PS2 texture (kind, key, name hash) and the Xbox texture that replaces it."""

    ps2_key: int
    name_hash: int
    kind: int
    texture_number: int
    volume: int
    image_offset: int
    image_size: int
    width: int
    height: int

    @property
    def key(self) -> tuple[int, int, int]:
        """What a reader looks up: (kind, PS2 key, name hash)."""
        return (self.kind, self.ps2_key, self.name_hash)


@dataclass(frozen=True)
class TextureMap:
    """The records of an `xbox-textures.bin` and the SHA-1s of the two archives it was made from."""

    ps2_dir_sha1: bytes
    xbox_index_sha1: bytes
    records: tuple[Replacement, ...]

    def find(self, kind: int, ps2_key: int, name: str) -> Replacement | None:
        """The Xbox texture for a PS2 texture, or None when it stays the PS2 one."""
        wanted = (kind, ps2_key, texture_name_hash(name))
        return next((r for r in self.records if r.key == wanted), None)


def make(ps2_dir_sha1: bytes, xbox_index_sha1: bytes, replacements: list[Replacement]) -> TextureMap:
    """A map from the replacements one extraction made: sorted, duplicates merged, conflicting keys left out."""
    by_key: dict[tuple[int, int, int], Replacement | None] = {}
    for replacement in replacements:
        if replacement.key not in by_key:
            by_key[replacement.key] = replacement
        elif by_key[replacement.key] != replacement:
            by_key[replacement.key] = None  # two answers: neither is safe
    records = tuple(r for _, r in sorted(by_key.items()) if r is not None)
    return TextureMap(ps2_dir_sha1, xbox_index_sha1, records)


def to_bytes(texture_map: TextureMap) -> bytes:
    """The file's bytes (layout in the module's docstring)."""
    head = _HEADER.pack(
        MAGIC, VERSION, len(texture_map.records), 0, texture_map.ps2_dir_sha1, texture_map.xbox_index_sha1
    )
    body = [
        _RECORD.pack(
            r.ps2_key,
            r.name_hash,
            r.kind,
            r.texture_number,
            r.volume,
            0,
            r.image_offset,
            r.image_size,
            r.width,
            r.height,
        )
        for r in texture_map.records
    ]
    return head + b"".join(body)


def from_bytes(data: bytes) -> TextureMap:
    """Parse a file's bytes; raises ConfigError for a wrong magic or version, or a size that does not fit."""
    if len(data) < HEADER_SIZE:
        raise ConfigError(f"xbox texture map: truncated ({len(data)} bytes)")
    magic, version, count, _, ps2_sha1, xbox_sha1 = _HEADER.unpack_from(data, 0)
    if magic != MAGIC or version != VERSION:
        raise ConfigError(f"xbox texture map: not version {VERSION} of {MAGIC.decode()} ({magic!r}, {version})")
    needed = HEADER_SIZE + count * RECORD_SIZE
    if len(data) != needed:
        raise ConfigError(f"xbox texture map: {count} records need {needed} bytes, the file has {len(data)}")
    records = []
    for number in range(count):
        key, name, kind, texture, volume, _, offset, size, width, height = _RECORD.unpack_from(
            data, HEADER_SIZE + number * RECORD_SIZE
        )
        records.append(Replacement(key, name, kind, texture, volume, offset, size, width, height))
    return TextureMap(ps2_sha1, xbox_sha1, tuple(records))
