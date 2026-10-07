# SPDX-License-Identifier: GPL-3.0-or-later
"""The Xbox port's graphics resources: memory images the build tools wrote, read into textures and meshes.

Every Xbox graphics object (a texture chunk `0x2a`, a model chunk `0x47`, a sector BSP chunk `0x15`, a level's
world `.xlev` and each of its sectors `.xsec`) is one *resource image*:

    0x00  u32 kind, u32 tag, u32 resourceCount, u32 dataSize, u32 headersSize, u32 objectSize, then tool garbage
    0x80  dataSize bytes: the GPU data (texels of every mip level, vertices, indices)
    ...   resourceCount Direct3D resource headers (texture 20 bytes, vertex or index buffer 12), padded to headersSize
    ...   objectSize bytes: the object (a model's draw fields, a world's sector table ...)

A header's `Data` word is the offset of its bytes in the data block; the loader adds the block's address
(XBE 0x000bb190). A texture header's `Format` word holds the Direct3D format (bits 8-15), the mip count (16-19)
and log2 of the width (20-23) and height (24-27). Every texture on the disc is DXT1, DXT2/3 or DXT4/5, which the
Xbox stores unswizzled, so they decode as on a PC.

Research: docs/research/xbox-assets.md#resource-images
"""

from __future__ import annotations

import struct
from dataclasses import dataclass

import numpy as np
import numpy.typing as npt

#: Resource kinds (the image's first word) and what they are.
KIND_TEXTURE = 0
KIND_SKINNED_MODEL = 1
KIND_MODEL = 2
KIND_FRAME_MODEL = 3
KIND_SECTOR = 4
KIND_LEVEL = 5
KIND_BSP = 6
#: Direct3D formats on the disc (the XDK's numbers): the block-compressed ones.
DXT1 = 0x0C
DXT3 = 0x0E  # also DXT2: the Xbox gives both one number
DXT5 = 0x0F  # also DXT4
FORMAT_NAMES = {DXT1: "DXT1", DXT3: "DXT2/3", DXT5: "DXT4/5"}
_HEAD = 0x80  # the image header and its tool garbage
_TEXTURE_HEADER = 20  # Common, Data, Lock, Format, Size
_BUFFER_HEADER = 12  # Common, Data, Lock
_TYPE_VERTEX_BUFFER = 0
_TYPE_INDEX_BUFFER = 1
_TYPE_TEXTURE = 4


class XboxGfxError(ValueError):
    """A resource image that does not have the expected shape."""


@dataclass(frozen=True)
class D3DResource:
    """One Direct3D resource header of an image: its type, where its bytes start in the data block and, for a
    texture, the format fields."""

    type: int  # _TYPE_VERTEX_BUFFER, _TYPE_INDEX_BUFFER or _TYPE_TEXTURE (bits 16-18 of the Common word)
    offset: int  # the Data word: bytes into the data block
    format: int = 0  # texture: the D3D format number
    mips: int = 0
    width: int = 0
    height: int = 0

    @property
    def is_texture(self) -> bool:
        """Whether this header is a texture's."""
        return self.type == _TYPE_TEXTURE

    @property
    def is_vertex_buffer(self) -> bool:
        """Whether this header is a vertex buffer's."""
        return self.type == _TYPE_VERTEX_BUFFER

    @property
    def is_index_buffer(self) -> bool:
        """Whether this header is an index buffer's."""
        return self.type == _TYPE_INDEX_BUFFER

    @property
    def format_name(self) -> str:
        """The texture format's name (`DXT1`), or its number in hex."""
        return FORMAT_NAMES.get(self.format, f"0x{self.format:02x}")


@dataclass(frozen=True)
class ResourceImage:
    """A parsed resource image: its kind and tag, the data block, the resource headers and the object's bytes."""

    kind: int
    tag: int
    data: bytes
    resources: tuple[D3DResource, ...]
    object: bytes

    def textures(self) -> list[D3DResource]:
        """The texture headers, in stored order."""
        return [r for r in self.resources if r.is_texture]


