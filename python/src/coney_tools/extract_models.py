# SPDX-License-Identifier: GPL-3.0-or-later
"""The `models` stage of `coney-tools extract`: every distinct model resource as glTF 2.0.

`models/<name>.gltf` and `.bin` for each clump (chunk `0x47`): one node per RenderWare frame, one mesh per atomic
with a primitive per material, and for a skinned character a glTF skin over its HAnim bones with the skin plugin's
inverse bind matrices. Positions keep RenderWare's axes and units (the packed integers times the atomic's position
scale); texture coordinates are scaled by the atomic's second scale; vertex colours are mapped from the PS2's 0-128
to 0-1. Materials are untextured in the files: a model takes the first texture of the dictionary the Object List or
the Character List gives it (else one named like it), referenced as `../textures/<dictionary>/<texture>.png`.
`models/index.json` lists every model with its dictionary, counts and bone offsets (chunk `0x28`).

Research: docs/research/formats/renderware.md#clump, docs/research/characters.md#character-geometry,
docs/research/level-loading.md#the-object-list
"""

from __future__ import annotations

import json
import struct
from dataclasses import dataclass
from typing import Any

import numpy as np
import numpy.typing as npt

from coney_tools import gltf, ps2mesh, rw, rwclump
from coney_tools.extract_output import Output, Report
from coney_tools.extract_wad import SHAPE_GLOBAL, SHAPE_MODEL, Entry, Item

KIND = "models"
_V4_16, _V2_16, _V3_32, _V2_32 = 0x0D, 0x05, 0x08, 0x04


@dataclass
class Primitive:
    """One mesh's vertex arrays and triangles, ready for glTF."""

    positions: npt.NDArray[np.float32]
    triangles: npt.NDArray[np.uint32]
    normals: npt.NDArray[np.float32] | None
    uv: list[npt.NDArray[np.float32]]
    colours: npt.NDArray[np.float32] | None
    joints: npt.NDArray[np.uint16] | None
    weights: npt.NDArray[np.float32] | None
    material: int


def build_primitive(record: rwclump.MeshRecord, strip: bool, position_scale: float, uv_scale: float) -> Primitive:
    """Decode one mesh's chain into indexed vertex arrays: identical vertices merged, degenerate triangles dropped."""
    mesh = ps2mesh.decode(record.chain, strip)
    slots = mesh.slots
    count = mesh.count
    if mesh.formats.get(0) == _V3_32:  # RenderWare's default layout: floats
        positions = slots[0].view(np.float32).reshape(count, 3).astype(np.float32)
        uv = [slots[1].view(np.float32).reshape(count, 2).astype(np.float32)] if 1 in slots else []
        normals = slots[3][:, :3].astype(np.float32) / 127.0 if 3 in slots else None
    else:  # the game's packed layout: 16-bit integers and scales
        positions = slots[0][:, :3].astype(np.float32) * np.float32(position_scale)
        uv = []
        if 1 in slots:
            raw = slots[1].astype(np.float32) * np.float32(uv_scale)
            uv = [raw[:, 0:2]] + ([raw[:, 2:4]] if raw.shape[1] == 4 else [])
        normals = slots[3][:, :3].astype(np.float32) / 127.0 if 3 in slots else None
    colours = None
    if 2 in slots:
        colours = np.minimum(slots[2].astype(np.float32) / 128.0, 1.0).astype(np.float32)
    joints = weights = None
    if 4 in slots:
        joints, weights = ps2mesh.skin_weights(slots[4])
        totals = weights.sum(axis=1, keepdims=True)
        weights = np.where(totals > 0, weights / np.where(totals > 0, totals, 1), weights).astype(np.float32)
    if normals is not None:
        lengths = np.linalg.norm(normals, axis=1, keepdims=True)
        normals = np.where(lengths > 0, normals / np.where(lengths > 0, lengths, 1), [0.0, 1.0, 0.0]).astype(np.float32)
    # Merge identical vertices so the triangles index shared ones.
    columns = [positions] + uv + [a for a in (normals, colours, weights) if a is not None]
    if joints is not None:
        columns.append(joints.astype(np.float32))
    table = np.ascontiguousarray(np.concatenate(columns, axis=1))
    rows = table.view(np.dtype((np.void, table.dtype.itemsize * table.shape[1]))).ravel()
    _, first, inverse = np.unique(rows, return_index=True, return_inverse=True)
    order = np.argsort(first)  # keep vertices in the order they first appear
    remap = np.empty_like(order)
    remap[order] = np.arange(len(order))
    keep = first[order]
    indices = remap[inverse.ravel()]
    corners = indices[ps2mesh.strip_triangles(count)] if strip else indices[: count - count % 3].reshape(-1, 3)
    good = (corners[:, 0] != corners[:, 1]) & (corners[:, 1] != corners[:, 2]) & (corners[:, 0] != corners[:, 2])
    return Primitive(
        positions[keep],
        corners[good].astype(np.uint32),
        normals[keep] if normals is not None else None,
        [u[keep] for u in uv],
        colours[keep] if colours is not None else None,
        joints[keep] if joints is not None else None,
        weights[keep] if weights is not None else None,
        record.material,
    )


