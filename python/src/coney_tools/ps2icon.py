# SPDX-License-Identifier: GPL-3.0-or-later
"""The PlayStation 2 memory card icon (`.ico`): a small morphing 3D model and its texture.

Layout (docs/research/formats/memory-card-icon.md): a 20-byte header `{u32 0x00010000, u32 shapes, u32 texture type,
u32 0, u32 vertices}`; per vertex, one `s16 x, y, z, w` position per shape, an `s16` normal, `s16 u, v` and an RGBA
colour; an animation header `{u32 1, u32 frame length, f32 speed, u32 offset, u32 frames}` and per frame
`{u32 shape, u32 keys}` followed by `keys` pairs `{f32 time, f32 weight}`; then the 128 x 128 texture, 16-bit
`A1B5G5R5` (uncompressed when bit 3 of the texture type is clear). Fixed-point values are 4.12 (1.0 = 4096).
"""

from __future__ import annotations

import struct
from dataclasses import dataclass
from typing import Any

import numpy as np
import numpy.typing as npt

MAGIC = 0x00010000
TEXTURE_SIZE = 128
_COMPRESSED = 0x08


class IconError(ValueError):
    """An icon whose layout does not hold together, or a texture form this decoder does not read."""


@dataclass
class Icon:
    """A decoded icon: one position array per shape, normals, texture coordinates, colours, the animation, the
    texture (RGBA, 128 x 128) and the header values."""

    shapes: list[npt.NDArray[np.float32]]
    normals: npt.NDArray[np.float32]
    uv: npt.NDArray[np.float32]
    colours: npt.NDArray[np.uint8]
    animation: dict[str, Any]
    texture: npt.NDArray[np.uint8]
    texture_type: int


def parse(data: bytes) -> Icon:
    """Decode an icon file."""
    if len(data) < 20:
        raise IconError("an icon is shorter than its header")
    magic, shape_count, texture_type, _, vertices = struct.unpack_from("<5I", data, 0)
    if magic != MAGIC or not 1 <= shape_count <= 16:
        raise IconError(f"not an icon (magic {magic:#x}, {shape_count} shapes)")
    stride = 8 * shape_count + 16
    end = 20 + stride * vertices
    if end + 20 > len(data):
        raise IconError(f"{vertices} vertices run past the file")
    table = np.frombuffer(data, np.uint8, stride * vertices, 20).reshape(vertices, stride)
    words = table[:, : 8 * shape_count + 12].copy().view("<i2").reshape(vertices, -1)
    shapes = [words[:, 4 * s : 4 * s + 3].astype(np.float32) / np.float32(4096) for s in range(shape_count)]
    normals = words[:, 4 * shape_count : 4 * shape_count + 3].astype(np.float32) / np.float32(4096)
    uv = words[:, 4 * shape_count + 4 : 4 * shape_count + 6].astype(np.float32) / np.float32(4096)
    colours = table[:, 8 * shape_count + 12 :].copy()
    _, frame_length, speed, offset, frame_count = struct.unpack_from("<IIfII", data, end)
    at = end + 20
    frames = []
    for _ in range(frame_count):
        shape, key_count = struct.unpack_from("<II", data, at)
        keys = struct.unpack_from(f"<{2 * key_count}f", data, at + 8)
        frames.append({"shape": shape, "keys": [[keys[i], keys[i + 1]] for i in range(0, len(keys), 2)]})
        at += 8 + 8 * key_count
    animation = {"frame_length": frame_length, "speed": speed, "offset": offset, "frames": frames}
    if texture_type & _COMPRESSED:
        raise IconError(f"texture type {texture_type:#x} is compressed; not read")
    size = TEXTURE_SIZE * TEXTURE_SIZE * 2
    if at + size > len(data):
        raise IconError("the texture runs past the file")
    pixels = np.frombuffer(data, "<u2", TEXTURE_SIZE * TEXTURE_SIZE, at).reshape(TEXTURE_SIZE, TEXTURE_SIZE)
    texture = np.empty((TEXTURE_SIZE, TEXTURE_SIZE, 4), np.uint8)
    for channel, shift in enumerate((0, 5, 10)):
        texture[..., channel] = ((pixels >> shift) & 31) * 255 // 31
    texture[..., 3] = 255
    return Icon(shapes, normals, uv, colours, animation, texture, texture_type)
