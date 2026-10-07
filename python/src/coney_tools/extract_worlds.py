# SPDX-License-Identifier: GPL-3.0-or-later
"""The `worlds` stage of `coney-tools extract`: each streamed world assembled into one glTF scene.

A streamed world is a manifest (`<world>_sec.mem`), a world stream (`<world>_sec.wld`: the BSP, whose atomic sectors
hold no triangles) and its parts (`<world>_ms<i>.sec`: one atomic per streamed sector). For every world,
`worlds/<world>/`:

* `world.gltf` and `.bin`: one node per streamed sector, placed at its sector plugin origin, holding the atomic its
  part gives it (packed positions times the atomic's position scale, texture coordinates times its second scale, both
  texture-coordinate sets kept); the node's extras give the sector index, the part and the sector's box. Each material
  references its texture by name, from the part's dictionary first, then the world stream's
  (`../../textures/worlds/<file>/<texture>.png`);
* `world.json`: the manifest, the world's flags and box, every atomic sector (box, streamed index, part, origin) in
  stream order, and the files written;
* `unused/<file>`: the part files no sector names (numbered above the manifest's count), as stored.

Coordinates are RenderWare's axes, as stored. A world's files arrive parts first; each world is assembled when its
world stream arrives and its parts are then freed.

Research: docs/research/world.md#data
"""

from __future__ import annotations

import json
import re
import struct
from dataclasses import dataclass, field
from typing import Any

from coney_tools import gltf, ps2mesh, rw, rwclump, wad_kinds
from coney_tools.extract_models import (
    TextureRef,
    add_material,
    add_primitive,
    build_primitive,
    first_texture,
    texture_listing,
    write_document,
)
from coney_tools.extract_output import Output, Report
from coney_tools.extract_wad import Entry, Item

KIND = "worlds"
_PART = re.compile(r"^(?P<world>.+)_ms(?P<part>\d+)\.sec$", re.IGNORECASE)
_STREAM = re.compile(r"^(?P<world>.+)_sec\.(?P<ext>wld|mem)$", re.IGNORECASE)


class WorldError(ValueError):
    """A world file whose layout does not hold together."""


@dataclass
class PartAtomic:
    """One atomic of a part file: the streamed sector it belongs to, and the atomic."""

    sector: int
    atomic: rwclump.Atomic
    geometry: rwclump.Geometry


def read_part(data: bytes) -> tuple[int, list[PartAtomic]]:
    """A part file (world.md#part-file): the offset of its texture dictionary, and its atomics."""
    dictionary = rw.section_at(data, 16)
    if dictionary.type != rw.TEX_DICTIONARY:
        raise WorldError("a part file does not start with a texture dictionary")
    (count,) = struct.unpack_from("<I", data, dictionary.end)
    at = dictionary.end + 4
    atomics = []
    for _ in range(count):
        (sector,) = struct.unpack_from("<I", data, at)
        section = rw.section_at(data, at + 4)
        if section.type != rw.ATOMIC:
            raise WorldError(f"section 0x{section.type:x} where a part's atomic should be")
        atomic = rwclump.read_atomic(data, section)
        atomics.append(PartAtomic(sector, atomic, rwclump.read_geometry_of_atomic(data, section)))
        at = section.end
    return 16, atomics


def read_stream(data: bytes) -> tuple[int, rwclump.World]:
    """A world stream (world.md#world-stream): its part count and the world."""
    (parts,) = struct.unpack_from("<I", data, 0)
    dictionary = rw.section_at(data, 4)
    if dictionary.type != rw.TEX_DICTIONARY:
        raise WorldError("a world stream does not start with a texture dictionary after its count")
    return parts, rwclump.read_world(data, dictionary.end)


def read_manifest(data: bytes) -> dict[str, Any]:
    """A manifest (world.md#manifest): the world's size and heap, and each part's size and heap."""
    size, heap, count = struct.unpack_from("<III", data, 0)
    parts = [{"size": s, "heap": h} for s, h in (struct.unpack_from("<II", data, 12 + 8 * i) for i in range(count))]
    return {"world_size": size, "world_heap": heap, "parts": parts}


@dataclass
class _World:
    """A world's files as they arrive."""

    parts: dict[int, tuple[str, bytes]] = field(default_factory=dict)
    manifest: dict[str, Any] | None = None


