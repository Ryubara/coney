// SPDX-License-Identifier: GPL-3.0-or-later
#include "hud/mash_meter.h"

#include <algorithm>
#include <array>
#include <string>

#include "graphics/font.h"
#include "graphics/overlay_camera.h"
#include "hud/hud_layout.h"

namespace coney::hud {

namespace {

// The layout table `0x0050d050` in the default video mode, GUI units, by player: the button's centre, the bar's left
// end and the glyphs' right edges (docs/research/hud.md#mash-meter-layout).
struct MeterPlace {
    float buttonX;
    float barLeft;
    float triangleX;
    float l1X;
    float r1X;
};
constexpr std::array<MeterPlace, 2> kPlaces{
    MeterPlace{.buttonX = 0.09F, .barLeft = 0.02F, .triangleX = 0.045F, .l1X = 0.05F, .r1X = 0.21F},
    MeterPlace{.buttonX = 0.885F, .barLeft = 0.815F, .triangleX = 1.015F, .l1X = 0.845F, .r1X = 1.005F}};
constexpr float kButtonY = 0.59F;
constexpr float kButtonSize = 0.12F;
constexpr float kBarY = 0.65F;
constexpr float kBarWidth = 0.2F;
constexpr float kBarHeight = 0.025F;
constexpr float kTextSize = 0.04F;
// Text 1 (triangle) sits on the bar's line, texts 2 and 3 (L1, R1) on the button's.
constexpr float kTriangleY = kBarY;
// The glyph codes of font slot 3 and the blink: a phase of 400 ms out of 800.
constexpr char kTriangleGlyph = static_cast<char>(0x96);
constexpr char kL1Glyph = static_cast<char>(0xa0);
constexpr char kR1Glyph = static_cast<char>(0x9c);
constexpr std::uint64_t kHalfPeriodMs = 400;
constexpr int kGlyphFontSlot = 3;
// The bar's sheet (`menu_system`, sheet-table record 3 of sprite word `0x30050`) and its rectangles: back, fill and
// the end caps.
constexpr std::uint32_t kBarRecord = 3;
constexpr std::size_t kBackRect = 80;
constexpr std::size_t kFillRect = 81;
constexpr std::size_t kLeftCapRect = 82;
constexpr std::size_t kRightCapRect = 83;
constexpr graphics::Rgba kGrey{128, 128, 128, 255};
constexpr graphics::Rgba kFillColour{225, 186, 65, 255};
// The batch and rectangles the bar is drawn with.
struct BarRects {
    graphics::SpriteBatch* batch;
    std::size_t back;
    std::size_t fill;
    std::size_t leftCap;
    std::size_t rightCap;
};
// A sprite word's rectangle (the low 16 bits).
constexpr std::uint32_t kWordRectMask = 0xffffU;

} // namespace

void MashMeter::show(int button, std::uint32_t word) {
    m_button = button;
    m_word = word;
    m_fill = 0.0F;
}

void MashMeter::hide() {
    m_button = 0;
    m_word = 0;
    m_fill = 0.0F;
}

void MashMeter::setFill(float fill) { m_fill = std::clamp(fill, 0.0F, 1.0F); }

char MashMeter::glyph(std::uint64_t nowMs) const {
    if (!shown()) {
        return 0;
    }
    const bool second = (nowMs % (2 * kHalfPeriodMs)) / kHalfPeriodMs != 0;
    if (m_word == kUncuffWord) {
        return second ? kL1Glyph : kR1Glyph;
    }
    return second ? kTriangleGlyph : 0;
}

void MashMeter::render(const HudCanvas& canvas, std::size_t player, std::uint64_t nowMs) const {
    if (!shown() || player >= kPlaces.size()) {
        return;
    }
    const MeterPlace& place = kPlaces.at(player);
    // The button sprite, centred, as tall as its size with square texels.
    if (graphics::SpriteBatch* parts = canvas.parts; parts != nullptr) {
        const std::size_t rect = m_word & kWordRectMask;
        if (rect < parts->sheet().page.rects.size()) {
            const graphics::UvRect uv = parts->sheet().page.rect(rect);
            addRect(parts, rect, place.buttonX, kButtonY, squareTexelWidth(parts->sheet(), uv, kButtonSize),
                    kButtonSize, kGrey);
        }
    }
    // The bar from its left end: the back, the fill over it, the caps outside. **Coney's stand-in**: the loaded
    // `menu_system` page has no rectangles 80-83, so the rage meter's `part_page0` rectangles stand in for them.
    BarRects rects{.batch = canvas.sheet ? canvas.sheet(kBarRecord) : nullptr,
                   .back = kBackRect,
                   .fill = kFillRect,
                   .leftCap = kLeftCapRect,
                   .rightCap = kRightCapRect};
    if (rects.batch == nullptr || kRightCapRect >= rects.batch->sheet().page.rects.size()) {
        rects = BarRects{.batch = canvas.parts,
                         .back = kMeterBodyRect,
                         .fill = kMeterStripRect,
                         .leftCap = kMeterLeftCapRect,
                         .rightCap = kMeterRightCapRect};
    }
    if (graphics::SpriteBatch* bar = rects.batch; bar != nullptr) {
        const float leftCap = capWidth(bar, rects.leftCap, kBarHeight);
        const float rightCap = capWidth(bar, rects.rightCap, kBarHeight);
        addRect(bar, rects.leftCap, place.barLeft - (leftCap / 2.0F), kBarY, leftCap, kBarHeight, kGrey);
        addStrip(bar, rects.back, place.barLeft, kBarY, kBarWidth, kBarHeight, kGrey);
        addStrip(bar, rects.fill, place.barLeft, kBarY, kBarWidth * m_fill, kBarHeight, kFillColour);
        addRect(bar, rects.rightCap, place.barLeft + kBarWidth + (rightCap / 2.0F), kBarY, rightCap, kBarHeight, kGrey);
    }
    // The blinking glyph, right-aligned on its place.
    const char shownGlyph = glyph(nowMs);
    const graphics::Font* font = canvas.text.fonts ? canvas.text.fonts(kGlyphFontSlot) : nullptr;
    if (shownGlyph == 0 || font == nullptr) {
        return;
    }
    const std::string text(1, shownGlyph);
    const graphics::FontMetrics metrics = metricsOfHeight(kTextSize);
    const float width = font->measure(text, metrics, graphics::kFontProportional);
    const float right = shownGlyph == kL1Glyph ? place.l1X : (shownGlyph == kR1Glyph ? place.r1X : place.triangleX);
    const float y = shownGlyph == kTriangleGlyph ? kTriangleY : kButtonY;
    drawPlainText(canvas, kGlyphFontSlot, text, right - width, y, metrics, kGrey);
}

} // namespace coney::hud
