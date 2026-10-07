# SPDX-License-Identifier: GPL-3.0-or-later
"""One pass over WARRIORS.WAD for `coney-tools extract`: each entry is read once, classified, and handed to every
stage that is running; each resource of a pack or standalone entry is handed over once, however many packs hold it.

The stages here are the WAD's own types: `index` (what every entry and resource is), `scripts` (the compiled Lua
chunks as stored), `textures` (texture dictionaries as PNG) and `raw` (every entry or resource no decoder takes yet,
as stored, with a JSON description). Other modules add stages for the types they decode.

Research: docs/research/formats/wad-contents.md, docs/research/formats/inventory.md
"""

from __future__ import annotations

import hashlib
import struct
from collections.abc import Callable, Iterable, Iterator
from dataclasses import dataclass, field
from typing import Any, Protocol

from coney_tools import chunk_names, ps2tex, rw, wad, wad_kinds
from coney_tools.chunks import Container, Resource
from coney_tools.disc import Disc
from coney_tools.extract_output import Output, Report, png_bytes

#: Resource shapes: what a resource is, from the chunk types it holds (docs/research/formats/wad-contents.md).
SHAPE_TEXTURES = "textures"
SHAPE_MODEL = "model"
SHAPE_ANIMATION = "animation"
SHAPE_CHARACTER = "character"
SHAPE_LEVEL = "level"
SHAPE_GLOBAL = "global"
SHAPE_SCENE_LIST = "scene-list"
SHAPE_OTHER = "other"


def resource_shape(resource: Resource) -> str:
    """The shape of a resource, from its set of chunk types."""
    types = resource.types
    if 0x17 in types:
        return SHAPE_LEVEL
    if 0x29 in types:
        return SHAPE_GLOBAL
    if types == {0x43}:
        return SHAPE_SCENE_LIST
    if 0x08 in types:
        return SHAPE_CHARACTER
    if types <= {0x00, 0x02} and types:
        return SHAPE_ANIMATION
    if types and types <= {0x47, 0x28} and 0x47 in types:
        return SHAPE_MODEL
    if types and types <= {0x2A, 0x4C} and 0x2A in types:
        return SHAPE_TEXTURES
    return SHAPE_OTHER


@dataclass
class Entry:
    """One WAD entry as the stages see it: the index record, its bytes, its kind and its name (when recovered)."""

    record: wad.WadEntry
    data: bytes
    kind: str
    container: Container | None
    name: str | None

    def label(self) -> str:
        """The entry's name, or its hash in hex."""
        return self.name or f"{self.record.hash:08x}"


@dataclass
class Item:
    """One distinct resource: where it was first found, its bytes and parsed chunks, and its name."""

    resource: Resource
    data: bytes  # the whole entry the resource was found in; offsets in `resource` count from its start
    shape: str
    name: str | None
    digest: str  # SHA-1 of the resource's bytes, to tell two resources with one hash apart
    variant: int  # 1 for the first resource with this hash, 2 for a second, different one ...
    entries: list[int] = field(default_factory=list)  # WAD entries (packs or standalone) that hold it

    def label(self) -> str:
        """The resource's name, or its hash in hex; `~2` etc. for a later variant of the same hash."""
        base = self.name or f"{self.resource.hash:08x}"
        return base if self.variant == 1 else f"{base}~{self.variant}"

    def chunk_bytes(self, index: int) -> bytes:
        """The data of the resource's `index`-th chunk."""
        chunk = self.resource.chunks[index]
        return self.data[chunk.offset : chunk.offset + chunk.size]

    def resource_bytes(self) -> bytes:
        """The resource as stored: its 16-byte header and every chunk with its header."""
        last = self.resource.chunks[-1]
        return self.data[self.resource.offset : last.offset + last.size]


class Stage(Protocol):
    """A part of the extraction that looks at the WAD's entries and resources as the pass reaches them."""

    report: Report

    def entry(self, entry: Entry) -> None:
        """Called once per WAD entry, in index order."""

    def item(self, item: Item) -> None:
        """Called once per distinct resource, in the order first found."""

    def finish(self) -> None:
        """Called after the last entry: write indexes and totals."""


def entry_name(entry: Entry, extension: str) -> str:
    """A file name for an entry: its recovered name, or `<hash>.<extension>`."""
    return entry.name or f"{entry.record.hash:08x}.{extension}"


