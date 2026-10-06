# SPDX-License-Identifier: GPL-3.0-or-later
"""Read files from the player's own disc: a mounted disc or folder, or an `.iso` image.

The ISO 9660 reader is deliberately small: it reads the primary volume descriptor, the root directory and the
folders directly below it, which is all the game's disc needs (`SLUS_212.15`, `WARRIORS.DIR` and `WARRIORS.WAD` sit
in the root, the streamed sound in `IOP/`). A file in a folder is named `FOLDER/FILE`. Sectors are 2048 bytes.
Files are never loaded whole: `Disc.open` returns a seekable stream that reads on demand, so the 1.4 GB WAD costs
no memory.
"""

from __future__ import annotations

import io
import struct
from dataclasses import dataclass
from pathlib import Path
from typing import BinaryIO, cast

from coney_tools.config import ConfigError

SECTOR = 2048
_PVD_SECTOR = 16


class FileSlice(io.RawIOBase):
    """A read-only, seekable view of `length` bytes of an open file, starting at `start`."""

    def __init__(self, handle: BinaryIO, start: int, length: int) -> None:
        super().__init__()
        self._handle = handle
        self._start = start
        self._length = length
        self._pos = 0

    def readable(self) -> bool:
        return True

    def seekable(self) -> bool:
        return True

    def tell(self) -> int:
        return self._pos

    def seek(self, offset: int, whence: int = io.SEEK_SET) -> int:
        """Move within the slice; a position before its start clamps to 0, one past its end reads nothing."""
        base = {io.SEEK_SET: 0, io.SEEK_CUR: self._pos, io.SEEK_END: self._length}[whence]
        self._pos = max(0, base + offset)
        return self._pos

    def readinto(self, buffer: bytearray | memoryview) -> int:  # type: ignore[override]
        """Fill `buffer` from the slice, never past its end; returns the bytes read (0 at the end)."""
        want = min(len(buffer), max(0, self._length - self._pos))
        if want == 0:
            return 0
        self._handle.seek(self._start + self._pos)
        data = self._handle.read(want)
        buffer[: len(data)] = data
        self._pos += len(data)
        return len(data)

    def close(self) -> None:
        """Close the underlying file too: each slice owns the handle `Disc.open` opened for it."""
        self._handle.close()
        super().close()


@dataclass(frozen=True)
class IsoEntry:
    """A file in the ISO's root directory or a folder below it."""

    extent: int  # first sector
    size: int  # bytes


def _clean_name(raw: str) -> str:
    """Drop the ISO 9660 `;1` version suffix and a trailing dot; a backslash separates folders as `/` does."""
    return raw.replace("\\", "/").split(";", 1)[0].rstrip(".")


