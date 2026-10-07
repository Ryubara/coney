# SPDX-License-Identifier: GPL-3.0-or-later
"""The Xbox resource index (`xbox-resources.bin`): where every resource's chunks lie in the archive volumes.

Finding a resource on the Xbox disc by its hash means parsing every pack and standalone resource of the archive
(about 1 GB). This file keeps the result, so the pass happens once, at install. Its layout is the specification for
the engine's port (docs/research/xbox-assets.md#resource-index); all values little-endian:

    header, 32 bytes:
        char magic[4]        "CXRI"
        u32  version         1
        u32  recordCount
        u8   indexSha1[20]   SHA-1 of the XBoxWad.idx it was built from (a different disc means a stale index)
    recordCount records, 24 bytes each, sorted by (resourceHash, chunkType), chunks of one key in resource order:
        u32  resourceHash    the chunk container's resource hash (the same on both discs)
        u16  chunkType       0x2a texture, 0x47 model, ...
        u16  volume          index into XBoxWad.idx's volume list
        u64  offset          byte offset of the chunk's data (after its 16-byte header) in that volume
        u32  size            bytes of chunk data
        u32  entry           the XBoxWad.idx entry that holds it

Only the first instance of each resource hash is kept: packs repeat shared resources.

Research: docs/research/xbox-assets.md#resource-index
"""

from __future__ import annotations

import hashlib
import struct
from dataclasses import dataclass
from pathlib import Path

from coney_tools import xbox
from coney_tools.config import ConfigError
from coney_tools.xdvdfs import XboxDisc

MAGIC = b"CXRI"
VERSION = 1
_HEADER = struct.Struct("<4sII20s")
HEADER_SIZE = _HEADER.size  # 32
_RECORD = struct.Struct("<IHHQII")
RECORD_SIZE = _RECORD.size  # 24


@dataclass(frozen=True)
class ChunkLocation:
    """One chunk of an indexed resource: its key, where its data is, and the archive entry holding it."""

    resource_hash: int
    chunk_type: int
    volume: int
    offset: int  # bytes into the volume
    size: int
    entry: int


@dataclass(frozen=True)
class ResourceIndex:
    """The records of an `xbox-resources.bin`, and the SHA-1 of the `XBoxWad.idx` they were built from."""

    index_sha1: bytes
    records: tuple[ChunkLocation, ...]

    def chunks(self, resource_hash: int, chunk_type: int) -> list[ChunkLocation]:
        """The chunks of one type of the resource with this hash, in resource order (empty when none)."""
        return self._table().get((resource_hash, chunk_type), [])

    def _table(self) -> dict[tuple[int, int], list[ChunkLocation]]:
        """The records keyed by (resource hash, chunk type), built on first use."""
        table: dict[tuple[int, int], list[ChunkLocation]] | None = self.__dict__.get("_by_key")
        if table is None:
            table = {}
            for record in self.records:
                table.setdefault((record.resource_hash, record.chunk_type), []).append(record)
            object.__setattr__(self, "_by_key", table)  # a cache on a frozen dataclass
        return table


def index_digest(disc: XboxDisc) -> bytes:
    """SHA-1 of the disc's `XBoxWad.idx`, which ties a resource index to the archive it describes."""
    with disc.open(xbox.INDEX_FILE) as handle:
        return hashlib.sha1(handle.read()).digest()


def build(disc: XboxDisc, archive: xbox.XboxIndex, survey: xbox.Survey) -> ResourceIndex:
    """The resource index of a surveyed archive: every chunk of the first instance of each resource hash."""
    seen: set[int] = set()
    records = []
    for entry_number, _, resource in survey.resources:
        if resource.hash in seen:
            continue
        seen.add(resource.hash)
        entry = archive.entries[entry_number]
        for chunk in resource.chunks:
            where = entry.offset + chunk.offset
            records.append(ChunkLocation(resource.hash, chunk.type, entry.volume, where, chunk.size, entry.index))
    # A stable sort keeps a key's chunks in resource order.
    records.sort(key=lambda r: (r.resource_hash, r.chunk_type))
    return ResourceIndex(index_digest(disc), tuple(records))


def to_bytes(index: ResourceIndex) -> bytes:
    """The file's bytes (layout in the module's docstring)."""
    parts = [_HEADER.pack(MAGIC, VERSION, len(index.records), index.index_sha1)]
    parts += [_RECORD.pack(r.resource_hash, r.chunk_type, r.volume, r.offset, r.size, r.entry) for r in index.records]
    return b"".join(parts)


def from_bytes(data: bytes) -> ResourceIndex:
    """Parse a file's bytes; raises ConfigError for a wrong magic or version, or a size that does not fit."""
    if len(data) < _HEADER.size:
        raise ConfigError(f"xbox resource index: truncated ({len(data)} bytes)")
    magic, version, count, digest = _HEADER.unpack_from(data, 0)
    if magic != MAGIC or version != VERSION:
        raise ConfigError(f"xbox resource index: not version {VERSION} of {MAGIC.decode()} ({magic!r}, {version})")
    needed = _HEADER.size + count * _RECORD.size
    if len(data) != needed:
        raise ConfigError(f"xbox resource index: {count} records need {needed} bytes, the file has {len(data)}")
    records = tuple(ChunkLocation(*_RECORD.unpack_from(data, _HEADER.size + n * _RECORD.size)) for n in range(count))
    return ResourceIndex(digest, records)


def load(path: Path, disc: XboxDisc) -> ResourceIndex | None:
    """The index at `path` when it exists, parses and was built from this disc's archive; None otherwise."""
    if not path.is_file():
        return None
    try:
        index = from_bytes(path.read_bytes())
    except ConfigError:
        return None
    return index if index.index_sha1 == index_digest(disc) else None