def walk(
    disc: Disc,
    entries: list[wad.WadEntry],
    names: dict[int, str],
    resource_names: Callable[[int], str | None],
    stages: Iterable[Stage],
) -> None:
    """Read every entry once, classify it and hand it, then its new resources, to every stage."""
    stages = list(stages)
    seen: dict[int, list[str]] = {}  # resource hash -> digests of its variants
    items: dict[tuple[int, str], Item] = {}

    def deliver(entry: Entry) -> None:
        for stage in stages:
            stage.entry(entry)
        for resource in entry.container.resources if entry.container else ():
            last = resource.chunks[-1]
            digest = hashlib.sha1(entry.data[resource.offset : last.offset + last.size]).hexdigest()
            known = items.get((resource.hash, digest))
            if known is not None:
                known.entries.append(entry.record.index)
                continue
            variants = seen.setdefault(resource.hash, [])
            variants.append(digest)
            item = Item(
                resource,
                entry.data,
                resource_shape(resource),
                resource_names(resource.hash),
                digest,
                len(variants),
                [entry.record.index],
            )
            items[(resource.hash, digest)] = item
            for stage in stages:
                stage.item(item)

    held: Entry | None = None  # one entry of look-ahead: a bank's samples are known by the index after them
    with disc.open(wad.WAD_FILE) as handle:
        for record in entries:
            handle.seek(record.offset)
            data = handle.read(record.size)
            classified = wad_kinds.classify(data)
            entry = Entry(record, data, classified.kind, classified.container, names.get(record.hash))
            if held is not None:
                if entry.kind == wad_kinds.BANK_INDEX and held.kind == wad_kinds.UNKNOWN:
                    held.kind = wad_kinds.BANK_SAMPLES
                deliver(held)
            held = entry
    if held is not None:
        deliver(held)
    for stage in stages:
        stage.finish()


# --- index ---------------------------------------------------------------------------------------------------------


class IndexStage:
    """`index/wad.json`: every entry with its kind and name, and every pack's or resource's chunks; the map a
    loader of the extracted folder starts from."""

    KIND = "index"

    def __init__(self, output: Output) -> None:
        """Start the type."""
        self.output = output
        self.report = Report(self.KIND)
        output.start(self.KIND)
        self.entries: list[dict[str, Any]] = []
        self.resources: dict[str, dict[str, Any]] = {}

    def entry(self, entry: Entry) -> None:
        """Record the entry, and for a container the hashes of its resources."""
        record: dict[str, Any] = {
            "index": entry.record.index,
            "hash": f"{entry.record.hash:08x}",
            "name": entry.name,
            "kind": entry.kind,
            "size": entry.record.size,
        }
        if entry.container is not None:
            record["resources"] = [f"{r.hash:08x}" for r in entry.container.resources]
        self.entries.append(record)
        self.report.count(f"entries: {entry.kind}")

    def item(self, item: Item) -> None:
        """Record a distinct resource and its chunks."""
        key = f"{item.resource.hash:08x}" if item.variant == 1 else f"{item.resource.hash:08x}~{item.variant}"
        self.resources[key] = {
            "name": item.name,
            "shape": item.shape,
            "sha1": item.digest,
            "chunks": [
                {"type": c.type, "name": chunk_names.name(c.type), "size": c.size, "hash": f"{c.hash:08x}"}
                for c in item.resource.chunks
            ],
        }
        self.report.count(f"resources: {item.shape}")

    def finish(self) -> None:
        """Write the index."""
        self.output.write_json(self.KIND, "index/wad.json", {"entries": self.entries, "resources": self.resources})


# --- scripts -------------------------------------------------------------------------------------------------------


class ScriptsStage:
    """`scripts/<name>.lua`: every compiled Lua 4.0 chunk exactly as stored (`luac` output, not source), and
    `scripts/index.json`. The container is described on docs/research/formats/lua-chunks.md."""

    KIND = "scripts"

    def __init__(self, output: Output) -> None:
        """Start the type."""
        self.output = output
        self.report = Report(self.KIND)
        output.start(self.KIND)
        self.index: list[dict[str, Any]] = []

    def entry(self, entry: Entry) -> None:
        """Write a Lua chunk."""
        if entry.kind != wad_kinds.LUA:
            return
        path = self.output.write(self.KIND, f"scripts/{entry_name(entry, 'lua')}", entry.data)
        self.index.append(
            {"file": path, "hash": f"{entry.record.hash:08x}", "name": entry.name, "size": len(entry.data)}
        )
        self.report.count("named chunks" if entry.name else "unnamed chunks")

    def item(self, item: Item) -> None:
        """Scripts are never resources."""

    def finish(self) -> None:
        """Write the index."""
        self.output.write_json(self.KIND, "scripts/index.json", {"chunks": self.index})


# --- textures ------------------------------------------------------------------------------------------------------


