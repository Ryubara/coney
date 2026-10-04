// SPDX-License-Identifier: GPL-3.0-or-later
#include "graphics/font.h"

#include <cmath>
#include <cstddef>
#include <utility>

#include "graphics/overlay_camera.h"
#include "graphics/screen.h"

namespace coney::graphics {

namespace {

// The characters that are measured as a gap and never drawn.
constexpr std::uint8_t kSpace = 0x20;
constexpr std::uint8_t kWideSpace = 0xac;
// A gap is this fraction of a glyph and its spacing.
constexpr float kSpaceFraction = 0.56F;

// True for a gap character.
bool isSpace(std::uint8_t character) { return character == kSpace || character == kWideSpace; }

// A texel count as the original rounds it: + 0.5, then truncated (the same as floor for these non-negative sizes).
float roundTexels(float texels) { return std::floor(texels + 0.5F); }

} // namespace

FontMetrics fontMetrics(float scale) {
    FontMetrics metrics;
    metrics.width = scale / 30.0F;
    metrics.height = metrics.width * (kLogicalWidth / kLogicalHeight) * 0.75F * 1.3333F;
    metrics.spacing = 0.003F;
    metrics.lineGap = -metrics.height / 7.0F;
    return metrics;
}

std::expected<Font, Error> Font::fromSheet(SpriteSheet sheet) {
    if (sheet.page.firstGlyph < 0) {
        return fail(ErrorCode::Invalid, "the sprite sheet is not a font (its first glyph is -1)");
    }
    if (!sheet.texture) {
        return fail(ErrorCode::Invalid, "the font's sprite sheet has no texture");
    }
    return Font(std::move(sheet));
}

std::optional<UvRect> Font::glyph(std::uint8_t character) const {
    const auto index = static_cast<std::size_t>(m_sheet.page.firstGlyph) + character;
    if (index >= m_sheet.page.rects.size()) {
        return std::nullopt;
    }
    return m_sheet.page.rect(index);
}

float Font::glyphWidth(std::uint8_t character, const FontMetrics& metrics, bool proportional) const {
    const std::optional<UvRect> rect = glyph(character);
    if (!rect || isSpace(character)) {
        return 0.0F;
    }
    if (!proportional) {
        return metrics.width;
    }
    // The rectangle's shape in texels, kept at the glyph height.
    const float widthPx = roundTexels((rect->u1 - rect->u0) * static_cast<float>(m_sheet.texture->width()));
    const float heightPx = roundTexels((rect->v1 - rect->v0) * static_cast<float>(m_sheet.texture->height()));
    return heightPx > 0.0F ? widthPx * metrics.width / heightPx : 0.0F;
}

float Font::advance(std::uint8_t character, const FontMetrics& metrics, bool proportional) const {
    if (!glyph(character)) {
        return 0.0F;
    }
    if (isSpace(character)) {
        const float gap = (metrics.width + metrics.spacing) * kSpaceFraction;
        return proportional ? gap / 2.0F : gap;
    }
    return glyphWidth(character, metrics, proportional) + metrics.spacing;
}

float Font::measure(std::string_view text, const FontMetrics& metrics, std::uint32_t flags) const {
    const bool proportional = (flags & kFontProportional) != 0;
    float width = 0.0F;
    for (const char c : text) {
        width += advance(static_cast<std::uint8_t>(c), metrics, proportional);
    }
    return width;
}

void Font::draw(std::vector<Sprite>& out, std::string_view text, float x, float y, const FontMetrics& metrics,
                std::uint32_t flags, Rgba colour, std::uint8_t shadowAlpha) const {
    // Where the pen starts: alignment moves it left by the measured width.
    const bool proportional = (flags & kFontProportional) != 0;
    float pen = x;
    if ((flags & kFontRightAligned) != 0) {
        pen -= measure(text, metrics, flags) + metrics.width / 2.0F;
    } else if ((flags & kFontCentred) != 0) {
        pen -= measure(text, metrics, flags) / 2.0F;
    }
    const auto shadowA = static_cast<std::uint8_t>(static_cast<unsigned>(shadowAlpha) * colour.a / 255U);

    // One sprite per drawn glyph, its shadow first.
    for (const char c : text) {
        const auto character = static_cast<std::uint8_t>(c);
        const float step = advance(character, metrics, proportional);
        const std::optional<UvRect> rect = glyph(character);
        if (rect && !isSpace(character)) {
            const float width = glyphWidth(character, metrics, proportional);
            const float centreX = pen + (width + metrics.spacing) / 2.0F;
            const float overlayWidth = OverlayCamera::guiWidthToOverlay(width);
            if (shadowAlpha != 0) {
                out.push_back(Sprite{OverlayCamera::guiToOverlay(centreX + kShadowOffsetX, y + kShadowOffsetY),
                                     overlayWidth, metrics.height, *rect, Rgba{0, 0, 0, shadowA}});
            }
            out.push_back(Sprite{OverlayCamera::guiToOverlay(centreX, y), overlayWidth, metrics.height, *rect, colour});
        }
        pen += step;
    }
}

} // namespace coney::graphics
