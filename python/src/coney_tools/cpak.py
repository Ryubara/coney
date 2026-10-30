# SPDX-License-Identifier: GPL-3.0-or-later
"""The Coney Pack (.cpak): read its index, verify its entries and write one.

A second implementation of docs/guides/cpak-format.md beside the engine's (src/core/cpak_index.h, cpak_writer.h), so
the page is checked by two readers and two writers: both write the page's sample archive byte for byte. Entries are
streamed, never loaded whole, so a multi-gigabyte archive costs little memory.

Research: docs/guides/cpak-format.md
"""

from __future__ import annotations

import hashlib
import struct
from collections.abc import Iterable
from dataclasses import dataclass
from itertools import pairwise
from typing import BinaryIO

MAGIC = b"CPAK"
VERSION = 1
HEADER_SIZE = 64
RECORD_SIZE = 64
MIN_ALIGNMENT = 16
MAX_ALIGNMENT = 1 << 20
DEFAULT_ALIGNMENT = 4096
MAX_NAME_BYTES = 1024
#: The compression field's values and the names the tools print (docs/guides/cpak-format.md#compression).
COMPRESSIONS = {0: "none", 1: "lz4", 2: "zstd"}

_HEADER = struct.Struct("<4sHHIIIIQ32s")
_RECORD = struct.Struct("<QQQIHBB32s")
_HASHED_HEADER = 32  # the header bytes the index hash covers, before the index itself
_CHUNK = 1 << 20  # bytes read at a time when verifying an entry


class CpakError(Exception):
    """An archive or a name breaks the format; the message says which rule."""


@dataclass(frozen=True)
class Header:
    """The header's values (docs/guides/cpak-format.md#header)."""

    version: int
    alignment: int
    entry_count: int
    index_size: int
    archive_size: int
    index_sha256: bytes


@dataclass(frozen=True)
class Entry:
    """One record of the index (docs/guides/cpak-format.md#records)."""

    name: str
    offset: int
    stored_size: int
    size: int
    compression: int
    sha256: bytes


def compression_name(value: int) -> str:
    """The name of a compression value: none, lz4, zstd, or unknown."""
    return COMPRESSIONS.get(value, "unknown")


def check_name(name: str) -> None:
    """Raise CpakError unless `name` keeps the format's name rules (docs/guides/cpak-format.md#names)."""
    raw = name.encode("utf-8", errors="surrogatepass")
    if not 1 <= len(raw) <= MAX_NAME_BYTES:
        raise CpakError(f"a name must be 1 to {MAX_NAME_BYTES} bytes long")
    if any(byte < 0x20 or byte == 0x7F for byte in raw) or "\\" in name or ":" in name:
        raise CpakError(f"the name {name!r} holds a control character, a backslash or a colon")
    if any(segment in ("", ".", "..") for segment in name.split("/")):
        raise CpakError(f"the name {name!r} has an empty, . or .. segment")


def _read_exactly(handle: BinaryIO, count: int, what: str) -> bytes:
    """`count` bytes from `handle`, or CpakError naming `what` when it ends first."""
    data = handle.read(count)
    if len(data) != count:
        raise CpakError(f"the archive ends inside its {what}")
    return data


def _parse_header(raw: bytes) -> Header:
    """The header's values, checked against the rules that need nothing else."""
    magic, version, flags, alignment, count, index_size, reserved, archive_size, index_hash = _HEADER.unpack(raw)
    if magic != MAGIC:
        raise CpakError("not a Coney Pack (no CPAK magic)")
    if version != VERSION:
        raise CpakError(f"format version {version}; this reader knows only {VERSION}")
    if flags or reserved:
        raise CpakError("unknown header flags or reserved bytes set")
    if alignment & (alignment - 1) or not MIN_ALIGNMENT <= alignment <= MAX_ALIGNMENT:
        raise CpakError(f"alignment {alignment} is not a power of two from {MIN_ALIGNMENT} to {MAX_ALIGNMENT}")
    return Header(version, alignment, count, index_size, archive_size, index_hash)


def _parse_records(header: Header, index: bytes) -> list[Entry]:
    """The entries of a hashed index, each checked on its own."""
    if header.entry_count * RECORD_SIZE > len(index):
        raise CpakError("the index is smaller than its records")
    names = index[header.entry_count * RECORD_SIZE :]
    entries = []
    for i in range(header.entry_count):
        offset, stored, size, name_at, name_length, compression, flags, digest = _RECORD.unpack_from(
            index, i * RECORD_SIZE
        )
        if compression not in COMPRESSIONS:
            raise CpakError(f"unknown compression {compression}")
        if flags:
            raise CpakError("unknown entry flags set")
        if name_at + name_length > len(names):
            raise CpakError("an entry's name lies outside the name table")
        try:
            name = names[name_at : name_at + name_length].decode("utf-8")
        except UnicodeDecodeError as error:
            raise CpakError("an entry's name is not UTF-8") from error
        check_name(name)
        if compression == 0 and size != stored:
            raise CpakError(f"{name} is stored as it is but its two sizes differ")
        entries.append(Entry(name, offset, stored, size, compression, digest))
    return entries