def dictionaries_in(data: bytes, start: int, end: int) -> Iterator[int]:
    """The offsets of the texture dictionary sections found in a run of RenderWare sections in `data[start:end]`."""
    at = start
    view = memoryview(data)[:end]
    while at + rw.HEADER <= end:
        try:
            section = rw.section_at(view, at)
        except rw.RwError:
            return
        if section.stamp not in rw.STAMPS:
            return
        if section.type == rw.TEX_DICTIONARY:
            yield at
        at = section.end


@dataclass
class _Dictionary:
    """One written texture dictionary, for the index."""

    folder: str
    source: str
    textures: list[dict[str, Any]]
    sprites: list[list[float]] | None = None
    resource_hash: int | None = None  # the resource the dictionary came from; None for a streamed-world file


class TexturesStage:
    """`textures/<dictionary>/<texture>.png` for every texture of every distinct dictionary: resources (`0x2A`), the
    streamed world's dictionaries (`_sec.wld`, `_ms<i>.sec`) and the level files' own; then
    `textures/index.json`, which lists each dictionary's textures with their sizes, depth, mipmap count, filter and
    addressing, and a sprite sheet's rectangles (chunk `0x4C`)."""

    KIND = "textures"

    def __init__(self, output: Output) -> None:
        """Start the type."""
        self.output = output
        self.report = Report(self.KIND)
        output.start(self.KIND)
        self.dictionaries: list[_Dictionary] = []

    def _write(
        self, folder: str, source: str, data: bytes, start: int, resource_hash: int | None = None
    ) -> _Dictionary | None:
        """Decode and write one dictionary; None (with a problem noted) when it does not parse."""
        try:
            textures = ps2tex.parse_dictionary(data, start)
        except (rw.RwError, ps2tex.TextureError, struct.error) as error:
            self.report.problem(f"{source}: {error}")
            return None
        listed = []
        for texture in textures:
            record: dict[str, Any] = {
                "name": texture.name,
                "mask": texture.mask or None,
                "width": texture.header.width,
                "height": texture.header.height,
                "depth": texture.depth,
                "mipmaps": texture.header.levels,
                "filter": texture.filter_mode,
                "address_u": texture.address_u,
                "address_v": texture.address_v,
            }
            try:
                pixels = ps2tex.decode(texture)
            except (ps2tex.TextureError, ValueError, IndexError, struct.error) as error:
                self.report.problem(f"{source}/{texture.name}: {error}")
                record["file"] = None
            else:
                record["file"] = self.output.write(self.KIND, f"{folder}/{texture.name}.png", png_bytes(pixels))
                self.report.count("textures")
            listed.append(record)
        dictionary = _Dictionary(folder, source, listed, resource_hash=resource_hash)
        self.dictionaries.append(dictionary)
        self.report.count("dictionaries")
        return dictionary

    def entry(self, entry: Entry) -> None:
        """The streamed world's dictionaries (the resources come through `item`)."""
        if entry.kind == wad_kinds.WORLD_STREAM:
            self._write(f"textures/worlds/{entry.label()}", entry.label(), entry.data, 4)
        elif entry.kind == wad_kinds.SECTOR_PARTS:
            self._write(f"textures/worlds/{entry.label()}", entry.label(), entry.data, 16)

    def item(self, item: Item) -> None:
        """A resource's texture dictionaries (and its sprite sheet, chunk `0x4C`)."""
        written = None
        for index, chunk in enumerate(item.resource.chunks):
            if chunk.type == 0x2A:
                for start in dictionaries_in(item.data, chunk.offset, chunk.offset + chunk.size):
                    written = self._write(
                        f"textures/{item.label()}", item.label(), item.data, start, item.resource.hash
                    )
            elif chunk.type == 0x4C and written is not None:
                written.sprites = sprite_rectangles(item.chunk_bytes(index))

    def finish(self) -> None:
        """Write the index."""
        listing = []
        for d in self.dictionaries:
            record: dict[str, Any] = {"folder": d.folder, "source": d.source}
            if d.resource_hash is not None:
                record["resource"] = f"{d.resource_hash:08x}"
            record["textures"] = d.textures
            if d.sprites is not None:
                record["sprites"] = d.sprites
            listing.append(record)
        self.output.write_json(self.KIND, "textures/index.json", {"dictionaries": listing})


def sprite_rectangles(data: bytes) -> list[list[float]]:
    """A sprite sheet's rectangles `{u0, v0, u1, v1}` (chunk `0x4C`, docs/research/gui.md#particle-page): the count
    at `+0x04`, the rectangles from `+0x14`."""
    if len(data) < 0x14:
        return []
    (count,) = struct.unpack_from("<I", data, 0x04)
    count = min(count, (len(data) - 0x14) // 16)
    return [list(struct.unpack_from("<4f", data, 0x14 + 16 * i)) for i in range(count)]
