# SPDX-License-Identifier: GPL-3.0-or-later
"""Read files from the player's own Xbox disc: a full disc image, a plain XISO, or an extracted folder.

The Xbox file system (XDVDFS) is a public format. A game partition starts at a fixed byte offset of the image: 0 for
an XISO that holds just the partition, `0x18300000` on an XGD1 disc image, `0x1FB20000` on an XGD2 one. Its sector
32 (2048-byte sectors) is the volume descriptor: the magic `MICROSOFT*XBOX*MEDIA`, then the sector and byte size of
the root directory table. A directory table is a binary search tree of records, each

    u16 left, u16 right   offsets of the subtrees in 4-byte units from the table's start (0: none)
    u32 sector, u32 size  where the file (or a subdirectory's table) is, relative to the partition
    u8 attributes         0x10: a directory
    u8 nameLength, name   then padding to a 4-byte boundary

Records never span a sector; the rest of a sector is filled with `0xff`. Files are never loaded whole: the 7.8 GB
image is read on demand through `XboxDisc.open`.

Research: docs/research/xbox-assets.md (The disc)
"""

from __future__ import annotations

import io
import struct
from dataclasses import dataclass
from pathlib import Path
from typing import BinaryIO, cast

from coney_tools.config import ConfigError
from coney_tools.disc import SECTOR, FileSlice

#: The volume descriptor's magic, at its start and again at byte 0x7ec.
MAGIC = b"MICROSOFT*XBOX*MEDIA"
#: Byte offsets where a game partition can start: a plain XISO, an XGD1 image, an XGD2 image.
PARTITION_OFFSETS: tuple[int, ...] = (0, 0x18300000, 0x1FB20000)
_VOLUME_SECTOR = 32  # the volume descriptor's sector within the partition
_RECORD = 14  # bytes of a directory record before its name
_DIRECTORY = 0x10  # attribute bit of a subdirectory


@dataclass(frozen=True)
class XdvdfsFile:
    """A file in the partition: its path (`/`-separated, as stored), first sector and byte size."""

    path: str
    sector: int  # relative to the partition's start
    size: int


def find_partition(handle: BinaryIO, path: Path) -> int:
    """Return the byte offset of the game partition in an image. Raises ConfigError when no offset has the magic."""
    for offset in PARTITION_OFFSETS:
        handle.seek(offset + _VOLUME_SECTOR * SECTOR)
        if handle.read(len(MAGIC)) == MAGIC:
            return offset
    raise ConfigError(f"{path}: not an Xbox disc image (no {MAGIC.decode()} volume descriptor found)")


def _read_table(handle: BinaryIO, partition: int, sector: int, size: int, path: Path) -> bytes:
    """Read one directory table, checking that the image holds all of it."""
    handle.seek(partition + sector * SECTOR)
    data = handle.read(size)
    if len(data) < size:
        raise ConfigError(f"{path}: a directory table at sector {sector} is cut off (image truncated?)")
    return data


def parse_table(data: bytes, where: str) -> list[tuple[str, int, int, bool]]:
    """Walk one directory table's tree; return `(name, sector, size, is_directory)` for each record, in tree order.

    `where` names the table in error messages. Raises ConfigError for a record that does not fit in the table or a
    subtree offset that points back into the walk (a loop).
    """
    records: list[tuple[str, int, int, bool]] = []
    if not data or data[:4] == b"\xff\xff\xff\xff":  # an empty directory
        return records
    stack = [0]
    seen: set[int] = set()
    while stack:
        pos = stack.pop()
        if pos in seen:
            raise ConfigError(f"{where}: directory tree loops back to byte {pos}")
        seen.add(pos)
        if pos + _RECORD > len(data):
            raise ConfigError(f"{where}: directory record at byte {pos} lies past the table's end")
        left, right, sector, size, attributes, name_length = struct.unpack_from("<HHIIBB", data, pos)
        end = pos + _RECORD + name_length
        if name_length == 0 or end > len(data):
            raise ConfigError(f"{where}: damaged directory record at byte {pos}")
        name = data[pos + _RECORD : end].decode("latin-1")
        records.append((name, sector, size, bool(attributes & _DIRECTORY)))
        # Visit the right subtree after the left one; 0xffff marks "none" in some images, like 0.
        for child in (right, left):
            if child not in (0, 0xFFFF):
                stack.append(child * 4)
    return records


