// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <span>
#include <vector>

#include "core/error.h"
#include "graphics/render_device.h"

namespace coney::graphics {

/// Bytes of a particle page's header, before its rectangles.
inline constexpr std::size_t kParticlePageHeaderSize = 0x14;
/// Bytes of one rectangle: four floats.
inline constexpr std::size_t kParticlePageRectSize = 16;

/// The rectangles of a sprite sheet, read from chunk 0x4C ("Particle Page"): texture coordinates into the single
/// texture of the dictionary before it. Every 2D image of the front end and the HUD is one of these rectangles.
///
/// Research: docs/research/gui.md#particle-page
struct ParticlePage {
    /// The rectangle of character 0 when the sheet is used as a font: character `c` is rectangle `firstGlyph + c`.
    /// -1 for a sheet that is not a font.
    std::int32_t firstGlyph = -1;
    /// The rectangles, in the file's order.
    std::vector<UvRect> rects;

    /// Rectangle `index`, which must be below rects.size() (checked by CONEY_ASSERT).
    /// @orig 0x00181e38 Page_Rect (unknown)
    [[nodiscard]] const UvRect& rect(std::size_t index) const;
};

/// Parses the data of a 0x4C chunk: the 20-byte header (a placeholder word, the count, firstGlyph and two words the
/// loader fills in), then `count` rectangles of four little-endian floats. Zero padding after the rectangles is
/// allowed and ignored, as are the placeholder and the two load-time words, which the original overwrites.
///
/// Fails with ErrorCode::Truncated when the data is shorter than its header or than its count says. The coordinates
/// are taken as they are: the original does not check them either.
[[nodiscard]] std::expected<ParticlePage, Error> parseParticlePage(std::span<const std::byte> data);

/// A sprite sheet ready to draw: its rectangles and the texture they refer to. A copy shares the texture.
///
/// The texture must be released before the renderer that made it stops (for librw textures, the RenderEngine).
struct SpriteSheet {
    ParticlePage page;
    std::shared_ptr<const Texture> texture; ///< Never null in a sheet a loader returns.
};

} // namespace coney::graphics
