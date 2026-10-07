# SPDX-License-Identifier: GPL-3.0-or-later
"""PS2 native geometry: the DMA chain of each mesh, walked and unpacked into vertex arrays.

A mesh of PS2 native geometry is a DMA chain for vector unit 1: tags whose upper words carry VIF commands, and data.
The vertices come in batches: `STCYCL n, 1`, one `UNPACK` per attribute to vector-unit slots 0, 1, ..., `ITOP` (the
batch's vertex count; an `UNPACK` may carry a few more vectors as padding) and a microprogram start. Two layouts occur:

* the game's **packed** layout (streamed world, props, characters): slot 0 positions `V4_16`, slot 1 texture
  coordinates `V4_16` (two sets) or `V2_16` (one), slot 2 colours `V4_8` unsigned, slot 3 normals `V4_8`, and for
  skinned geometry slot 4 bone weights `V4_32`;
* RenderWare's **default** layout (the level file's light glows): positions `V3_32`, texture coordinates `V2_32`,
  colours `V4_8`, normals `V3_8`.

In a triangle strip every batch after the first repeats the last two vertices of the one before; joining drops them.

Research: docs/research/world.md#ps2-world-geometry, docs/research/characters.md#character-geometry,
docs/research/formats/renderware.md#native-geometry
"""

from __future__ import annotations

import struct
from dataclasses import dataclass, field

import numpy as np
import numpy.typing as npt

# DMA tag ids and VIF commands, as the PS2 defines them.
_TAG_REFE, _TAG_CNT, _TAG_REF, _TAG_REFS, _TAG_RET, _TAG_END = 0, 1, 3, 4, 6, 7
_STCYCL, _ITOP, _STMOD, _STMASK, _STROW, _STCOL, _MPG, _DIRECT, _DIRECTHL = (
    0x01,
    0x04,
    0x05,
    0x20,
    0x30,
    0x31,
    0x4A,
    0x50,
    0x51,
)
_MSCAL, _MSCALF, _MSCNT = 0x14, 0x15, 0x17
_NO_DATA = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x10, 0x11, 0x13, 0x14, 0x15, 0x17}

# UNPACK formats (vn << 2 | vl): numpy element type and components.
_FORMATS: dict[int, tuple[str, int]] = {
    0x00: ("<i4", 1),
    0x01: ("<i2", 1),
    0x02: ("i1", 1),
    0x04: ("<i4", 2),
    0x05: ("<i2", 2),
    0x06: ("i1", 2),
    0x08: ("<i4", 3),
    0x09: ("<i2", 3),
    0x0A: ("i1", 3),
    0x0C: ("<i4", 4),
    0x0D: ("<i2", 4),
    0x0E: ("i1", 4),
}


class MeshError(ValueError):
    """A DMA chain or VIF stream this decoder cannot read."""


@dataclass
class Mesh:
    """A decoded mesh: one array per vector-unit slot, every batch joined, and the slots' UNPACK formats."""

    slots: dict[int, npt.NDArray[np.generic]] = field(default_factory=dict)
    formats: dict[int, int] = field(default_factory=dict)
    unsigned: dict[int, bool] = field(default_factory=dict)
    batches: int = 0

    @property
    def count(self) -> int:
        """Vertices in the mesh."""
        return len(self.slots[0]) if 0 in self.slots else 0


def gather_vif(chain: bytes) -> bytes:
    """The VIF stream a DMA chain sends: for each tag its two VIF words, then its data (inline after `cnt` and `ret`,
    at the tag's address, in 16-byte units from the chain's start, for `ref`). Stops after `ret`, `end` or `refe`."""
    stream = bytearray()
    at = 0
    while True:
        if at + 16 > len(chain):
            raise MeshError(f"DMA chain ends at {at:#x} without a ret or end tag")
        tag, address = struct.unpack_from("<II", chain, at)
        size = (tag & 0xFFFF) * 16
        kind = (tag >> 28) & 7
        stream += chain[at + 8 : at + 16]
        start = address * 16 if kind in (_TAG_REF, _TAG_REFS, _TAG_REFE) else at + 16
        if kind not in (_TAG_REF, _TAG_REFS, _TAG_REFE, _TAG_CNT, _TAG_RET, _TAG_END):
            raise MeshError(f"DMA tag {tag:#010x} at {at:#x}: id {kind} is not supported")
        if start + size > len(chain):
            raise MeshError(f"DMA tag at {at:#x}: {size} bytes at {start:#x} run past the chain")
        stream += chain[start : start + size]
        if kind in (_TAG_RET, _TAG_END, _TAG_REFE):
            return bytes(stream)
        at = at + 16 + size if kind == _TAG_CNT else at + 16


