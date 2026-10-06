// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "graphics/render_device.h"

// The platform-neutral half of Coney's 2D reference images (`coney --render-references`, the radar icons and the
// particle effects; docs/guides/building.md#reference-images): which sprite each image shows, which texels a sprite
// sheet's rectangle covers, and how the cut-out is scaled. Pure and deterministic (integer arithmetic), so the same
// disc gives byte-identical images on every machine; the renderer in src/platform/ only loads and writes.

namespace coney::graphics {

/// The largest side of a 2D reference image, in pixels: LEGAL.md allows icons of at most 64 x 64.
inline constexpr int kReferenceIconLimit = 64;

/// A rectangle of whole texels: its top-left texel and its size.
struct TexelBox {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;

    friend bool operator==(const TexelBox&, const TexelBox&) = default;
};

/// An image's width and height in pixels.
struct ImageSize {
    int width = 0;
    int height = 0;

    friend bool operator==(const ImageSize&, const ImageSize&) = default;
};

/// The texels `rect` covers on a `textureWidth` × `textureHeight` texture: from the texel holding its top-left corner
/// to the one holding its bottom-right, whole. The disc's rectangles are inset by a quarter of a texel's width on
/// every side (docs/research/gui.md#particle-page), so this is the sprite as drawn. Clamped to the texture, and at
/// least one texel each way.
[[nodiscard]] TexelBox rectTexels(const UvRect& rect, int textureWidth, int textureHeight);

/// The size an image of `width` × `height` pixels is scaled to so that its longer side is at most `limit`, keeping
/// its proportions (the shorter side rounded to nearest, at least 1). With `enlarge`, a smaller image is scaled up so
/// that its longer side is `limit`; without, it keeps its own size.
[[nodiscard]] ImageSize fitWithin(int width, int height, int limit, bool enlarge);

/// The pixels of `box` cut out of an RGBA image of `width` × `height` pixels (rows top down). The box must lie inside
/// the image (checked by CONEY_ASSERT).
[[nodiscard]] std::vector<std::uint8_t> cropRgba(std::span<const std::uint8_t> rgba, int width, int height,
                                                 const TexelBox& box);

/// An RGBA image of `width` × `height` pixels scaled to `outWidth` × `outHeight`: each output pixel is the average of
/// the source area it covers, weighted by how much of each source pixel falls in it and by alpha (premultiplied, so a
/// transparent neighbour leaves no colour fringe). Exact integer arithmetic; scaling up by a whole factor repeats each
/// pixel. Every size must be positive (checked by CONEY_ASSERT).
[[nodiscard]] std::vector<std::uint8_t> resampleRgba(std::span<const std::uint8_t> rgba, int width, int height,
                                                     int outWidth, int outHeight);

/// A sprite a reference image shows: the image's name, the sheet (its resource name, or `0x` and the hex name hash
/// for a sheet whose name is not recovered) and the rectangle's index in it.
struct ReferenceSprite {
    std::string_view name;
    std::string_view sheet;
    std::uint32_t rect = 0;
};

/// The radar icons the reference images show: every icon id the scripts and the code use
/// (docs/references/radar-icons.md). Icon `n` is rectangle `n` of kRadarSheet (docs/research/gui.md#radar-icons).
[[nodiscard]] std::span<const std::uint32_t> radarIconIds();

/// The sprite sheet the radar draws its icons from.
inline constexpr std::string_view kRadarSheet = "part_page0";

/// The particle effects whose sprite is traced, in name order, each with the first rectangle its code draws
/// (docs/references/particles.md, docs/research/particles.md).
[[nodiscard]] std::span<const ReferenceSprite> particleSprites();

/// The file name of radar icon `id`'s image: `icon-<id>.png`, as docs/references/index.md names it.
[[nodiscard]] std::string radarIconFileName(std::uint32_t id);

} // namespace coney::graphics
