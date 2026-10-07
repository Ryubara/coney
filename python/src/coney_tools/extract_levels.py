# SPDX-License-Identifier: GPL-3.0-or-later
"""The `levels` stage of `coney-tools extract`: each level file (`.lev`) taken apart into open files.

For every level resource, `levels/<name>/`:

* `level.json`: the chunk list, the 48-byte level header (`0x17`, stale tool pointers) in hex and the files written;
* `collision.json` and `collision.gltf`: the collision mesh (header, vertices, triangles with flags, material and area
  byte, the grid's non-empty cells), and the triangles as a glTF mesh with one primitive per material id;
* `paths.json`: the path data (`0x40`) resolved: areas, polygons with their slab edge lists, route nodes and edges;
* `occluders.json`: each occluder's four points as stored (game axes);
* `subtitles.json`: the caption text, language -> scene -> captions with their kind;
* `skyline.gltf`, `skybox.gltf`, `cloudbox.gltf`: the three background models, each with its dictionary's first
  texture (as the game links them), and `glows.gltf`: the level world of light glows;
* `source.lev`: the resource exactly as stored, so nothing the decoders leave out is lost.

Every coordinate keeps the axes it is stored in: collision, paths and occluders in game axes, the models and the glow
world in RenderWare's.

Research: docs/research/level-loading.md#the-level-file, docs/research/collision.md#chunks,
docs/research/movies.md#caption-text
"""

from __future__ import annotations

import struct
from dataclasses import dataclass
from typing import Any

import numpy as np

from coney_tools import chunk_names, gltf, ps2mesh, rw, rwclump
from coney_tools.extract_models import clump_document, first_texture, texture_listing, world_document, write_document
from coney_tools.extract_output import Output, Report
from coney_tools.extract_wad import SHAPE_LEVEL, Entry, Item

KIND = "levels"
#: The three background models, in the order their `0x2A`/`0x47` pairs come in the file.
BACKGROUNDS = ("skyline", "skybox", "cloudbox")
LANGUAGES = ("ENGLISH", "GERMAN", "SPANISH", "FRENCH", "ITALIAN")


class LevelError(ValueError):
    """A level chunk whose counts or offsets do not hold together."""