def _unpack_size(format_: int, count: int) -> int:
    """Bytes of an UNPACK's data: `count` vectors, padded to a whole word."""
    components = ((format_ >> 2) & 3) + 1
    bits = 16 if format_ & 3 == 3 else (32 >> (format_ & 3)) * components
    return (count * bits + 31) // 32 * 4


def decode(chain: bytes, strip: bool) -> Mesh:
    """Decode one mesh's DMA chain into per-slot vertex arrays (shape (vertices, components))."""
    stream = gather_vif(chain)
    mesh = Mesh()
    batch: dict[int, npt.NDArray[np.generic]] = {}
    itop: int | None = None
    at = 0
    parts: dict[int, list[npt.NDArray[np.generic]]] = {}
    while at + 4 <= len(stream):
        (code,) = struct.unpack_from("<I", stream, at)
        at += 4
        command = (code >> 24) & 0x7F
        number = (code >> 16) & 0xFF
        immediate = code & 0xFFFF
        if command >= 0x60:
            format_ = command & 0x0F
            if command & 0x10 or format_ not in _FORMATS:
                raise MeshError(f"UNPACK {code:#010x}: masked or of an unsupported format")
            count = number or 256
            size = _unpack_size(format_, count)
            if at + size > len(stream):
                raise MeshError(f"UNPACK {code:#010x}: its data is cut off")
            dtype, components = _FORMATS[format_]
            unsigned = bool(immediate & 0x4000)
            if unsigned:
                dtype = dtype.replace("i", "u")
            slot = immediate & 0x3FF
            values = np.frombuffer(stream, dtype=dtype, count=count * components, offset=at).reshape(count, components)
            batch[slot] = values
            mesh.formats[slot] = format_
            mesh.unsigned[slot] = unsigned
            at += size
            continue
        if command == _ITOP:
            itop = immediate & 0x3FF
        elif command == _STMOD and immediate & 3:
            raise MeshError("offset or difference unpacking is not supported")
        elif command in (_MSCAL, _MSCALF, _MSCNT):
            if 0 not in batch or itop is None:
                raise MeshError(f"batch {mesh.batches} has no positions or no vertex count")
            skip = 2 if strip and mesh.batches > 0 else 0
            for slot, values in batch.items():
                if len(values) < itop:
                    raise MeshError(f"batch {mesh.batches}: slot {slot} holds {len(values)} of {itop} vertices")
                parts.setdefault(slot, []).append(values[skip:itop])
            mesh.batches += 1
            batch = {}
            itop = None
        elif command == _STMASK:
            at += 4
        elif command in (_STROW, _STCOL):
            at += 16
        elif command == _MPG:
            at += (number or 256) * 8
        elif command in (_DIRECT, _DIRECTHL):
            at += (immediate or 65536) * 16
        elif command not in _NO_DATA:
            raise MeshError(f"VIF command {code:#010x} is not supported")
    if batch or mesh.batches == 0:
        raise MeshError("the chain ends with vertices no microprogram call draws")
    mesh.slots = {slot: np.concatenate(values) for slot, values in parts.items()}
    lengths = {len(v) for v in mesh.slots.values()}
    if len(lengths) != 1:
        raise MeshError("the slots hold different vertex counts")
    return mesh


def strip_triangles(count: int) -> npt.NDArray[np.int64]:
    """The triangles of a strip of `count` vertices, every other one turned to keep the winding."""
    if count < 3:
        return np.zeros((0, 3), dtype=np.int64)
    first = np.arange(count - 2)
    triangles = np.stack([first, first + 1, first + 2], axis=1)
    odd = first % 2 == 1
    triangles[odd] = triangles[odd][:, [1, 0, 2]]
    return triangles


def skin_weights(words: npt.NDArray[np.generic]) -> tuple[npt.NDArray[np.uint16], npt.NDArray[np.float32]]:
    """Bone indexes and weights from the packed slot: each word is a float whose low 10 bits hold
    `(node + 1) << 2` (0 for an unused slot); the weight is the float with those bits cleared."""
    raw = np.ascontiguousarray(words).view(np.uint32).reshape(-1, 4)
    nodes = ((raw & 0x3FF) >> 2).astype(np.int32) - 1
    weights = (raw & ~np.uint32(0x3FF)).view(np.float32).copy()
    unused = nodes < 0
    weights[unused] = 0.0
    nodes[unused] = 0
    return nodes.astype(np.uint16), weights
