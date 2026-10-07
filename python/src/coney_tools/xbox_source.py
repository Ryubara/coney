# SPDX-License-Identifier: GPL-3.0-or-later
"""The player's Xbox disc as a second source of assets: its archive's resources by hash, each level's world textures
and the movies, read straight from the image (or an XISO, or an extracted folder).

Research: docs/research/xbox-assets.md (The archive, Resource index, Resource images, Asset kinds)
"""

from __future__ import annotations

import hashlib
import re
from collections.abc import Iterator
from contextlib import ExitStack
from pathlib import Path
from typing import BinaryIO

from coney_tools import chunks, xbox, xbox_gfx, xbox_index, xbox_texture_map
from coney_tools.xbox_match import Candidate, ImageLocation
from coney_tools.xdvdfs import XboxDisc

#: The folder of the streamed world below `ee_files\`: `<level>.xlev` and its sectors `<level>_<n>.xsec`.
SECTORS = "sectors\\"
_SECTOR_LIMIT = 512  # sector numbers tried per level
#: A PS2 world file's level: `level99s_sec.wld`, `level99d_ms3.sec` -> `level99` (`s`/`d` are the PS2's halves).
_PS2_WORLD = re.compile(r"^(?P<level>[a-z]+\d*?)(?:s|d)?_(?:sec\.wld|ms\d+\.sec)$", re.IGNORECASE)
MOVIE_FOLDER = "bik"


def ps2_world_level(source: str) -> str | None:
    """The level a PS2 world file belongs to (`level99s_ms3.sec` -> `level99`), or None for another name."""
    match = _PS2_WORLD.match(source)
    return match.group("level").lower() if match else None


