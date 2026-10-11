// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// A synthetic font: a sprite sheet of 256 glyph rectangles over a fake 128 x 128 texture. Nothing here comes from the
// game (LEGAL.md, "No game data").

#include <cstddef>
#include <memory>

#include "graphics/font.h"
#include "graphics/particle_page.h"
#include "support/recording_device.h"

namespace coney::test {

/// Texels of the fake font texture, both ways.
inline constexpr int kFontTextureSize = 128;

/// A font of `rectangles` rectangles (256 by default, one per character) whose every rectangle is an 8 x 16-texel
/// glyph, except `W`, which is 16 x 16, and those past 255, which are 12 x 16. Rectangle `c` sits at
/// u0 = (c % 16) × 8 texels, v0 = ((c / 16) % 8) × 16, so rectangles 0-127 are distinct and those past 255 (big_font's
/// six `<MONEYFONT>` pictures stand there) differ from every other by their width. The first glyph is 0.
inline graphics::SpriteSheet testFontSheet(std::size_t rectangles = 256) {
    graphics::SpriteSheet sheet;
    sheet.page.firstGlyph = 0;
    constexpr float kTexel = 1.0F / kFontTextureSize;
    for (std::size_t c = 0; c < rectangles; ++c) {
        const float u0 = static_cast<float>(c % 16) * 8.0F * kTexel;
        const float v0 = static_cast<float>((c / 16) % 8) * 16.0F * kTexel;
        const float widthTexels = c == 'W' ? 16.0F : (c > 255 ? 12.0F : 8.0F);
        sheet.page.rects.push_back(graphics::UvRect{u0, v0, u0 + (widthTexels * kTexel), v0 + 16.0F * kTexel});
    }
    sheet.texture = std::make_shared<FakeTexture>(kFontTextureSize, kFontTextureSize);
    return sheet;
}

/// testFontSheet(`rectangles`) as a font.
inline graphics::Font testFont(std::size_t rectangles = 256) {
    return graphics::Font::fromSheet(testFontSheet(rectangles)).value();
}

} // namespace coney::test