def read_tree(handle: BinaryIO, partition: int, path: Path) -> tuple[dict[str, XdvdfsFile], int]:
    """Read every directory table of the partition at `partition`.

    Returns the files keyed by lowercased path (`audio/xbox000.xwb`) and the number of directories below the root.
    Raises ConfigError when the image is damaged.
    """
    handle.seek(partition + _VOLUME_SECTOR * SECTOR)
    descriptor = handle.read(SECTOR)
    if len(descriptor) < SECTOR or descriptor[: len(MAGIC)] != MAGIC:
        raise ConfigError(f"{path}: no Xbox volume descriptor at byte {partition + _VOLUME_SECTOR * SECTOR:#x}")
    root_sector, root_size = struct.unpack_from("<II", descriptor, len(MAGIC))
    files: dict[str, XdvdfsFile] = {}
    directories = 0
    pending = [("", root_sector, root_size)]
    visited: set[int] = set()
    while pending:
        prefix, sector, size = pending.pop()
        if sector in visited:  # a directory reached twice would list its files twice
            raise ConfigError(f"{path}: directory table at sector {sector} is reached twice")
        visited.add(sector)
        table = _read_table(handle, partition, sector, size, path)
        for name, child_sector, child_size, is_directory in parse_table(table, f"{path}: /{prefix}"):
            child = f"{prefix}{name}"
            if is_directory:
                directories += 1
                pending.append((child + "/", child_sector, child_size))
            else:
                files[child.lower()] = XdvdfsFile(child, child_sector, child_size)
    return files, directories


def _key(name: str) -> str:
    """The lookup key of a path: `/`-separated, without a leading slash, lowercased."""
    return name.replace("\\", "/").strip("/").lower()


class XboxDisc:
    """The Xbox disc as an image (full disc or XISO) or an extracted folder; open its files by path."""

    def __init__(self, path: Path) -> None:
        """Open `path`, an image or a folder. Raises ConfigError when it is neither or holds no Xbox partition."""
        self.path = path
        self.partition: int | None = None  # byte offset of the partition in an image; None for a folder
        self._files: dict[str, XdvdfsFile] = {}
        self._folder: dict[str, Path] = {}
        self.directories = 0
        if path.is_dir():
            # Walk the folder like the partition: every file by its relative path, every subfolder counted.
            for child in sorted(path.rglob("*")):
                relative = child.relative_to(path).as_posix()
                if child.is_dir():
                    self.directories += 1
                elif child.is_file():
                    self._folder[relative.lower()] = child
        elif path.is_file():
            try:
                with path.open("rb") as handle:
                    self.partition = find_partition(handle, path)
                    self._files, self.directories = read_tree(handle, self.partition, path)
            except OSError as error:
                raise ConfigError(f"{path}: cannot be read ({error})") from error
        else:
            raise ConfigError(f"{path}: not a folder or an Xbox disc image (does it exist?)")

    def files(self) -> list[tuple[str, int]]:
        """Every file as `(path, size)`, sorted by lowercased path; paths use `/` and keep the disc's spelling."""
        if self.partition is None:
            return [
                (file.relative_to(self.path).as_posix(), self.size(key)) for key, file in sorted(self._folder.items())
            ]
        return [(entry.path, entry.size) for _, entry in sorted(self._files.items())]

    def has(self, name: str) -> bool:
        """Whether the disc holds the file `name` (any case, `/` or `\\` separators)."""
        key = _key(name)
        return key in self._folder or key in self._files

    def size(self, name: str) -> int:
        """Byte size of the file `name`. Raises ConfigError when the disc has no such file."""
        key = _key(name)
        if key in self._folder:
            try:
                return self._folder[key].stat().st_size
            except OSError as error:
                raise ConfigError(f"{self.path}: cannot read {name} ({error})") from error
        if key in self._files:
            return self._files[key].size
        raise ConfigError(f"{self.path}: no {name} on the Xbox disc")

    def open(self, name: str) -> BinaryIO:
        """Open the file `name` for reading (seekable, streamed). Raises ConfigError when it is missing."""
        key = _key(name)
        try:
            if key in self._folder:
                return self._folder[key].open("rb")
            if key in self._files and self.partition is not None:
                entry = self._files[key]
                raw = FileSlice(self.path.open("rb"), self.partition + entry.sector * SECTOR, entry.size)
                return cast(BinaryIO, io.BufferedReader(raw, buffer_size=1 << 20))
        except OSError as error:
            raise ConfigError(f"{self.path}: cannot open {name} ({error})") from error
        raise ConfigError(f"{self.path}: no {name} on the Xbox disc")
