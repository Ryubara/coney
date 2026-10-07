# SPDX-License-Identifier: GPL-3.0-or-later
"""RenderWare clumps and atomics with PS2 native geometry: frames, HAnim, materials, meshes, skins.

A clump (section `0x10`) holds a frame list, a geometry list and its atomics; each atomic joins a frame and a
geometry. The geometry is PS2 native: no vertex arrays in the stream, but the mesh plugin (`0x50E`, each mesh's
vertex count and material) and the native data (`0x510`, each mesh's DMA chain, decoded by `ps2mesh`). This module
reads the sections into plain records; it does not decode the chains.

Research: docs/research/formats/renderware.md#clump
"""

from __future__ import annotations

import struct
from dataclasses import dataclass, field

from coney_tools import rw

_PS2 = 4  # platform id of native geometry and skins
ATOMIC_PLUGIN = 0x3F0  # the game's atomic plugin: position scale, texture-coordinate scale, a word
MATFX = 0x120


@dataclass
class Frame:
    """One frame: its matrix relative to its parent (right, up, at, position) and its HAnim id (or -1)."""

    right: tuple[float, float, float]
    up: tuple[float, float, float]
    at: tuple[float, float, float]
    position: tuple[float, float, float]
    parent: int
    name: str | None = None
    hanim_id: int = -1


@dataclass
class HAnimNode:
    """One node of an HAnim hierarchy: its bone id, its index and its flags (push 2, pop 1)."""

    id: int
    index: int
    flags: int


@dataclass
class Material:
    """A material: colour (RGBA bytes), its texture's name (None when untextured) and the texture's sampling word."""

    colour: tuple[int, int, int, int]
    texture: str | None
    filter_addressing: int
    ambient: float
    diffuse: float
    matfx: int | None = None  # MatFX effect type, when the material has one


@dataclass
class MeshRecord:
    """A mesh: its vertex count (strip or list), its material and its DMA chain."""

    count: int
    material: int
    chain: bytes


@dataclass
class Skin:
    """A PS2 native skin: bone count, the bones used, the most weights a vertex has, the inverse bind matrices
    (16 floats each, column-major right, up, at, position)."""

    bones: int
    used: list[int]
    max_weights: int
    inverse_bind: list[tuple[float, ...]]


@dataclass
class Geometry:
    """A geometry: format flags, texture-coordinate sets, counts, bounding sphere, materials, meshes and skin."""

    flags: int
    texture_sets: int
    triangles: int
    vertices: int
    sphere: tuple[float, float, float, float]
    materials: list[Material]
    strip: bool
    meshes: list[MeshRecord]
    skin: Skin | None
    extensions: list[int] = field(default_factory=list)


@dataclass
class Atomic:
    """An atomic: its frame and geometry, its flags, and the game plugin's two scales and word."""

    frame: int
    geometry: int
    flags: int
    position_scale: float = 1.0
    texture_scale: float = 1.0
    word: int = 0
    extensions: list[int] = field(default_factory=list)


@dataclass
class Clump:
    """A clump: frames, the HAnim hierarchy (when the clump is skinned), geometries and atomics."""

    frames: list[Frame]
    hierarchy: list[HAnimNode]
    geometries: list[Geometry]
    atomics: list[Atomic]


def _floats(data: bytes, at: int, count: int) -> tuple[float, ...]:
    """`count` little-endian floats."""
    return struct.unpack_from(f"<{count}f", data, at)


