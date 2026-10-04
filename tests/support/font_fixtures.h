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

/// A font whose every character is an 8 x 16-texel glyph, except `W`, which is 16 x 16. Glyph `c` sits at
/// u0 = (c % 16) × 8 texels, so each character's rectangle is distinct. The first glyph is 0.
inline graphics::SpriteSheet testFontSheet() {
    graphics::SpriteSheet sheet;
    sheet.page.firstGlyph = 0;
    constexpr float kTexel = 1.0F / kFontTextureSize;
    for (std::size_t c = 0; c < 256; ++c) {
        const float u0 = static_cast<float>(c % 16) * 8.0F * kTexel;
        const float v0 = static_cast<float>((c / 16) % 8) * 16.0F * kTexel;
        const float width = (c == 'W' ? 16.0F : 8.0F) * kTexel;
        sheet.page.rects.push_back(graphics::UvRect{u0, v0, u0 + width, v0 + 16.0F * kTexel});
    }
    sheet.texture = std::make_shared<FakeTexture>(kFontTextureSize, kFontTextureSize);
    return sheet;
}

/// testFontSheet() as a font.
inline graphics::Font testFont() { return graphics::Font::fromSheet(testFontSheet()).value(); }

} // namespace coney::test