def add_primitive(document: gltf.Document, primitive: Primitive, material: int | None) -> dict[str, Any]:
    """The glTF primitive for decoded arrays (the accessors go into `document`)."""
    attributes = {"POSITION": document.accessor(primitive.positions, gltf.ARRAY_BUFFER, bounds=True)}
    if primitive.normals is not None:
        attributes["NORMAL"] = document.accessor(primitive.normals, gltf.ARRAY_BUFFER)
    for index, uv in enumerate(primitive.uv):
        attributes[f"TEXCOORD_{index}"] = document.accessor(uv, gltf.ARRAY_BUFFER)
    if primitive.colours is not None:
        attributes["COLOR_0"] = document.accessor(primitive.colours, gltf.ARRAY_BUFFER)
    if primitive.joints is not None and primitive.weights is not None:
        attributes["JOINTS_0"] = document.accessor(primitive.joints, gltf.ARRAY_BUFFER)
        attributes["WEIGHTS_0"] = document.accessor(primitive.weights, gltf.ARRAY_BUFFER)
    result: dict[str, Any] = {
        "attributes": attributes,
        "indices": document.accessor(primitive.triangles.ravel(), gltf.ELEMENT_ARRAY_BUFFER),
        "mode": 4,
    }
    if material is not None:
        result["material"] = material
    return result


@dataclass
class TextureRef:
    """Where a texture went: its PNG path in the output, and its sampling."""

    path: str
    filter: int
    address_u: int
    address_v: int


def add_material(document: gltf.Document, material: rwclump.Material, texture: TextureRef | None, prefix: str) -> int:
    """A glTF material: the RenderWare colour as the base colour factor, the texture when there is one."""
    colour = [c / 255.0 for c in material.colour]
    pbr: dict[str, Any] = {"baseColorFactor": colour, "metallicFactor": 0.0, "roughnessFactor": 1.0}
    value: dict[str, Any] = {"pbrMetallicRoughness": pbr}
    if texture is not None:
        sampler = document.sampler(texture.filter, texture.address_u, texture.address_v)
        pbr["baseColorTexture"] = {"index": document.texture(prefix + texture.path, sampler)}
        value["alphaMode"] = "MASK"
        value["alphaCutoff"] = 0.5
        value["name"] = texture.path.rsplit("/", 1)[-1].removesuffix(".png")
    if material.matfx is not None:
        value["extras"] = {"matfx": material.matfx}
    return document.add("materials", value)