def _frame_list(data: bytes, section: rw.Section) -> tuple[list[Frame], list[HAnimNode]]:
    """The frame records, then one extension per frame (HAnim and the frame name plugin)."""
    parts = list(rw.children(data, section))
    head = parts[0]
    (count,) = struct.unpack_from("<I", data, head.start)
    frames = []
    for i in range(count):
        at = head.start + 4 + 56 * i
        values = _floats(data, at, 12)
        (parent,) = struct.unpack_from("<i", data, at + 48)
        frames.append(Frame(values[0:3], values[3:6], values[6:9], values[9:12], parent))  # type: ignore[arg-type]
    hierarchy: list[HAnimNode] = []
    extensions = [p for p in parts[1:] if p.type == rw.EXTENSION]
    for frame, extension in zip(frames, extensions, strict=False):
        for plugin in rw.children(data, extension):
            if plugin.type == rw.HANIM:
                _, bone_id, nodes = struct.unpack_from("<Iii", data, plugin.start)
                frame.hanim_id = bone_id
                for n in range(max(nodes, 0)):
                    node = struct.unpack_from("<iiI", data, plugin.start + 20 + 12 * n)
                    hierarchy.append(HAnimNode(*node))
            elif plugin.type == rw.FRAME_NAME:
                frame.name = bytes(data[plugin.start : plugin.end]).split(b"\0", 1)[0].decode("latin-1")
    return frames, hierarchy


def _material(data: bytes, section: rw.Section) -> Material:
    """A material's colour, lighting, texture and MatFX effect."""
    parts = list(rw.children(data, section))
    head = parts[0]
    colour = tuple(data[head.start + 4 : head.start + 8])
    (textured,) = struct.unpack_from("<I", data, head.start + 12)
    ambient = diffuse = 1.0
    if head.size >= 28:
        ambient, _, diffuse = _floats(data, head.start + 16, 3)
    texture = None
    sampling = 0
    rest = parts[1:]
    if textured and rest and rest[0].type == rw.TEXTURE:
        inner = list(rw.children(data, rest[0]))
        (sampling,) = struct.unpack_from("<I", data, inner[0].start)
        texture = rw.string(data, inner[1])
    matfx = None
    for extension in (p for p in rest if p.type == rw.EXTENSION):
        for plugin in rw.children(data, extension):
            if plugin.type == MATFX and plugin.size >= 4:
                (matfx,) = struct.unpack_from("<I", data, plugin.start)
    return Material(colour, texture, sampling, ambient, diffuse, matfx)  # type: ignore[arg-type]


def _skin(data: bytes, plugin: rw.Section) -> Skin | None:
    """A PS2 native skin (a struct inside the plugin), or None for another kind."""
    head = rw.section_at(data, plugin.start)
    if head.type != rw.STRUCT:
        return None
    (platform,) = struct.unpack_from("<I", data, head.start)
    if platform != _PS2:
        return None
    bones, used, max_weights = data[head.start + 4], data[head.start + 5], data[head.start + 6]
    used_bones = list(data[head.start + 8 : head.start + 8 + used])
    matrices = []
    at = head.start + 8 + used
    for _ in range(bones):
        matrices.append(_floats(data, at, 16))
        at += 64
    return Skin(bones, used_bones, max_weights, matrices)


