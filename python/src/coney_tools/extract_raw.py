# SPDX-License-Identifier: GPL-3.0-or-later
"""The `raw` stage of `coney-tools extract`: what no decoder takes yet, as stored, each with a JSON description.

* `raw/<kind>/<name>` and `<name>.json` for each WAD entry of a kind with no decoder (scene records, the streamed
  world's geometry, the level object lists, the font metrics and its bitmap, the memory card icon, the five old
  RenderWare files). The JSON holds the entry's index, hash, name, size and the values of its header.
* `raw/resources/<shape>/<name>.res` and `.json` for each distinct resource of a shape with no decoder: the
  resource exactly as stored (its header and chunks), and its chunk list with each type's name.

As decoders arrive, kinds and shapes leave the RAW sets below; nothing is ever both decoded and raw.

Research: docs/research/formats/inventory.md (which formats are raw), docs/research/formats/wad-contents.md
"""

from __future__ import annotations

import struct
from typing import Any

from coney_tools import chunk_names, rw, scenes, wad_kinds
from coney_tools.extract_output import Output, Report
from coney_tools.extract_wad import (
    SHAPE_ANIMATION,
    SHAPE_CHARACTER,
    SHAPE_GLOBAL,
    SHAPE_LEVEL,
    SHAPE_MODEL,
    SHAPE_OTHER,
    SHAPE_SCENE_LIST,
    Entry,
    Item,
)

KIND = "raw"
#: Entry kinds written raw (packs are lists of resources and come out as their resources).
RAW_KINDS = {
    wad_kinds.SCENE: "scn",
    wad_kinds.SECTOR_PARTS: "sec",
    wad_kinds.WORLD_STREAM: "wld",
    wad_kinds.MANIFEST: "mem",
    wad_kinds.OBJECT_LIST: "txt",
    wad_kinds.METRICS: "met",
    wad_kinds.ICON: "ico",
    wad_kinds.BITMAP: "bmp",
    wad_kinds.LEGACY_RW: "rws",
    wad_kinds.UNKNOWN: "bin",
}
#: Resource shapes written raw.
RAW_SHAPES = {SHAPE_MODEL, SHAPE_ANIMATION, SHAPE_CHARACTER, SHAPE_LEVEL, SHAPE_GLOBAL, SHAPE_SCENE_LIST, SHAPE_OTHER}


def _words(data: bytes, count: int) -> list[int]:
    """The first `count` little-endian words (fewer when the data is short)."""
    count = min(count, len(data) // 4)
    return list(struct.unpack_from(f"<{count}I", data, 0))


def describe(entry: Entry) -> dict[str, Any]:
    """The header values of an entry of a raw kind, as far as the format pages describe them."""
    data = entry.data
    if entry.kind == wad_kinds.SCENE:
        if scenes.is_header(data):
            header = scenes.parse_header(data, entry.label())
            return {"record": "header", "name": header.name, "frames": header.frames}
        segment = scenes.parse_segment(data, entry.label())
        return {"record": "segment", "name": segment.name, "next": segment.next_segment}
    if entry.kind == wad_kinds.SECTOR_PARTS:
        dictionary = rw.section_at(data, 16)
        (count,) = struct.unpack_from("<I", data, dictionary.end)
        at = dictionary.end + 4
        sectors = []
        for _ in range(count):
            (sector,) = struct.unpack_from("<I", data, at)
            sectors.append(sector)
            at = rw.section_at(data, at + 4).end
        return {"name_hash": f"{_words(data, 4)[3]:08x}", "atomics": count, "sectors": sectors}
    if entry.kind == wad_kinds.WORLD_STREAM:
        dictionary = rw.section_at(data, 4)
        world = rw.section_at(data, dictionary.end)
        return {"parts": _words(data, 1)[0], "world_bytes": world.size}
    if entry.kind == wad_kinds.MANIFEST:
        world, heap, count = struct.unpack_from("<III", data, 0)
        parts = [list(struct.unpack_from("<II", data, 12 + 8 * i)) for i in range(count)]
        return {"world_size": world, "world_heap": heap, "parts": parts}
    if entry.kind == wad_kinds.OBJECT_LIST:
        text = data.rstrip(b"\0").decode("ascii")
        return {"count": int(text.split(None, 1)[0]), "lines": len(text.splitlines())}
    return {"words": [f"{w:08x}" for w in _words(data, 4)]}


class RawStage:
    """Writes the raw kinds and shapes."""

    def __init__(self, output: Output) -> None:
        """Start the type."""
        self.output = output
        self.report = Report(KIND)
        output.start(KIND)

    def entry(self, entry: Entry) -> None:
        """Write an entry of a raw kind with its description."""
        extension = RAW_KINDS.get(entry.kind)
        if extension is None:
            return
        name = entry.name or f"{entry.record.hash:08x}.{extension}"
        path = self.output.write(KIND, f"raw/{entry.kind}/{name}", entry.data)
        record: dict[str, Any] = {
            "file": path,
            "index": entry.record.index,
            "hash": f"{entry.record.hash:08x}",
            "name": entry.name,
            "kind": entry.kind,
            "size": entry.record.size,
        }
        try:
            record["header"] = describe(entry)
        except (ValueError, struct.error, rw.RwError, scenes.SceneError) as error:
            self.report.problem(f"{entry.label()}: {error}")
        self.output.write_json(KIND, f"{path}.json", record)
        self.report.count(f"entries: {entry.kind}")

    def item(self, item: Item) -> None:
        """Write a resource of a raw shape with its chunk list."""
        if item.shape not in RAW_SHAPES:
            return
        path = self.output.write(KIND, f"raw/resources/{item.shape}/{item.label()}.res", item.resource_bytes())
        record = {
            "file": path,
            "hash": f"{item.resource.hash:08x}",
            "name": item.name,
            "shape": item.shape,
            "chunks": [
                {"type": c.type, "name": chunk_names.name(c.type), "size": c.size, "hash": f"{c.hash:08x}"}
                for c in item.resource.chunks
            ],
        }
        self.output.write_json(KIND, f"{path}.json", record)
        self.report.count(f"resources: {item.shape}")

    def finish(self) -> None:
        """Nothing to add."""
