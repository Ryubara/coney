// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

namespace coney::graphics {

/// How the GS picks a mipmapped texture's level: by the vertex's distance from the camera, not by its size on screen.
/// With `TEX1`'s `LCM` 0 the level of detail is `LOD = log2(1/Q) × 2^L + K`, where `1/Q` is the camera distance (the
/// view depth, in metres), and the level is blended between `floor(LOD)` and the next under a linear-mip-linear
/// filter; below 0 the texture is magnified, at level 0. `K` and `L` come with each texture's PS2 raster (its header's
/// `+0x3c`, which RenderWare also writes as the sky mipmap extension `0x110`).
///
/// The streamed world's sectors are the only geometry drawn with mipmaps; everything else (objects, characters, the
/// sky, 2D) is drawn with level 0 alone (`MMIN` 1, bilinear).
///
/// Research: docs/research/rendering.md#world, docs/research/graphics.md#texture-formats,
/// docs/research/ps2-render.md#texture-states
struct TextureLod {
    float k = 0.0F; ///< The level at 1 m of distance: `K`, -2 to -7.6 on `level99`'s world (most -4 to -5).
    int l = 0;      ///< `L`: the distance term's scale is 2^L (0 on every texture seen).

    /// The level of detail at `depth` metres from the camera, before the clamp to the texture's levels: negative
    /// means magnified. A depth at or below 0 counts as a hair in front of the camera.
    [[nodiscard]] float at(float depth) const;
};

/// Reads the raster's packed word as librw keeps it (`K` in the low 12 bits, `L` in the next 2): `K` is the GS's
/// signed fixed point with four fraction bits, so 0xFC0 is -4.0 and 0xFB2 -4.875 (public GS register layout).
[[nodiscard]] TextureLod unpackTextureLod(std::uint32_t kl);

} // namespace coney::graphics