def _geometry(data: bytes, section: rw.Section) -> Geometry:
    """A geometry's struct, material list and extensions (mesh, native data, skin)."""
    parts = list(rw.children(data, section))
    head = parts[0]
    flags, triangles, vertices, _ = struct.unpack_from("<IiiI", data, head.start)
    sphere = _floats(data, head.start + 16, 4) if head.size >= 32 else (0.0, 0.0, 0.0, 0.0)
    materials = []
    material_list = rw.need(rw.child(data, section, rw.MATERIAL_LIST), "material list")
    for part in rw.children(data, material_list):
        if part.type == rw.MATERIAL:
            materials.append(_material(data, part))
    strip = False
    counts: list[tuple[int, int]] = []
    chains: list[bytes] = []
    skin = None
    extension_ids = []
    extension = rw.child(data, section, rw.EXTENSION)
    for plugin in rw.children(data, extension) if extension else ():
        extension_ids.append(plugin.type)
        if plugin.type == 0x50E:
            mode, count, _ = struct.unpack_from("<III", data, plugin.start)
            strip = mode == 1
            counts = [struct.unpack_from("<II", data, plugin.start + 12 + 8 * i) for i in range(count)]
        elif plugin.type == rw.NATIVE_DATA:
            # The struct header's size is larger than the section on this disc; skip it unread, as RenderWare does.
            at = plugin.start + rw.HEADER
            (platform,) = struct.unpack_from("<I", data, at)
            if platform != _PS2:
                raise rw.RwError("native data is not for the PS2")
            at += 4
            while at + 8 <= plugin.end:
                (size,) = struct.unpack_from("<I", data, at)
                chains.append(bytes(data[at + 8 : at + 8 + size]))
                at += 8 + size
        elif plugin.type == rw.SKIN:
            skin = _skin(data, plugin)
    if len(chains) != len(counts):
        raise rw.RwError(f"{len(counts)} meshes but {len(chains)} DMA chains")
    meshes = [MeshRecord(c, m, chain) for (c, m), chain in zip(counts, chains, strict=True)]
    texture_sets = (flags >> 16) & 0xFF or (2 if flags & 0x80 else 1 if flags & 0x04 else 0)
    return Geometry(flags, texture_sets, triangles, vertices, sphere, materials, strip, meshes, skin, extension_ids)  # type: ignore[arg-type]


def read_atomic(data: bytes, section: rw.Section) -> Atomic:
    """An atomic's struct and the game's plugin `0x3F0`."""
    head = rw.need(rw.child(data, section, rw.STRUCT), "atomic struct")
    frame, geometry, flags = struct.unpack_from("<iiI", data, head.start)
    atomic = Atomic(frame, geometry, flags)
    extension = rw.child(data, section, rw.EXTENSION)
    for plugin in rw.children(data, extension) if extension else ():
        atomic.extensions.append(plugin.type)
        if plugin.type == ATOMIC_PLUGIN and plugin.size >= 12:
            atomic.position_scale, atomic.texture_scale = _floats(data, plugin.start, 2)
            (atomic.word,) = struct.unpack_from("<I", data, plugin.start + 8)
    return atomic


def read_geometry_of_atomic(data: bytes, section: rw.Section) -> Geometry:
    """The geometry inside a standalone atomic (the streamed world's parts carry one each)."""
    return _geometry(data, rw.need(rw.child(data, section, rw.GEOMETRY), "geometry"))


def read_clump(data: bytes, start: int) -> Clump:
    """Read the clump section whose header is at `start`."""
    clump = rw.section_at(data, start)
    if clump.type != rw.CLUMP:
        raise rw.RwError(f"section 0x{clump.type:x} is not a clump")
    frames, hierarchy = _frame_list(data, rw.need(rw.child(data, clump, rw.FRAME_LIST), "frame list"))
    geometry_list = rw.need(rw.child(data, clump, rw.GEOMETRY_LIST), "geometry list")
    geometries = [_geometry(data, g) for g in rw.children(data, geometry_list) if g.type == rw.GEOMETRY]
    atomics = [read_atomic(data, a) for a in rw.children(data, clump) if a.type == rw.ATOMIC]
    return Clump(frames, hierarchy, geometries, atomics)


@dataclass
class Sector:
    """An atomic sector of a world: its bounding box, meshes (PS2 native) and the game's sector plugin (`0x3F1`:
    streamed-sector index, part number, the origin its streamed atomic is placed at), when present."""

    box_min: tuple[float, float, float]
    box_max: tuple[float, float, float]
    material_base: int
    strip: bool
    meshes: list[MeshRecord]
    streamed_index: int | None = None
    part: int = 0
    origin: tuple[float, float, float] = (0.0, 0.0, 0.0)


@dataclass
class World:
    """A RenderWare world: its format flags, bounding box, materials and atomic sectors in stream order."""

    flags: int
    box_min: tuple[float, float, float]
    box_max: tuple[float, float, float]
    materials: list[Material]
    sectors: list[Sector]