def _check_layout(header: Header, entries: list[Entry]) -> None:
    """Names strictly rising in byte order; each entry aligned, after the index, inside the archive, not overlapping."""
    for before, after in pairwise(entries):
        if not before.name.encode() < after.name.encode():
            raise CpakError(f"{after.name} is out of name order or repeated")
    front = HEADER_SIZE + header.index_size
    for entry in entries:
        if entry.offset % header.alignment:
            raise CpakError(f"{entry.name} is not aligned to {header.alignment}")
        if entry.offset < front or entry.offset + entry.stored_size > header.archive_size:
            raise CpakError(f"{entry.name} lies outside the archive's data")
    stored = sorted((entry for entry in entries if entry.stored_size), key=lambda entry: entry.offset)
    for first, second in pairwise(stored):
        if first.offset + first.stored_size > second.offset:
            raise CpakError(f"{first.name} overlaps {second.name}")


def read_index(handle: BinaryIO, archive_size: int) -> tuple[Header, list[Entry]]:
    """Read and check the header and index of the archive open in `handle`, which is `archive_size` bytes long.

    Raises CpakError for anything the format refuses: a wrong magic or version, a size that differs from the header's,
    an index that does not match its SHA-256 (a damaged index), or records out of order, unaligned or overlapping.
    """
    handle.seek(0)
    head = _read_exactly(handle, HEADER_SIZE, "header")
    header = _parse_header(head)
    if archive_size != header.archive_size:
        raise CpakError(f"the archive is {archive_size} bytes; its header says {header.archive_size}")
    if HEADER_SIZE + header.index_size > archive_size:
        raise CpakError("the index runs past the end of the archive")
    index = _read_exactly(handle, header.index_size, "index")
    if hashlib.sha256(head[:_HASHED_HEADER] + index).digest() != header.index_sha256:
        raise CpakError("the index does not match its SHA-256 (a damaged index)")
    entries = _parse_records(header, index)
    _check_layout(header, entries)
    return header, entries


def verify_entry(handle: BinaryIO, entry: Entry) -> None:
    """Read `entry` from the archive in `handle` and check its SHA-256; CpakError for a damaged or compressed entry."""
    if entry.compression:
        raise CpakError(f"{entry.name} is compressed ({compression_name(entry.compression)}); no decoder yet")
    handle.seek(entry.offset)
    digest = hashlib.sha256()
    left = entry.stored_size
    while left:
        piece = handle.read(min(left, _CHUNK))
        if not piece:
            raise CpakError(f"{entry.name} runs past the end of the archive")
        digest.update(piece)
        left -= len(piece)
    if digest.digest() != entry.sha256:
        raise CpakError(f"{entry.name} does not match its SHA-256 (a damaged entry)")


def write(entries: Iterable[tuple[str, bytes]], alignment: int = DEFAULT_ALIGNMENT) -> bytes:
    """An archive of (name, bytes) entries, uncompressed, as the engine's writer makes it (cpak_writer.h)."""
    if alignment & (alignment - 1) or not MIN_ALIGNMENT <= alignment <= MAX_ALIGNMENT:
        raise CpakError(f"alignment {alignment} is not a power of two from {MIN_ALIGNMENT} to {MAX_ALIGNMENT}")
    items = list(entries)
    seen: dict[bytes, str] = {}
    for name, _ in items:
        check_name(name)
        folded = name.encode().lower()  # ASCII case only, as the engine's writer folds it
        if folded in seen:
            raise CpakError(f"{name} is already in the archive as {seen[folded]} (names must differ by more than case)")
        seen[folded] = name
    items.sort(key=lambda item: item[0].encode())
    encoded = [name.encode() for name, _ in items]
    index_size = RECORD_SIZE * len(items) + sum(len(name) for name in encoded)
    cursor = HEADER_SIZE + index_size
    offsets = []
    for _, data in items:
        cursor = -(-cursor // alignment) * alignment
        offsets.append(cursor)
        cursor += len(data)
    index = bytearray()
    name_at = 0
    for (_, data), raw, offset in zip(items, encoded, offsets, strict=True):
        index += _RECORD.pack(offset, len(data), len(data), name_at, len(raw), 0, 0, hashlib.sha256(data).digest())
        name_at += len(raw)
    index += b"".join(encoded)
    head = _HEADER.pack(MAGIC, VERSION, 0, alignment, len(items), index_size, 0, cursor, bytes(32))[:_HASHED_HEADER]
    out = bytearray(cursor)
    out[:HEADER_SIZE] = head + hashlib.sha256(head + index).digest()
    out[HEADER_SIZE : HEADER_SIZE + index_size] = index
    for (_, data), offset in zip(items, offsets, strict=True):
        out[offset : offset + len(data)] = data
    return bytes(out)