def _read_directory(
    handle: BinaryIO, path: Path, extent: int, size: int
) -> tuple[dict[str, IsoEntry], dict[str, IsoEntry]]:
    """One ISO 9660 directory: its files and its folders, keyed by upper-case name without the `;1` suffix."""
    handle.seek(extent * SECTOR)
    data = handle.read(size)
    if len(data) < size:
        raise ConfigError(f"{path}: a directory is cut off (image truncated?)")
    files: dict[str, IsoEntry] = {}
    folders: dict[str, IsoEntry] = {}
    pos = 0
    while pos < len(data):
        length = data[pos]
        if length == 0:  # records never span sectors; zero padding means "next sector"
            pos = (pos // SECTOR + 1) * SECTOR
            continue
        record = data[pos : pos + length]
        if length < 34 or len(record) < length:
            raise ConfigError(f"{path}: damaged directory record at byte {pos} of a directory")
        entry_extent, entry_size = struct.unpack_from("<I4xI", record, 2)
        name = record[33 : 33 + record[32]]
        pos += length
        if name in (b"\x00", b"\x01"):  # "." and ".."
            continue
        key = _clean_name(name.decode("latin-1")).upper()
        (folders if record[25] & 2 else files)[key] = IsoEntry(entry_extent, entry_size)
    return files, folders


def read_iso_root(handle: BinaryIO, path: Path) -> dict[str, IsoEntry]:
    """Parse the primary volume descriptor and the root directory of an ISO 9660 image.

    Returns the files of the root directory, and of each folder directly below it as `FOLDER/FILE`, keyed by
    upper-case name without the `;1` suffix. Raises ConfigError when `path` is not an ISO 9660 image or a directory
    is damaged.
    """
    handle.seek(_PVD_SECTOR * SECTOR)
    pvd = handle.read(SECTOR)
    if len(pvd) < SECTOR or pvd[0] != 1 or pvd[1:6] != b"CD001":
        raise ConfigError(f"{path}: not an ISO 9660 image (no primary volume descriptor at sector 16)")
    if struct.unpack_from("<H", pvd, 128)[0] != SECTOR:
        raise ConfigError(f"{path}: unsupported ISO logical block size (only 2048-byte sectors)")
    root_extent, root_size = struct.unpack_from("<I4xI", pvd[156:190], 2)
    files, folders = _read_directory(handle, path, root_extent, root_size)
    for folder, entry in folders.items():
        try:
            inner, _ = _read_directory(handle, path, entry.extent, entry.size)
        except ConfigError:  # a folder past the end of a cut-down image: its files are simply not there
            continue
        files.update({f"{folder}/{name}": item for name, item in inner.items()})
    return files


class Disc:
    """The game's disc as a folder or an `.iso` image; open files from its root, or a folder below it, by name."""

    def __init__(self, path: Path) -> None:
        """Open `path` (a directory or an ISO 9660 image). Raises ConfigError when it is neither."""
        self.path = path
        self._iso: dict[str, IsoEntry] | None = None
        self._folder: dict[str, Path] | None = None
        if path.is_dir():
            self._folder = {}
            for child in path.iterdir():
                if child.is_file():
                    self._folder[_clean_name(child.name).upper()] = child
                elif child.is_dir():  # one level of folders, as on the disc (`IOP/BFW.SND`)
                    for inner in child.iterdir():
                        if inner.is_file():
                            self._folder[f"{child.name}/{_clean_name(inner.name)}".upper()] = inner
        elif path.is_file():
            try:
                with path.open("rb") as handle:
                    self._iso = read_iso_root(handle, path)
            except OSError as error:
                raise ConfigError(f"{path}: cannot be read ({error})") from error
        else:
            raise ConfigError(f"{path}: not a folder or an ISO image (does it exist?)")

    def has(self, name: str) -> bool:
        """Whether the disc root holds `name`; any case, with a `;1` suffix or trailing dot ignored, as in C++."""
        key = _clean_name(name).upper()
        return key in (self._folder if self._folder is not None else self._iso or {})

    def size(self, name: str) -> int:
        """Byte size of the root file `name`. Raises ConfigError when the disc has no such file."""
        key = _clean_name(name).upper()
        try:
            if self._folder is not None and key in self._folder:
                return self._folder[key].stat().st_size
        except OSError as error:
            raise ConfigError(f"{self.path}: cannot read {name} ({error})") from error
        if self._iso is not None and key in self._iso:
            return self._iso[key].size
        raise ConfigError(f"{self.path}: no {name} in the disc root")

    def open(self, name: str) -> BinaryIO:
        """Open the root file `name` for reading (seekable, streamed). Raises ConfigError when it is missing."""
        key = _clean_name(name).upper()
        try:
            if self._folder is not None and key in self._folder:
                return self._folder[key].open("rb")
            if self._iso is not None and key in self._iso:
                entry = self._iso[key]
                raw = FileSlice(self.path.open("rb"), entry.extent * SECTOR, entry.size)
                return cast(BinaryIO, io.BufferedReader(raw, buffer_size=1 << 20))
        except OSError as error:
            raise ConfigError(f"{self.path}: cannot open {name} ({error})") from error
        raise ConfigError(f"{self.path}: no {name} in the disc root")