_ATOMIC_SECTOR = 0x09
_PLANE_SECTOR = 0x0A
SECTOR_PLUGIN = 0x3F1


def _meshes(data: bytes, extension: rw.Section | None) -> tuple[bool, list[MeshRecord], dict[int, rw.Section]]:
    """The mesh plugin and native data of a geometry or sector extension, and its other plugins by id."""
    strip = False
    counts: list[tuple[int, int]] = []
    chains: list[bytes] = []
    plugins: dict[int, rw.Section] = {}
    for plugin in rw.children(data, extension) if extension else ():
        plugins[plugin.type] = plugin
        if plugin.type == 0x50E:
            mode, count, _ = struct.unpack_from("<III", data, plugin.start)
            strip = mode == 1
            counts = [struct.unpack_from("<II", data, plugin.start + 12 + 8 * i) for i in range(count)]
        elif plugin.type == rw.NATIVE_DATA:
            at = plugin.start + rw.HEADER  # the struct header's size is not to be trusted (see _geometry)
            (platform,) = struct.unpack_from("<I", data, at)
            if platform != _PS2:
                raise rw.RwError("native data is not for the PS2")
            at += 4
            while at + 8 <= plugin.end:
                (size,) = struct.unpack_from("<I", data, at)
                chains.append(bytes(data[at + 8 : at + 8 + size]))
                at += 8 + size
    if len(chains) != len(counts):
        raise rw.RwError(f"{len(counts)} meshes but {len(chains)} DMA chains")
    return strip, [MeshRecord(c, m, chain) for (c, m), chain in zip(counts, chains, strict=True)], plugins


def _sectors(data: bytes, section: rw.Section, out: list[Sector]) -> None:
    """Walk a sector tree in stream order, collecting the atomic sectors."""
    if section.type == _PLANE_SECTOR:
        for child in rw.children(data, section):
            if child.type in (_PLANE_SECTOR, _ATOMIC_SECTOR):
                _sectors(data, child, out)
        return
    head = rw.need(rw.child(data, section, rw.STRUCT), "sector struct")
    base, _, _ = struct.unpack_from("<Iii", data, head.start)
    box_max = _floats(data, head.start + 12, 3)
    box_min = _floats(data, head.start + 24, 3)
    strip, meshes, plugins = _meshes(data, rw.child(data, section, rw.EXTENSION))
    sector = Sector(box_min, box_max, base, strip, meshes)  # type: ignore[arg-type]
    plugin = plugins.get(SECTOR_PLUGIN)
    if plugin is not None and plugin.size >= 20:
        index, part = struct.unpack_from("<iI", data, plugin.start)
        sector.streamed_index = None if index < 0 else index
        sector.part = part
        sector.origin = _floats(data, plugin.start + 8, 3)  # type: ignore[assignment]
    out.append(sector)


def read_world(data: bytes, start: int) -> World:
    """Read the world section (`0x0B`) whose header is at `start`."""
    world = rw.section_at(data, start)
    if world.type != rw.WORLD:
        raise rw.RwError(f"section 0x{world.type:x} is not a world")
    head = rw.need(rw.child(data, world, rw.STRUCT), "world struct")
    (flags,) = struct.unpack_from("<I", data, head.start + 36)
    box_max = _floats(data, head.start + 40, 3)
    box_min = _floats(data, head.start + 52, 3)
    material_list = rw.need(rw.child(data, world, rw.MATERIAL_LIST), "material list")
    materials = [_material(data, m) for m in rw.children(data, material_list) if m.type == rw.MATERIAL]
    sectors: list[Sector] = []
    for child in rw.children(data, world):
        if child.type in (_PLANE_SECTOR, _ATOMIC_SECTOR):
            _sectors(data, child, sectors)
    return World(flags, box_min, box_max, materials, sectors)  # type: ignore[arg-type]