def decode_collision(header: bytes, vertices: bytes, triangles: bytes, grid: bytes, lists: bytes) -> dict[str, Any]:
    """The collision mesh: chunks `0x03`, `0x07`, `0x04`, `0x05` and `0x06` (collision.md)."""
    if len(header) < 160:
        raise LevelError("a collision header is shorter than 160 bytes")
    matrix = [list(struct.unpack_from("<4f", header, 0x10 + 16 * row)) for row in range(4)]
    clamp_min = list(struct.unpack_from("<4f", header, 0x50))
    clamp_max = list(struct.unpack_from("<4f", header, 0x60))
    nx, ny, nz = struct.unpack_from("<3H", header, 0x70)
    (list_count,) = struct.unpack_from("<I", header, 0x7C)
    (triangle_count,) = struct.unpack_from("<H", header, 0x84)
    (vertex_count,) = struct.unpack_from("<I", header, 0x90)
    (lowest,) = struct.unpack_from("<f", header, 0x98)
    if vertex_count * 16 > len(vertices) or triangle_count * 10 > len(triangles) or nx * ny * nz * 4 > len(grid):
        raise LevelError("the collision header's counts run past its chunks")
    points = np.frombuffer(vertices, "<f4", vertex_count * 4).reshape(-1, 4)[:, :3]
    records = [struct.unpack_from("<3HHBB", triangles, 10 * i) for i in range(triangle_count)]
    words = np.frombuffer(lists, "<u2", min(list_count, len(lists) // 2))
    cells = []
    for index, offset in enumerate(struct.unpack_from(f"<{nx * ny * nz}I", grid)):
        if offset == 0:
            continue
        if offset >= len(words) or offset + 1 + words[offset] > len(words):
            raise LevelError(f"grid cell {index} points past the index lists")
        x, rest = index % nx, index // nx
        cells.append([x, rest % ny, rest // ny, [int(t) for t in words[offset + 1 : offset + 1 + words[offset]]]])
    return {
        "world_to_grid": matrix,
        "grid_min": clamp_min,
        "grid_max": clamp_max,
        "grid_size": [nx, ny, nz],
        "lowest_z": lowest,
        "vertices": [[round(float(v), 6) for v in p] for p in points],
        "triangles": [
            {"vertices": [a, b, c], "flags": flags, "material": material, "area": area}
            for a, b, c, flags, material, area in records
        ],
        "cells": cells,
    }


def collision_document(collision: dict[str, Any]) -> gltf.Document:
    """The collision triangles as glTF: one primitive per material id, flat (no normals), positions as stored."""
    document = gltf.Document()
    positions = np.array(collision["vertices"], dtype=np.float32).reshape(-1, 3)
    by_material: dict[int, list[list[int]]] = {}
    for triangle in collision["triangles"]:
        by_material.setdefault(triangle["material"], []).append(triangle["vertices"])
    primitives = []
    position = document.accessor(positions, gltf.ARRAY_BUFFER, bounds=True) if len(positions) else None
    for material, corners in sorted(by_material.items()):
        # A colour of its own per material id, so a viewer tells the surfaces apart.
        colour = [((material * 97 + k * 61) % 200 + 55) / 255 for k in range(3)] + [1.0]
        pbr = {"baseColorFactor": colour, "metallicFactor": 0.0, "roughnessFactor": 1.0}
        value = {"name": f"material_{material}", "pbrMetallicRoughness": pbr, "doubleSided": True}
        index = document.add("materials", value)
        indices = np.array(corners, dtype=np.uint32).ravel()
        primitives.append(
            {
                "attributes": {"POSITION": position},
                "indices": document.accessor(indices, gltf.ELEMENT_ARRAY_BUFFER),
                "material": index,
                "mode": 4,
            }
        )
    if primitives:
        mesh = document.add("meshes", {"name": "collision", "primitives": primitives})
        document.json["scenes"][0]["nodes"] = [document.add("nodes", {"name": "collision", "mesh": mesh})]
    return document


def _edge_list(lists: bytes, start: int) -> list[int]:
    """One slab's edge numbers: `s16` values from `start` up to the next negative one."""
    result: list[int] = []
    at = start * 2
    while at + 2 <= len(lists):
        (value,) = struct.unpack_from("<h", lists, at)
        if value < 0:
            return result
        result.append(value)
        at += 2
    raise LevelError(f"an edge list at {start} has no end")


def decode_paths(data: bytes) -> dict[str, Any]:
    """The path data (chunk `0x40`, level-loading.md#path-data): areas of polygons, route nodes and edges."""
    paths, vertices, nodes, a_count, edges = struct.unpack_from("<IIhhI", data, 0)
    at = 0x20
    a_records = [struct.unpack_from("<II", data, at + 16 * i) for i in range(a_count)]
    at += 16 * a_count
    points = [struct.unpack_from("<2f", data, at + 16 * i) for i in range(vertices)]
    at += 16 * vertices
    path_at = at
    at += 0x50 * paths
    node_at = at
    at += 32 * nodes
    edge_at = at
    at += 8 * edges
    lists = data[at:]
    if at > len(data):
        raise LevelError(f"the path data's counts need {at} bytes, the chunk holds {len(data)}")
    polygons = []
    areas: list[list[int]] = []
    vertex = 0
    a_next = 0
    node_next = 0
    joins_next = False
    for index in range(paths):
        base = path_at + 0x50 * index
        count, extra = struct.unpack_from("<hh", data, base)
        box = struct.unpack_from("<4f", data, base + 8)
        (same_area,) = struct.unpack_from("<I", data, base + 0x20)
        slabs = struct.unpack_from("<16h", data, base + 0x28)
        flags, ground, has_nodes = struct.unpack_from("<HHI", data, base + 0x48)
        if vertex + count > vertices:
            raise LevelError(f"path {index} runs past the vertices")
        polygon: dict[str, Any] = {
            "points": [[round(x, 6), round(y, 6)] for x, y in points[vertex : vertex + count]],
            "box": {"x": [round(box[0], 6), round(box[1], 6)], "y": [round(box[2], 6), round(box[3], 6)]},
            "flags": flags,
            "ground": ground,
            "extra": extra,
            "slabs": [None if s < 0 else _edge_list(lists, s) for s in slabs],
        }
        vertex += count
        if has_nodes:
            if a_next >= a_count:
                raise LevelError(f"path {index} has route nodes but the A records ran out")
            node_count, stored = a_records[a_next]
            polygon["nodes"] = list(range(node_next, node_next + node_count))
            polygon["a_word"] = stored
            node_next += node_count
            a_next += 1
        if not joins_next:
            areas.append([])
        areas[-1].append(index)
        joins_next = same_area != 0
        polygons.append(polygon)
    route_nodes = []
    edge = 0
    for index in range(nodes):
        base = node_at + 32 * index
        x, y, z, w = struct.unpack_from("<4f", data, base)
        (count,) = struct.unpack_from("<h", data, base + 0x14)
        links = []
        for i in range(count):
            target, word = struct.unpack_from("<II", data, edge_at + 8 * (edge + i))
            links.append({"to": target, "flags": word & 0xFFFF, "avoid": bool(word >> 31)})
        edge += count
        route_nodes.append({"position": [round(x, 6), round(y, 6), round(z, 6), round(w, 6)], "edges": links})
    if edge != edges:
        raise LevelError(f"the route nodes hold {edge} edges, the header says {edges}")
    return {"areas": areas, "paths": polygons, "nodes": route_nodes}


def decode_occluders(data: bytes) -> list[dict[str, Any]]:
    """The occluders (chunk `0x53`): a count, padding to 16, then 0x70-byte records whose points are stored."""
    (count,) = struct.unpack_from("<I", data, 0)
    if 16 + 0x70 * count > len(data):
        raise LevelError(f"{count} occluders do not fit in {len(data)} bytes")
    result = []
    for i in range(count):
        base = 16 + 0x70 * i
        points = [[round(v, 6) for v in struct.unpack_from("<3f", data, base + 0x30 + 16 * p)] for p in range(4)]
        result.append({"points": points})
    return result


def decode_subtitles(data: bytes) -> dict[str, dict[str, list[dict[str, Any]]]]:
    """The caption text (chunk `0x51`, movies.md#caption-text): language -> scene -> captions (`kind` 2 emphasised,
    3 ordinary)."""
    (length,) = struct.unpack_from("<H", data, 0)
    result: dict[str, dict[str, list[dict[str, Any]]]] = {}
    language: dict[str, list[dict[str, Any]]] | None = None
    scene: list[dict[str, Any]] | None = None
    at = 2
    while at < length - 1 and at + 4 <= len(data):
        (kind,) = struct.unpack_from("<I", data, at)
        end = data.find(b"\0", at + 4)
        if end < 0:
            raise LevelError("a caption record has no end")
        text = data[at + 4 : end].decode("latin-1")
        at = end + 1
        if kind == 0:
            language = result.setdefault(text, {})
            scene = None
        elif kind == 1:
            if language is None:
                raise LevelError("a scene marker comes before any language")
            scene = language.setdefault(text, [])
        else:
            if scene is None:
                raise LevelError("a caption comes before any scene")
            scene.append({"kind": kind, "text": text})
    return result


@dataclass
class _Level:
    """A level kept for its models, which need every texture written first."""

    item: Item
    folder: str
    record: dict[str, Any]


class LevelsStage:
    """Decodes each level resource as it arrives; writes its models when the textures are known."""

    def __init__(self, output: Output, textures: Any | None = None) -> None:
        """Start the type; `textures` is this run's textures stage, when it runs."""
        self.output = output
        self.textures = textures
        self.report = Report(KIND)
        output.start(KIND)
        self.levels: list[_Level] = []

    def entry(self, entry: Entry) -> None:
        """Levels come as resources only."""

    def _part(self, folder: str, record: dict[str, Any], name: str, label: str, decode: Any) -> None:
        """Run one decoder; write its JSON, or report what went wrong."""
        try:
            value = decode()
        except (LevelError, struct.error, ValueError, IndexError) as error:
            self.report.problem(f"{label}: {name}: {error}")
            return
        record["files"][name] = self.output.write_json(KIND, f"{folder}/{name}.json", value)
        if name == "collision":
            stem = record["files"][name].removesuffix(".json")
            record["files"]["collision_mesh"] = write_document(self.output, KIND, stem, collision_document(value))

    def item(self, item: Item) -> None:
        """A level resource's collision, paths, occluders and subtitles, and the resource as stored."""
        if item.shape != SHAPE_LEVEL:
            return
        label = item.label()
        folder = f"levels/{label}"
        chunks = item.resource.chunks
        by_type: dict[int, list[int]] = {}
        for index, chunk in enumerate(chunks):
            by_type.setdefault(chunk.type, []).append(index)

        def one(kind: int) -> bytes:
            """The data of the level's only chunk of type `kind`."""
            found = by_type.get(kind, [])
            if len(found) != 1:
                raise LevelError(f"{len(found)} chunks of type 0x{kind:02x}")
            return item.chunk_bytes(found[0])

        record: dict[str, Any] = {
            "name": item.name,
            "hash": f"{item.resource.hash:08x}",
            "chunks": [{"type": f"0x{c.type:02x}", "name": chunk_names.name(c.type), "size": c.size} for c in chunks],
            "files": {},
        }
        if 0x17 in by_type:
            record["header"] = item.chunk_bytes(by_type[0x17][0]).hex()
        self._part(folder, record, "collision", label, lambda: decode_collision(one(3), one(7), one(4), one(5), one(6)))
        self._part(folder, record, "paths", label, lambda: decode_paths(one(0x40)))
        self._part(folder, record, "occluders", label, lambda: decode_occluders(one(0x53)))
        self._part(folder, record, "subtitles", label, lambda: decode_subtitles(one(0x51)))
        record["files"]["source"] = self.output.write(KIND, f"{folder}/source.lev", item.resource_bytes())
        self.levels.append(_Level(item, folder, record))
        self.report.count("levels")

    def _models(self, level: _Level, dictionaries: list[list[dict[str, Any]]]) -> None:
        """The three background models and the glow world, with their dictionaries' textures."""
        item = level.item
        chunks = item.resource.chunks
        models = [i for i, c in enumerate(chunks) if c.type == 0x47]
        worlds = [i for i, c in enumerate(chunks) if c.type == 0x15]
        prefix = "../../"
        for number, (name, index) in enumerate(zip(BACKGROUNDS, models, strict=False)):
            try:
                data = item.chunk_bytes(index)
                start = rw.find(data, rw.CLUMP)
                if start is None:
                    raise rw.RwError("no clump section")
                clump = rwclump.read_clump(data, start.start - rw.HEADER)
                listed = dictionaries[number] if number < len(dictionaries) else []
                # The game gives the first material the dictionary's first texture, whatever its name.
                document, _ = clump_document(clump, [first_texture(listed)], prefix)
            except (rw.RwError, ps2mesh.MeshError, struct.error, ValueError, IndexError, KeyError) as error:
                self.report.problem(f"{item.label()}: {name}: {error}")
                continue
            level.record["files"][name] = write_document(self.output, KIND, f"{level.folder}/{name}", document)
            self.report.count("background models")
        for index in worlds:
            try:
                data = item.chunk_bytes(index)
                start = rw.find(data, rw.WORLD)
                if start is None:
                    raise rw.RwError("no world section")
                world = rwclump.read_world(data, start.start - rw.HEADER)
                listed = dictionaries[3] if len(dictionaries) > 3 else []
                textures = [first_texture(listed, m.texture) if m.texture else None for m in world.materials]
                document, _ = world_document(world, textures, prefix)
            except (rw.RwError, ps2mesh.MeshError, struct.error, ValueError, IndexError, KeyError) as error:
                self.report.problem(f"{item.label()}: glows: {error}")
                continue
            level.record["files"]["glows"] = write_document(self.output, KIND, f"{level.folder}/glows", document)
            self.report.count("glow worlds")

    def finish(self) -> None:
        """Write the models and each level's `level.json`, then the index."""
        listing = texture_listing(self.output, self.textures, self.report)
        index = []
        for level in self.levels:
            self._models(level, listing.get(level.item.label(), []))
            path = self.output.write_json(KIND, f"{level.folder}/level.json", level.record)
            index.append({"name": level.item.name, "hash": level.record["hash"], "file": path})
        self.output.write_json(KIND, "levels/index.json", {"levels": index})