def clump_document(
    clump: rwclump.Clump, textures: list[TextureRef | None], prefix: str
) -> tuple[gltf.Document, dict[str, int]]:
    """A glTF document for a clump; `textures[i]` is geometry material i's texture (by material index of the first
    geometry, then the fallback). Returns the document and its counts."""
    document = gltf.Document()
    nodes: list[dict[str, Any]] = []
    for index, frame in enumerate(clump.frames):
        node: dict[str, Any] = {"name": frame.name or f"frame_{index}"}
        identity = (frame.right, frame.up, frame.at, frame.position) == ((1, 0, 0), (0, 1, 0), (0, 0, 1), (0, 0, 0))
        if not identity:
            node["matrix"] = gltf.matrix(frame.right, frame.up, frame.at, frame.position)
        if frame.hanim_id >= 0:
            node["extras"] = {"hanim_id": frame.hanim_id}
        nodes.append(node)
    for index, frame in enumerate(clump.frames):
        if frame.parent >= 0:
            nodes[frame.parent].setdefault("children", []).append(index)
    roots = [i for i, f in enumerate(clump.frames) if f.parent < 0]
    counts = {"vertices": 0, "triangles": 0, "meshes": 0}
    skin_index = None
    if clump.hierarchy and any(g.skin is not None for g in clump.geometries):
        by_id = {f.hanim_id: i for i, f in enumerate(clump.frames) if f.hanim_id >= 0}
        joints = [by_id[node.id] for node in clump.hierarchy if node.id in by_id]
        geometry_skin = next((g.skin for g in clump.geometries if g.skin is not None), None)
        skin: dict[str, Any] = {"joints": joints}
        if geometry_skin is not None and len(geometry_skin.inverse_bind) == len(joints):
            fixed = []
            for m in geometry_skin.inverse_bind:
                row = list(m)
                row[3] = row[7] = row[11] = 0.0
                row[15] = 1.0
                fixed.append(row)
            skin["inverseBindMatrices"] = document.accessor(np.array(fixed, dtype=np.float32))
        if roots:
            skin["skeleton"] = roots[0]
        skin_index = document.add("skins", skin)
    material_cache: dict[tuple[int, int], int] = {}
    extra_nodes = []
    for atomic in clump.atomics:
        geometry = clump.geometries[atomic.geometry]
        primitives = []
        for record in geometry.meshes:
            primitive = build_primitive(record, geometry.strip, atomic.position_scale, atomic.texture_scale)
            key = (atomic.geometry, record.material)
            if key not in material_cache:
                texture = textures[record.material] if record.material < len(textures) else None
                material_cache[key] = add_material(document, geometry.materials[record.material], texture, prefix)
            primitives.append(add_primitive(document, primitive, material_cache[key]))
            counts["vertices"] += len(primitive.positions)
            counts["triangles"] += len(primitive.triangles)
        mesh = document.add("meshes", {"primitives": primitives})
        counts["meshes"] += 1
        if geometry.skin is not None and skin_index is not None:
            # A skinned mesh's node transform is ignored by glTF; give it its own node at the scene root.
            extra_nodes.append({"name": f"skinned_mesh_{mesh}", "mesh": mesh, "skin": skin_index})
        else:
            target = nodes[atomic.frame]
            if "mesh" in target:
                child = len(nodes) + len(extra_nodes)
                extra_nodes.append({"name": f"atomic_{mesh}", "mesh": mesh})
                target.setdefault("children", []).append(child)
            else:
                target["mesh"] = mesh
    for node in nodes:
        document.add("nodes", node)
    first_extra = len(nodes)
    for offset, node in enumerate(extra_nodes):
        document.add("nodes", node)
        if "skin" in node:
            roots.append(first_extra + offset)
    document.json["scenes"][0]["nodes"] = roots
    return document, counts