class WorldsStage:
    """Collects each world's files and assembles the world when its stream arrives."""

    def __init__(self, output: Output, textures: Any | None = None) -> None:
        """Start the type; `textures` is this run's textures stage, when it runs."""
        self.output = output
        self.textures = textures
        self.report = Report(KIND)
        output.start(KIND)
        self.worlds: dict[str, _World] = {}
        self.listing: dict[str, list[list[dict[str, Any]]]] | None = None
        self.index: list[dict[str, Any]] = []

    def _textures(self, source: str) -> list[dict[str, Any]]:
        """The texture list of a world file's dictionary (the first one it holds)."""
        # This run's textures stage grows as the pass goes on: look again when a file is not listed yet.
        if self.listing is None or (source not in self.listing and self.textures is not None):
            self.listing = texture_listing(self.output, self.textures, self.report)
        lists = self.listing.get(source, [])
        return lists[0] if lists else []

    def entry(self, entry: Entry) -> None:
        """Keep a part or manifest for its world; assemble the world when its stream arrives."""
        if entry.kind not in (wad_kinds.SECTOR_PARTS, wad_kinds.MANIFEST, wad_kinds.WORLD_STREAM):
            return
        name = entry.name or ""
        part = _PART.match(name)
        stream = _STREAM.match(name)
        if part is None and stream is None:
            self.report.problem(f"{entry.label()}: a world file without a recovered name; skipped")
            return
        if part is not None:
            world = self.worlds.setdefault(part["world"].lower(), _World())
            world.parts[int(part["part"])] = (entry.label(), entry.data)
            return
        assert stream is not None
        key = stream["world"].lower()
        world = self.worlds.setdefault(key, _World())
        if stream["ext"].lower() == "mem":
            world.manifest = read_manifest(entry.data)
            return
        self._assemble(stream["world"], entry, self.worlds.pop(key))

    def item(self, item: Item) -> None:
        """Worlds are entries only."""

    def _assemble(self, name: str, stream: Entry, world: _World) -> None:
        """Write one world's glTF and JSON."""
        folder = f"worlds/{name}"
        try:
            count, bsp = read_stream(stream.data)
        except (WorldError, rw.RwError, struct.error, ValueError, IndexError) as error:
            self.report.problem(f"{stream.label()}: {error}")
            return
        sectors = {s.streamed_index: s for s in bsp.sectors if s.streamed_index is not None}
        stream_textures = self._textures(stream.label())
        document = gltf.Document()
        materials: dict[str, int] = {}
        roots: list[int] = []
        record: dict[str, Any] = {
            "name": name,
            "stream": stream.label(),
            "manifest": world.manifest,
            "part_count": count,
            "flags": bsp.flags,
            "box": {"min": list(bsp.box_min), "max": list(bsp.box_max)},
            "sectors": [
                {
                    "min": list(s.box_min),
                    "max": list(s.box_max),
                    "index": s.streamed_index,
                    "part": s.part,
                    "origin": list(s.origin),
                }
                for s in bsp.sectors
            ],
            "files": {},
        }
        counts = {"atomics": 0, "vertices": 0, "triangles": 0}
        for number in sorted(world.parts):
            label, data = world.parts[number]
            if number > count:
                written = self.output.write(KIND, f"{folder}/unused/{label}", data)
                record["files"].setdefault("unused", []).append(written)
                self.report.count("unused parts")
                continue
            part_textures = self._textures(label)
            try:
                _, atomics = read_part(data)
                for part_atomic in atomics:
                    node = self._atomic(
                        document, materials, part_atomic, sectors, number, part_textures, stream_textures, counts
                    )
                    roots.append(node)
            except (WorldError, rw.RwError, ps2mesh.MeshError, struct.error, ValueError, IndexError, KeyError) as error:
                self.report.problem(f"{label}: {error}")
                continue
            self.report.count("parts")
        document.json["scenes"][0]["nodes"] = roots
        record["files"]["model"] = write_document(self.output, KIND, f"{folder}/world", document)
        record.update(counts)
        record["files"]["world"] = self.output.write_json(KIND, f"{folder}/world.json", record)
        self.index.append({"name": name, "file": record["files"]["world"], **counts})
        self.report.count("worlds")

    def _atomic(
        self,
        document: gltf.Document,
        materials: dict[str, int],
        part_atomic: PartAtomic,
        sectors: dict[int, rwclump.Sector],
        part: int,
        part_textures: list[dict[str, Any]],
        stream_textures: list[dict[str, Any]],
        counts: dict[str, int],
    ) -> int:
        """Add one part atomic as a node at its sector's origin; returns the node."""
        sector = sectors.get(part_atomic.sector)
        if sector is None:
            raise WorldError(f"an atomic names sector {part_atomic.sector}, which the world does not have")
        geometry = part_atomic.geometry
        atomic = part_atomic.atomic
        primitives = []
        for mesh in geometry.meshes:
            primitive = build_primitive(mesh, geometry.strip, atomic.position_scale, atomic.texture_scale)
            if len(primitive.triangles) == 0:
                continue
            material = geometry.materials[mesh.material]
            texture = self._texture(material.texture, part_textures, stream_textures)
            key = json.dumps([material.colour, material.matfx, texture.path if texture else None])
            if key not in materials:
                materials[key] = add_material(document, material, texture, "../../")
            primitives.append(add_primitive(document, primitive, materials[key]))
            counts["vertices"] += len(primitive.positions)
            counts["triangles"] += len(primitive.triangles)
        counts["atomics"] += 1
        node: dict[str, Any] = {
            "name": f"sector_{part_atomic.sector}",
            "translation": [float(v) for v in sector.origin],
            "extras": {
                "sector": part_atomic.sector,
                "part": part,
                "min": list(sector.box_min),
                "max": list(sector.box_max),
            },
        }
        if primitives:
            node["mesh"] = document.add("meshes", {"primitives": primitives})
        return document.add("nodes", node)

    @staticmethod
    def _texture(
        name: str | None, part_textures: list[dict[str, Any]], stream_textures: list[dict[str, Any]]
    ) -> TextureRef | None:
        """A material's texture by name: the part's dictionary first, then the world stream's."""
        if not name:
            return None
        for listed in (part_textures, stream_textures):
            if any(t["name"] == name and t.get("file") for t in listed):
                return first_texture(listed, name)
        return None

    def finish(self) -> None:
        """Report worlds whose stream never came, and write the index."""
        for name, world in self.worlds.items():
            self.report.problem(f"{name}: {len(world.parts)} parts but no world stream")
        self.output.write_json(KIND, "worlds/index.json", {"worlds": self.index})
