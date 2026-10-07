# SPDX-License-Identifier: GPL-3.0-or-later
"""PS2 native textures in RenderWare texture dictionaries: parse them and decode them to RGBA.

Every texture on the disc is palettised (4 or 8 bits a texel, a 32-bit palette) and stored the way the PS2 uploads
it: each mipmap level is a GS transfer packet (a GIF tag and the `TRXPOS`, `TRXREG`, `TRXDIR` registers, then an
image tag and the texels), and the palette is one more such packet. An 8-bit texture is usually sent as 32-bit
pixels of half its width and height (a 4-bit one as 16-bit pixels), so the bytes are in the order the GS memory
puts them; decoding undoes that order. Palette alpha runs 0-128 (128 opaque) and becomes 0-255.

Research: docs/research/formats/renderware.md (Texture dictionary, PS2 raster)
"""

from __future__ import annotations

import struct
from dataclasses import dataclass
from functools import cache

import numpy as np
import numpy.typing as npt

from coney_tools import rw

#: Raster format bits (`rasterFormat`, the low 16 bits of the raster header's fourth word).
PAL8 = 0x2000
PAL4 = 0x4000
MIPMAP = 0x8000
AUTOMIPMAP = 0x1000
COLOUR_MASK = 0x0F00
C8888 = 0x0500
C1555 = 0x0100
C888 = 0x0600
_GS_PACKET = 0x50  # GIF tag + TRXPOS + TRXREG + TRXDIR + image tag, in front of each level and of the palette


class TextureError(ValueError):
    """A native texture this decoder cannot read."""


@dataclass(frozen=True)
class RasterHeader:
    """The 64-byte raster header of a PS2 native texture (the struct inside the raster struct)."""

    width: int
    height: int
    depth: int
    raster_format: int
    version: int
    tex0: int
    palette_offset: int
    tex1_low: int
    miptbp1: int
    miptbp2: int
    pixel_size: int
    palette_size: int
    total_size: int
    mipmap_k: int

    @property
    def levels(self) -> int:
        """Mipmap levels stored: 1 plus TEX1's MXL field when the raster has mipmaps."""
        if not self.raster_format & MIPMAP:
            return 1
        return 1 + ((self.tex1_low >> 2) & 7)


@dataclass(frozen=True)
class NativeTexture:
    """One texture of a dictionary: its names, sampling modes, raster header and the raw texel and palette bytes."""

    name: str
    mask: str
    filter_addressing: int
    header: RasterHeader
    pixels: bytes
    palette: bytes
    raster_stamp: int

    @property
    def filter_mode(self) -> int:
        """RenderWare's filter mode (1 nearest, 2 linear, 3-6 the mipmapped modes)."""
        return self.filter_addressing & 0xFF

    @property
    def address_u(self) -> int:
        """Addressing in u (1 wrap, 2 mirror, 3 clamp, 4 border)."""
        return (self.filter_addressing >> 8) & 0xF

    @property
    def address_v(self) -> int:
        """Addressing in v."""
        return (self.filter_addressing >> 12) & 0xF

    @property
    def depth(self) -> int:
        """Bits per texel."""
        return self.header.depth


def parse_dictionary(data: bytes | memoryview, start: int = 0) -> list[NativeTexture]:
    """Parse the texture dictionary section (type `0x16`) whose header is at `start`."""
    dictionary = rw.section_at(data, start)
    if dictionary.type != rw.TEX_DICTIONARY:
        raise rw.RwError(f"section 0x{dictionary.type:x} is not a texture dictionary")
    head = rw.need(rw.child(data, dictionary, rw.STRUCT), "dictionary struct")
    (count,) = struct.unpack_from("<H", data, head.start)
    textures = [parse_native(data, s) for s in rw.children(data, dictionary) if s.type == rw.TEXTURE_NATIVE]
    if len(textures) != count:
        raise rw.RwError(f"dictionary says {count} textures, holds {len(textures)}")
    return textures


def parse_native(data: bytes | memoryview, native: rw.Section) -> NativeTexture:
    """Parse one texture native section (type `0x15`) with platform `PS2\\0`."""
    parts = [s for s in rw.children(data, native)]
    if len(parts) < 4 or parts[0].type != rw.STRUCT or parts[3].type != rw.STRUCT:
        raise rw.RwError("texture native: expected struct, name, mask, raster")
    if bytes(data[parts[0].start : parts[0].start + 4]) != b"PS2\0":
        raise TextureError("texture native: platform is not PS2")
    (filter_addressing,) = struct.unpack_from("<I", data, parts[0].start + 4)
    name = rw.string(data, parts[1])
    mask = rw.string(data, parts[2])
    raster = parts[3]
    head = rw.need(rw.child(data, raster, rw.STRUCT, 0), "raster header")
    body = rw.need(rw.child(data, raster, rw.STRUCT, 1), "raster data")
    fields = struct.unpack_from("<IIIHhQIIQQIIII", data, head.start)
    header = RasterHeader(*fields)
    pixels = bytes(data[body.start : body.start + header.pixel_size])
    palette = bytes(data[body.start + header.pixel_size : body.start + header.pixel_size + header.palette_size])
    return NativeTexture(name, mask, filter_addressing, header, pixels, palette, head.stamp)