def parse_image(raw: bytes | memoryview) -> ResourceImage:
    """Parse a resource image (a graphics chunk's data, or a whole `.xlev` / `.xsec` entry).

    Raises XboxGfxError when the header's sizes do not fit the bytes or a resource header has an unknown type.
    """
    raw = bytes(raw)
    if len(raw) < _HEAD:
        raise XboxGfxError(f"{len(raw)} bytes: shorter than the 128-byte image header")
    kind, tag, count, data_size, headers_size, object_size = struct.unpack_from("<6I", raw, 0)
    end = _HEAD + data_size + headers_size + object_size
    if end > len(raw) or count > headers_size // _BUFFER_HEADER:
        raise XboxGfxError(
            f"image kind {kind}: {count} headers in {headers_size} bytes, {data_size} data and {object_size} object "
            f"bytes need {end}, the image has {len(raw)}"
        )
    at = _HEAD + data_size
    resources = []
    for _ in range(count):
        common, offset = struct.unpack_from("<2I", raw, at)
        kind_bits = (common >> 16) & 7
        if kind_bits == _TYPE_TEXTURE:
            (packed,) = struct.unpack_from("<I", raw, at + 12)
            resources.append(
                D3DResource(
                    kind_bits,
                    offset,
                    (packed >> 8) & 0xFF,
                    (packed >> 16) & 0xF,
                    1 << ((packed >> 20) & 0xF),
                    1 << ((packed >> 24) & 0xF),
                )
            )
            at += _TEXTURE_HEADER
        elif kind_bits in (_TYPE_VERTEX_BUFFER, _TYPE_INDEX_BUFFER):
            resources.append(D3DResource(kind_bits, offset))
            at += _BUFFER_HEADER
        else:
            raise XboxGfxError(f"image kind {kind}: resource header of unknown type {kind_bits}")
    if at > _HEAD + data_size + headers_size:
        raise XboxGfxError(f"image kind {kind}: {count} resource headers overrun their {headers_size} bytes")
    data = raw[_HEAD : _HEAD + data_size]
    obj_at = _HEAD + data_size + headers_size
    return ResourceImage(kind, tag, data, tuple(resources), raw[obj_at : obj_at + object_size])


# --- textures ---


def _block_bytes(fmt: int) -> int:
    """Bytes per 4 x 4 block of a block-compressed format."""
    if fmt == DXT1:
        return 8
    if fmt in (DXT3, DXT5):
        return 16
    raise XboxGfxError(f"texture format 0x{fmt:02x} is not block-compressed (only DXT1/3/5 occur on the disc)")


