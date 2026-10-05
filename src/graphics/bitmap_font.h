// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>
#include <string_view>
#include <vector>

#include "graphics/render_device.h"

namespace coney::graphics {

/// Coney's built-in 5 x 7 pixel font for printable ASCII, drawn as flat-colour quads: the debug menus' text when no
/// disc (and so no game font) is loaded. The glyphs are Coney's own design, made for this file; nothing comes from the
/// game or another font. Each glyph is a 5 x 7 grid in a 6 x 9 cell (a column and two rows of spacing).
///
/// It needs no texture: every horizontal run of lit pixels in a glyph row is one quad, so it draws through any
/// RenderDevice, the headless one included.
class BitmapFont {
  public:
    /// Glyph width in font pixels.
    static constexpr int kGlyphWidth = 5;
    /// Glyph height in font pixels.
    static constexpr int kGlyphHeight = 7;
    /// The cell a character takes: its advance.
    static constexpr int kCellWidth = 6;
    /// The height of a line.
    static constexpr int kCellHeight = 9;

    /// The rows of `character`'s glyph, top first, bit 4 the leftmost pixel; a character outside 0x20-0x7e draws as
    /// `?`.
    [[nodiscard]] static std::array<std::uint8_t, kGlyphHeight> glyph(char character);

    /// The width of `text` in logical pixels at `scale` logical pixels per font pixel.
    [[nodiscard]] static float measure(std::string_view text, float scale) {
        return static_cast<float>(text.size() * kCellWidth) * scale;
    }

    /// Appends the quads of `text` with its top-left at (x, y) in logical pixels, `scale` logical pixels per font
    /// pixel, in `colour`, to `out`. Returns the x after the text.
    static float draw(std::vector<LogicalQuad>& out, std::string_view text, float x, float y, float scale, Rgba colour);
};

} // namespace coney::graphics