def _swizzle_index(x: npt.NDArray[np.int64], y: npt.NDArray[np.int64], log_w: int) -> npt.NDArray[np.int64]:
    """Where texel (x, y) of a 4-row band lies in the band's bytes when an 8-bit (or 4-bit) image was sent as 32-bit
    (16-bit) pixels: the GS column layout, folded into one bit shuffle."""
    y1 = (y >> 1) & 1
    y2 = (y >> 2) & 1
    x = x ^ ((y1 ^ y2) << 2)
    nx = (x & 7) | ((x >> 1) & ~7)
    n = y1 | (((x >> 3) & 1) << 1)
    return (n | (nx << 2) | ((y & 1) << (log_w + 1))) & ((1 << (log_w + 2)) - 1)


@cache
def unswizzle_table(width: int, height: int) -> npt.NDArray[np.int64]:
    """For a `width` x `height` image (width a power of two, height a multiple of 4): the source index of each
    texel, row by row."""
    log_w = max(width - 1, 0).bit_length()
    y, x = np.mgrid[0:height, 0:width].astype(np.int64)
    return ((y & ~3) * width + _swizzle_index(x, y, log_w)).ravel()


def _transfer(packet: bytes, at: int) -> tuple[int, int, int, int]:
    """Read one GS upload packet at `at`: (width, height) from TRXREG, the image's byte count, and where it starts."""
    if at + _GS_PACKET > len(packet):
        raise TextureError("GS packet runs past the data")
    width, height = struct.unpack_from("<II", packet, at + 0x20)
    (image_tag,) = struct.unpack_from("<Q", packet, at + 0x40)
    qwords = image_tag & 0x7FFF
    return width, height, qwords * 16, at + _GS_PACKET


def _texels(texture: NativeTexture) -> npt.NDArray[np.uint8]:
    """Level 0's palette indexes as a (height, width) array, in plain row order."""
    header = texture.header
    width, height, size, start = _transfer(texture.pixels, 0)
    depth = header.depth
    if depth not in (4, 8):
        raise TextureError(f"{texture.name}: depth {depth} is not palettised")
    raw = np.frombuffer(texture.pixels, dtype=np.uint8, count=size, offset=start)
    if depth == 4:
        values = np.empty(raw.size * 2, dtype=np.uint8)
        values[0::2] = raw & 0x0F
        values[1::2] = raw >> 4
    else:
        values = raw
    level_w = max(header.width, 1)
    level_h = max(header.height, 1)
    # Sent as wider pixels at half the size (8-bit as 32-bit, 4-bit as 16-bit): the swizzled order. Otherwise plain.
    swizzled = width * 2 >= level_w and height * 2 >= level_h and width * height * 4 == values.size
    if swizzled:
        full_w, full_h = width * 2, height * 2
        table = unswizzle_table(full_w, full_h)
        plane = values[table].reshape(full_h, full_w)
    else:
        full_w, full_h = width, height
        if full_w * full_h > values.size:
            raise TextureError(f"{texture.name}: {full_w}x{full_h} texels but {values.size} sent")
        plane = values[: full_w * full_h].reshape(full_h, full_w)
    if full_w < level_w or full_h < level_h:
        raise TextureError(f"{texture.name}: level 0 sent as {full_w}x{full_h}, smaller than {level_w}x{level_h}")
    return plane[:level_h, :level_w]


def palette_rgba(texture: NativeTexture) -> npt.NDArray[np.uint8]:
    """The palette as (colours, 4) RGBA bytes, in index order, with alpha scaled from 0-128 to 0-255."""
    if texture.header.raster_format & COLOUR_MASK != C8888:
        raise TextureError(f"{texture.name}: palette format 0x{texture.header.raster_format & COLOUR_MASK:x}")
    width, height, _, start = _transfer(texture.palette, 0)
    colours = np.frombuffer(texture.palette, dtype=np.uint8, count=width * height * 4, offset=start).reshape(-1, 4)
    colours = colours.copy()
    count = 256 if texture.depth == 8 else 16
    if colours.shape[0] < count:
        raise TextureError(f"{texture.name}: {colours.shape[0]} palette colours, needs {count}")
    colours = colours[:count]
    if count == 256:
        # The GS's CSM1 order: within each 32 entries, the second and third groups of 8 are swapped.
        index = np.arange(256)
        group = index & 0x18
        colours = colours[np.where((group == 0x08) | (group == 0x10), index ^ 0x18, index)]
    alpha = colours[:, 3].astype(np.uint16)
    colours[:, 3] = np.minimum(alpha * 255 // 128, 255).astype(np.uint8)
    return colours


def decode(texture: NativeTexture) -> npt.NDArray[np.uint8]:
    """Level 0 as a (height, width, 4) RGBA array."""
    texels = _texels(texture)
    return palette_rgba(texture)[texels]