def world_document(
    world: rwclump.World,
    textures: list[TextureRef | None],
    prefix: str,
    position_scale: float = 1.0,
    texture_scale: float = 1.0,
) -> tuple[gltf.Document, dict[str, int]]:
    """A glTF document for a RenderWare world: one node and mesh per atomic sector (its streamed-sector index, part
    and origin in the node's extras), a primitive per material; `textures[i]` is world material i's texture. Sector
    vertices are in world coordinates, so the nodes carry no transform."""
    document = gltf.Document()
    counts = {"vertices": 0, "triangles": 0, "meshes": 0}
    materials: dict[int, int] = {}
    roots = []
    for number, sector in enumerate(world.sectors):
        primitives = []
        for record in sector.meshes:
            primitive = build_primitive(record, sector.strip, position_scale, texture_scale)
            if len(primitive.triangles) == 0:
                continue
            material = record.material
            if material not in materials and material < len(world.materials):
                texture = textures[material] if material < len(textures) else None
                materials[material] = add_material(document, world.materials[material], texture, prefix)
            primitives.append(add_primitive(document, primitive, materials.get(material)))
            counts["vertices"] += len(primitive.positions)
            counts["triangles"] += len(primitive.triangles)
        node: dict[str, Any] = {"name": f"sector_{number}"}
        if primitives:
            node["mesh"] = document.add("meshes", {"primitives": primitives})
            counts["meshes"] += 1
        if sector.streamed_index is not None:
            node["extras"] = {
                "streamed_index": sector.streamed_index,
                "part": sector.part,
                "origin": list(sector.origin),
            }
        roots.append(document.add("nodes", node))
    document.json["scenes"][0]["nodes"] = roots
    return document, counts


def write_document(output: Output, kind: str, stem: str, document: gltf.Document) -> str:
    """Write `<stem>.gltf` and its `.bin` (both claimed together, so a clash renames both); returns the glTF path."""
    bin_path = output.claim(kind, f"{stem}.bin")
    gltf_path = bin_path.removesuffix(".bin") + ".gltf"
    document_bytes = document.dumps(bin_path.rsplit("/", 1)[-1])
    output.write_claimed(kind, bin_path, bytes(document.buffer))
    return output.write(kind, gltf_path, document_bytes)


def _object_list(data: bytes) -> dict[int, int]:
    """Model hash -> texture dictionary hash, from the Object List (chunk `0x46`: 16-byte header, 36-byte records)."""
    (count,) = struct.unpack_from("<I", data, 0)
    result: dict[int, int] = {}
    for i in range(count):
        _, _, model, textures = struct.unpack_from("<IIII", data, 16 + 36 * i)
        result.setdefault(model, textures)
    return result


def _character_list(data: bytes) -> dict[int, int]:
    """Model hash -> texture dictionary hash, from the Character List (chunk `0x44`: 16-byte header, 32-byte
    records)."""
    (count,) = struct.unpack_from("<I", data, 0)
    result: dict[int, int] = {}
    for i in range(count):
        _, _, model, textures = struct.unpack_from("<IIII", data, 16 + 32 * i)
        result.setdefault(model, textures)
    return result


def texture_listing(output: Output, textures: Any | None, report: Report) -> dict[str, list[list[dict[str, Any]]]]:
    """Dictionary source label -> the texture lists of its dictionaries, in the order they were found: from the
    textures stage of this run (`textures`), else from the index an earlier run wrote."""
    if textures is not None:
        listing = [{"source": d.source, "textures": d.textures} for d in textures.dictionaries]
    else:
        path = output.root / "textures" / "index.json"
        if not path.is_file():
            report.problem("no textures index: run the textures type first; models are written untextured")
            return {}
        listing = json.loads(path.read_text(encoding="utf-8"))["dictionaries"]
    result: dict[str, list[list[dict[str, Any]]]] = {}
    for dictionary in listing:
        result.setdefault(dictionary["source"], []).append(dictionary["textures"])
    return result


def first_texture(listed: list[dict[str, Any]], name: str | None = None) -> TextureRef | None:
    """The texture called `name` in a dictionary's list, else its first texture; None when it has none written."""
    chosen = next((t for t in listed if t["name"] == name and t.get("file")), None) if name else None
    chosen = chosen or next((t for t in listed if t.get("file")), None)
    if chosen is None:
        return None
    return TextureRef(chosen["file"], chosen["filter"], chosen["address_u"], chosen["address_v"])


