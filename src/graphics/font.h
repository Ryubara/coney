// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <expected>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

#include "core/error.h"
#include "graphics/particle_page.h"
#include "graphics/render_device.h"
#include "graphics/sprite_batch.h"

namespace coney::graphics {

/// The size of a font's glyphs at one scale, in GUI units (x across, y down, the screen about [0, 1]²).
struct FontMetrics {
    float width = 0.0F;   ///< w: a glyph's width in fixed-width mode, and the base of the others.
    float height = 0.0F;  ///< h: every glyph's height; w × (W / H) × 0.75 × 1.3333.
    float spacing = 0.0F; ///< Added after every glyph: 0.003.
    float lineGap = 0.0F; ///< Added to h between lines: -h / 7, so lines sit closer than their height.

    friend bool operator==(const FontMetrics&, const FontMetrics&) = default;
};

/// The metrics at `scale`: w = scale / 30, h = w × (W / H) × 0.75 × 1.3333 with W, H the screen's 640 and 448,
/// spacing 0.003, lineGap = -h / 7.
/// @orig 0x00179808 Font_Size (unknown)
[[nodiscard]] FontMetrics fontMetrics(float scale);

/// Font_Draw flag: draw ending left of x (by half a glyph more).
inline constexpr std::uint32_t kFontRightAligned = 0x01;
/// Font_Draw flag: draw centred on x.
inline constexpr std::uint32_t kFontCentred = 0x02;
/// Font_Measure and Font_Draw flag: each glyph as wide as its rectangle's shape; otherwise all w wide.
inline constexpr std::uint32_t kFontProportional = 0x04;

/// The black copy drawn under each glyph when a shadow is asked for: offset in GUI units.
inline constexpr float kShadowOffsetX = 0.0025F;
inline constexpr float kShadowOffsetY = 0.004F;

/// A sprite sheet used as a font: character `c` (a byte) is rectangle `firstGlyph + c`, and the button and HUD icons
/// are characters like any other. Measuring and drawing follow the original's text functions.
///
/// Research: docs/research/gui.md#text
class Font {
  public:
    /// The font of `sheet`. Fails with ErrorCode::Invalid when the sheet is not a font (`firstGlyph` -1) or has no
    /// texture.
    [[nodiscard]] static std::expected<Font, Error> fromSheet(SpriteSheet sheet);

    /// The sheet.
    [[nodiscard]] const SpriteSheet& sheet() const { return m_sheet; }

    /// The rectangle of `character`, or nothing when the sheet has no rectangle that far (Coney's choice: such a
    /// character is skipped, taking no room).
    [[nodiscard]] std::optional<UvRect> glyph(std::uint8_t character) const;

    /// The drawn width of `character` in GUI units: w in fixed-width mode; in proportional mode the rectangle's shape
    /// at height h, wPx × w / hPx with wPx and hPx its size in texels rounded (+ 0.5, truncated). 0 for a character
    /// with no rectangle, and for a space (0x20) or 0xac, which are not drawn.
    [[nodiscard]] float glyphWidth(std::uint8_t character, const FontMetrics& metrics, bool proportional) const;

    /// How far `character` moves the pen: its width plus the spacing, or for a space (0x20) or 0xac
    /// (w + spacing) × 0.56, halved in proportional mode. 0 for a character with no rectangle.
    [[nodiscard]] float advance(std::uint8_t character, const FontMetrics& metrics, bool proportional) const;

    /// The width of `text` in GUI units: the sum of its advances. Only kFontProportional of `flags` matters.
    /// @orig 0x00179958 Font_Measure (unknown)
    [[nodiscard]] float measure(std::string_view text, const FontMetrics& metrics, std::uint32_t flags) const;

    /// Appends one sprite per drawn character of `text` to `out`, the pen starting at GUI x `x` (moved left by the
    /// width plus w / 2 with kFontRightAligned, by half the width with kFontCentred) and every glyph centred on GUI y
    /// `y`. Each glyph is a sprite centred at (pen + (width + spacing) / 2, y) of size (width, h), converted to the
    /// overlay camera's space, with the glyph's rectangle and `colour`. With a non-zero `shadowAlpha`, a black copy
    /// offset by (kShadowOffsetX, kShadowOffsetY) with alpha `shadowAlpha` × colour alpha / 255 comes just before each
    /// glyph.
    /// @orig 0x00179c30 Font_Draw (unknown)
    void draw(std::vector<Sprite>& out, std::string_view text, float x, float y, const FontMetrics& metrics,
              std::uint32_t flags, Rgba colour, std::uint8_t shadowAlpha = 0) const;

  private:
    explicit Font(SpriteSheet sheet) : m_sheet(std::move(sheet)) {}

    SpriteSheet m_sheet;
};

} // namespace coney::graphics