class XboxSource:
    """An open Xbox disc: the archive index, the resource index (every resource's chunks by hash), and the decoded
    textures asked for so far."""

    def __init__(self, path: Path, index_path: Path | None = None) -> None:
        """Open the disc and its resource index: the one at `index_path` when it was built from this disc, else a
        new one (one pass over the packs and resources, about 1 GB), written to `index_path` when given."""
        self.disc = XboxDisc(path)
        self.index = xbox.load_index(self.disc)
        self._stack = ExitStack()
        self._handles: dict[int, BinaryIO] = {}
        self.by_hash = {entry.hash: entry for entry in self.index.entries}
        resources = xbox_index.load(index_path, self.disc) if index_path is not None else None
        if resources is None:
            resources = xbox_index.build(self.disc, self.index, xbox.survey(self.disc, self.index))
            if index_path is not None:
                index_path.parent.mkdir(parents=True, exist_ok=True)
                index_path.write_bytes(xbox_index.to_bytes(resources))
        self.resources = resources
        #: The PS2 textures replaced so far and their Xbox textures, for the texture map (extract_xbox).
        self.replacements: list[xbox_texture_map.Replacement] = []
        self._entry_cache: tuple[int, bytes] | None = None
        self._world_cache: dict[str, list[Candidate]] = {}

    def identity(self) -> str:
        """SHA-1 of the disc's `default.xbe`, which tells the disc's version apart (the manifest records it)."""
        digest = hashlib.sha1()
        with self.disc.open(xbox.XBE_FILE) as handle:
            digest.update(handle.read())
        return digest.hexdigest()

    def close(self) -> None:
        """Close the volume files."""
        self._stack.close()

    def __enter__(self) -> XboxSource:
        """Use as a context manager."""
        return self

    def __exit__(self, *_: object) -> None:
        """Close on leaving the block."""
        self.close()

    def _volume(self, volume: int) -> BinaryIO:
        """The open handle of an archive volume (each opened once)."""
        if volume not in self._handles:
            self._handles[volume] = self._stack.enter_context(self.disc.open(self.index.volumes[volume]))
        return self._handles[volume]

    def chunk_bytes(self, location: xbox_index.ChunkLocation) -> bytes:
        """One indexed chunk's data."""
        handle = self._volume(location.volume)
        handle.seek(location.offset)
        return handle.read(location.size)

    def entry_bytes(self, index: int) -> bytes:
        """One archive entry's bytes (the last one read is kept, as resources of one pack come in a row)."""
        if self._entry_cache is not None and self._entry_cache[0] == index:
            return self._entry_cache[1]
        entry = self.index.entries[index]
        handle = self._volume(entry.volume)
        handle.seek(entry.offset)
        data = handle.read(entry.size)
        self._entry_cache = (index, data)
        return data

    def named_entry(self, name: str) -> bytes | None:
        """The bytes of the entry named `name` below `ee_files\\`, or None when the archive has none."""
        entry = self.by_hash.get(xbox.name_hash(name))
        return None if entry is None else self.entry_bytes(entry.index)

    def resource_textures(self, resource_hash: int) -> list[Candidate]:
        """The textures of the Xbox resource with this hash (one per `0x2a` chunk, in order); empty when none."""
        candidates = []
        for number, location in enumerate(self.resources.chunks(resource_hash, xbox.TEXTURE_CHUNK)):
            where = (location.volume, location.offset)
            candidates += _image_candidates(self.chunk_bytes(location), f"{resource_hash:08x}#{number}", where)
        return candidates

    def world_textures(self, level: str) -> list[Candidate]:
        """Every texture of a level's world: its `.xlev`, then each `.xsec` in sector order."""
        level = level.lower()
        if level not in self._world_cache:
            candidates: list[Candidate] = []
            for name in self.world_files(level):
                entry = self.by_hash[xbox.name_hash(SECTORS + name)]
                data = self.entry_bytes(entry.index)
                container = chunks.parse_container(data) if chunks.looks_like_container(data[:64], len(data)) else None
                if container is None:
                    candidates += _image_candidates(data, f"sectors/{name}", (entry.volume, entry.offset))
                    continue
                # One `.xsec` on the disc is a texture dictionary resource instead of a sector image.
                for resource in container.resources:
                    for number, chunk in enumerate(c for c in resource.chunks if c.type == xbox.TEXTURE_CHUNK):
                        raw = data[chunk.offset : chunk.offset + chunk.size]
                        where = (entry.volume, entry.offset + chunk.offset)
                        candidates += _image_candidates(raw, f"sectors/{name}#{number}", where)
            self._world_cache = {level: candidates}  # one level at a time: the PS2 pass goes level by level
        return self._world_cache[level]

    def world_files(self, level: str) -> Iterator[str]:
        """The names of a level's world files that are on the disc: `<level>.xlev`, `<level>_<n>.xsec`."""
        for name in [f"{level}.xlev", *(f"{level}_{n}.xsec" for n in range(_SECTOR_LIMIT))]:
            if xbox.name_hash(SECTORS + name) in self.by_hash:
                yield name

    def movies(self) -> dict[str, str]:
        """The movie files, lower-cased base name (`intro_hd`) to path on the disc."""
        found = {}
        for path, _ in self.disc.files():
            folder, _, name = path.replace("\\", "/").rpartition("/")
            if folder.lower() == MOVIE_FOLDER and name.lower().endswith(".bik"):
                found[name[:-4].lower()] = path
        return found


def _image_candidates(raw: bytes, label: str, where: tuple[int, int]) -> list[Candidate]:
    """The decoded textures of one resource image, labelled `<label>#<n>` when it holds more than one; `where` is
    the image's (volume, byte offset) in the archive."""
    image = xbox_gfx.parse_image(raw)
    textures = image.textures()
    result = []
    for number, texture in enumerate(textures):
        name = label if len(textures) == 1 and "#" in label else f"{label}#{number}"
        location = ImageLocation(where[0], where[1], len(raw), number)
        pixels = xbox_gfx.decode_texture(image, texture)
        result.append(Candidate(name, texture.width, texture.height, pixels, location=location))
    return result