@dataclass
class _Model:
    """A model kept until the end of the pass, when every dictionary's place is known."""

    item: Item
    clump_chunk: bytes
    bone_offsets: bytes | None


class ModelsStage:
    """Collects the model resources during the WAD pass and writes them as glTF at its end."""

    def __init__(self, output: Output, textures: Any | None = None) -> None:
        """Start the type. `textures` is the textures stage of the same run, when there is one; otherwise the
        textures index an earlier run wrote is read."""
        self.output = output
        self.report = Report(KIND)
        output.start(KIND)
        self.textures = textures
        self.models: list[_Model] = []
        self.dictionary_of: dict[int, int] = {}
        self.resource_labels: dict[int, str] = {}

    def entry(self, entry: Entry) -> None:
        """Models come as resources only."""

    def item(self, item: Item) -> None:
        """Keep a model; read the Object and Character Lists from the global resource."""
        self.resource_labels.setdefault(item.resource.hash, item.label())
        if item.shape == SHAPE_GLOBAL:
            for index, chunk in enumerate(item.resource.chunks):
                if chunk.type == 0x46:
                    self.dictionary_of.update(_object_list(item.chunk_bytes(index)))
                elif chunk.type == 0x44:
                    for model, textures in _character_list(item.chunk_bytes(index)).items():
                        self.dictionary_of.setdefault(model, textures)
        if item.shape != SHAPE_MODEL:
            return
        clump = next((i for i, c in enumerate(item.resource.chunks) if c.type == 0x47), None)
        offsets = next((i for i, c in enumerate(item.resource.chunks) if c.type == 0x28), None)
        if clump is None:
            return
        self.models.append(
            _Model(item, item.chunk_bytes(clump), item.chunk_bytes(offsets) if offsets is not None else None)
        )

    def _texture_index(self) -> dict[str, list[dict[str, Any]]]:
        """Dictionary source label -> the textures of its first dictionary."""
        return {source: lists[0] for source, lists in texture_listing(self.output, self.textures, self.report).items()}

    def finish(self) -> None:
        """Write every model and the index."""
        dictionaries = self._texture_index()
        index = []
        for model in self.models:
            item = model.item
            label = item.label()
            dictionary_hash = self.dictionary_of.get(item.resource.hash)
            dictionary_label = self.resource_labels.get(dictionary_hash) if dictionary_hash is not None else None
            if dictionary_label is None and item.name and item.name.endswith("_geo"):
                dictionary_label = item.name[:-4] + "_tex"
                if dictionary_label not in dictionaries:
                    dictionary_label = None
            listed = dictionaries.get(dictionary_label or "", [])
            record: dict[str, Any] = {
                "name": item.name,
                "hash": f"{item.resource.hash:08x}",
                "dictionary": dictionary_label,
            }
            try:
                start = rw.find(model.clump_chunk, rw.CLUMP)
                if start is None:
                    raise rw.RwError("no clump section")
                clump = rwclump.read_clump(model.clump_chunk, start.start - rw.HEADER)
                materials = clump.geometries[0].materials if clump.geometries else []
                textures = [first_texture(listed, m.texture) for m in materials]
                document, counts = clump_document(clump, textures, "../")
            except (rw.RwError, ps2mesh.MeshError, struct.error, ValueError, IndexError, KeyError) as error:
                self.report.problem(f"{label}: {error}")
                continue
            if model.bone_offsets is not None:
                floats = struct.unpack_from(f"<{len(model.bone_offsets) // 4}f", model.bone_offsets)
                document.json["extras"] = {"bone_offsets": [list(floats[i : i + 4]) for i in range(0, len(floats), 4)]}
            skinned = any(g.skin is not None for g in clump.geometries)
            record["file"] = write_document(self.output, KIND, f"models/{label}", document)
            record.update(counts)
            record["skinned"] = skinned
            record["atomics"] = len(clump.atomics)
            index.append(record)
            self.report.count("skinned models" if skinned else "models")
        self.output.write_json(KIND, "models/index.json", {"models": index})