def level_size(fmt: int, width: int, height: int) -> int:
    """Bytes of one mip level: whole 4 x 4 blocks."""
    return ((width + 3) // 4) * ((height + 3) // 4) * _block_bytes(fmt)


def texture_size(resource: D3DResource) -> int:
    """Bytes of a texture with every mip level (each level halves both sides, down to 1)."""
    total, width, height = 0, resource.width, resource.height
    for _ in range(max(1, resource.mips)):
        total += level_size(resource.format, width, height)
        width, height = max(1, width // 2), max(1, height // 2)
    return total


def _colour_blocks(blocks: npt.NDArray[np.uint8], four_always: bool) -> npt.NDArray[np.uint8]:
    """The colours of DXT colour blocks (n, 8 bytes) as (n, 16, 4) RGBA.

    Two RGB565 end points, then 2-bit indices, row by row, lowest bits first. A DXT1 block whose first end point is
    not greater than the second has three colours and transparent black; DXT3/5 colour blocks always have four.
    """
    ends = blocks[:, :4].copy().view("<u2").astype(np.int32)  # (n, 2)
    red = ((ends >> 11) & 0x1F) * 255 // 31
    green = ((ends >> 5) & 0x3F) * 255 // 63
    blue = (ends & 0x1F) * 255 // 31
    c0 = np.stack([red[:, 0], green[:, 0], blue[:, 0]], axis=1)
    c1 = np.stack([red[:, 1], green[:, 1], blue[:, 1]], axis=1)
    four = np.ones(len(blocks), dtype=bool) if four_always else ends[:, 0] > ends[:, 1]
    third = np.where(four[:, None], (2 * c0 + c1) // 3, (c0 + c1) // 2)
    fourth = np.where(four[:, None], (c0 + 2 * c1) // 3, 0)
    alpha = np.full((len(blocks), 4), 255, dtype=np.int32)
    alpha[:, 3] = np.where(four, 255, 0)
    palette = np.stack(
        [np.concatenate([c, a[:, None]], axis=1) for c, a in zip((c0, c1, third, fourth), alpha.T, strict=True)],
        axis=1,
    )  # (n, 4 colours, RGBA)
    bits = blocks[:, 4:8].copy().view("<u4")[:, 0]
    index = (bits[:, None] >> (2 * np.arange(16, dtype=np.uint32))) & 3
    return np.take_along_axis(palette, index[:, :, None].astype(np.intp), axis=1).astype(np.uint8)


def _alpha_explicit(blocks: npt.NDArray[np.uint8]) -> npt.NDArray[np.uint8]:
    """DXT3 alpha: 4 bits a texel, row by row, lowest nibble first; (n, 16)."""
    nibbles = np.stack([blocks[:, :8] & 0x0F, blocks[:, :8] >> 4], axis=2).reshape(-1, 16)
    return (nibbles * 17).astype(np.uint8)


def _alpha_interpolated(blocks: npt.NDArray[np.uint8]) -> npt.NDArray[np.uint8]:
    """DXT5 alpha: two end points, then 3-bit indices into eight values (six interpolated, or four and 0 and 255
    when the first end point is not greater than the second); (n, 16)."""
    a0 = blocks[:, 0].astype(np.int32)
    a1 = blocks[:, 1].astype(np.int32)
    eight = a0 > a1
    steps = []
    for i in range(8):
        if i < 2:
            steps.append(a0 if i == 0 else a1)
            continue
        six = ((8 - i) * a0 + (i - 1) * a1) // 7
        four = np.where(i == 6, 0, np.where(i == 7, 255, ((6 - i) * a0 + (i - 1) * a1) // 5))
        steps.append(np.where(eight, six, four))
    values = np.stack(steps, axis=1)  # (n, 8)
    raw = np.zeros(len(blocks), dtype=np.uint64)
    for byte in range(6):
        raw |= blocks[:, 2 + byte].astype(np.uint64) << np.uint64(8 * byte)
    index = (raw[:, None] >> (np.uint64(3) * np.arange(16, dtype=np.uint64))) & np.uint64(7)
    return np.take_along_axis(values, index.astype(np.intp), axis=1).astype(np.uint8)


def decode_dxt(data: bytes | memoryview, fmt: int, width: int, height: int) -> npt.NDArray[np.uint8]:
    """Decode one DXT1/3/5 level into a (height, width, 4) RGBA array.

    DXT2 and DXT4 (premultiplied alpha) share their numbers with DXT3 and DXT5 on the Xbox; the colours are returned
    as stored. Raises XboxGfxError when `data` is shorter than the level.
    """
    size = level_size(fmt, width, height)
    if len(data) < size:
        raise XboxGfxError(f"{FORMAT_NAMES.get(fmt, fmt)} {width}x{height}: {len(data)} bytes, needs {size}")
    across, down = (width + 3) // 4, (height + 3) // 4
    stride = _block_bytes(fmt)
    blocks = np.frombuffer(bytes(data[:size]), dtype=np.uint8).reshape(-1, stride)
    if fmt == DXT1:
        texels = _colour_blocks(blocks, four_always=False)
    else:
        texels = _colour_blocks(blocks[:, 8:], four_always=True)
        texels[:, :, 3] = _alpha_explicit(blocks) if fmt == DXT3 else _alpha_interpolated(blocks)
    image = texels.reshape(down, across, 4, 4, 4).transpose(0, 2, 1, 3, 4).reshape(down * 4, across * 4, 4)
    return np.ascontiguousarray(image[:height, :width])


def decode_texture(image: ResourceImage, resource: D3DResource) -> npt.NDArray[np.uint8]:
    """Level 0 of one of an image's textures as RGBA."""
    start = resource.offset
    end = start + level_size(resource.format, resource.width, resource.height)
    if end > len(image.data):
        raise XboxGfxError(f"texture at data offset {start} ends at {end}, past the data block ({len(image.data)})")
    return decode_dxt(memoryview(image.data)[start:end], resource.format, resource.width, resource.height)
