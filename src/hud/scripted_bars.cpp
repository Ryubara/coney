// SPDX-License-Identifier: GPL-3.0-or-later
#include "hud/scripted_bars.h"

#include <algorithm>
#include <cmath>

#include "graphics/overlay_camera.h"
#include "gui/text_layout.h"
#include "hud/hud_layout.h"

namespace coney::hud {

namespace {

// The column: each bar's right end, the first bar's centre and the step between bars, GUI units; a hidden slot moves
// the later ones up by kHiddenStep.
constexpr float kColumnRight = 1.0F;
constexpr float kColumnTop = 0.26F;
constexpr float kColumnStep = 0.08F;
constexpr float kHiddenStep = 0.07F;
// The label: right-aligned at the column, this far above the bar's centre, at size 1.0 in font slot 3, grey.
constexpr float kLabelRaise = 0.035F;
constexpr float kLabelScale = 1.0F;
constexpr int kLabelFontSlot = 3;
constexpr graphics::Rgba kLabelColour{178, 178, 178, 255};
// The meter's background, and the gradient's ends.
constexpr graphics::Rgba kBarBackground{64, 64, 64, 255};
constexpr graphics::Rgba kEmptyColour{255, 16, 16, 255};
constexpr graphics::Rgba kFullColour{115, 183, 11, 255};
// Kind 0's colour and size, and kind 3's fill colour.
constexpr graphics::Rgba kRedBarColour{200, 30, 30, 255};
constexpr float kRedBarWidth = 0.17F;
constexpr graphics::Rgba kStackColour{217, 158, 12, 255};
// The chase gauge: its base, its sprites' height (overlay units), the marker's travel and lift.
constexpr float kGaugeX = 0.8F;
constexpr float kGaugeY = 0.25F;
constexpr float kGaugeSize = 0.03F;
constexpr float kGaugeHalfTravel = 0.115F;
constexpr float kGaugeMarkerLift = 0.04F;
// A sprite word's sheet-table record (top 16 bits) and rectangle (low 16).
constexpr unsigned kWordShift = 16U;
constexpr std::uint32_t kWordRectMask = 0xffffU;

// One channel of the mix of `a` and `b` at `t`.
std::uint8_t mix(std::uint8_t a, std::uint8_t b, float t) {
    return static_cast<std::uint8_t>(std::lround((static_cast<float>(a) * (1.0F - t)) + (static_cast<float>(b) * t)));
}

// Adds sprite word `word` as a sprite centred at GUI (`x`, `y`), `height` overlay units tall and as wide as its texels
// are square; nothing when its sheet's batch or its rectangle is missing.
void addWord(const HudCanvas& canvas, std::uint32_t word, float x, float y, float height, graphics::Rgba colour) {
    graphics::SpriteBatch* batch = canvas.sheet ? canvas.sheet(word >> kWordShift) : nullptr;
    const std::size_t rect = word & kWordRectMask;
    if (batch == nullptr || rect >= batch->sheet().page.rects.size()) {
        return;
    }
    const graphics::UvRect uv = batch->sheet().page.rect(rect);
    const float width = graphics::OverlayCamera::guiWidthToOverlay(squareTexelWidth(batch->sheet(), uv, height));
    batch->addSprite(graphics::Sprite{graphics::OverlayCamera::guiToOverlay(x, y), width, height, uv, colour});
}

} // namespace

graphics::Rgba ScriptedBars::gradientColour(float fill) {
    const float t = std::clamp(fill, 0.0F, 1.0F);
    return graphics::Rgba{mix(kEmptyColour.r, kFullColour.r, t), mix(kEmptyColour.g, kFullColour.g, t),
                          mix(kEmptyColour.b, kFullColour.b, t), 255};
}

void ScriptedBars::enable(int kind, bool on, std::span<const std::string> labels, std::uint32_t count, bool flag,
                          std::uint32_t texA, std::uint32_t texB) {
    const auto label = [labels](std::size_t i) { return i < labels.size() ? labels[i] : std::string(); };
    switch (kind) {
    case static_cast<int>(BarKind::Red):
    case static_cast<int>(BarKind::Labelled): {
        // Slot 0, made once; a second call while it exists does nothing.
        GenericBar& bar = m_bars.at(0);
        if (!on) {
            bar = GenericBar{};
        } else if (!bar.exists) {
            const bool red = kind == static_cast<int>(BarKind::Red);
            bar = GenericBar{.exists = true,
                             .shown = true,
                             .label = red ? std::string() : label(0),
                             .fill = 1.0F,
                             .gradient = !red,
                             .colour = red ? kRedBarColour : graphics::kWhite,
                             .width = red ? kRedBarWidth : GenericBar{}.width,
                             .height = GenericBar{}.height};
        }
        break;
    }
    case static_cast<int>(BarKind::Gauge):
        if (!on) {
            m_gauge = ChaseGauge{};
        } else if (!m_gauge.exists) {
            m_gauge = ChaseGauge{.exists = true, .endIcon = texA, .track = texB};
        }
        break;
    case static_cast<int>(BarKind::Stack):
        if (flag) {
            // One bar shown or hidden.
            if (count < kGenericBars) {
                m_bars.at(count).shown = on;
            }
        } else {
            const std::size_t bars = std::min<std::size_t>(count, kGenericBars);
            for (std::size_t i = 0; i < kGenericBars; ++i) {
                m_bars.at(i) = on && i < bars ? GenericBar{.exists = true,
                                                           .shown = true,
                                                           .label = label(i),
                                                           .fill = 1.0F,
                                                           .gradient = false,
                                                           .colour = kStackColour,
                                                           .width = GenericBar{}.width,
                                                           .height = GenericBar{}.height}
                                              : GenericBar{};
            }
        }
        break;
    default:
        break;
    }
}

void ScriptedBars::setPercentage(int kind, float fill, float fill2, std::uint32_t index, float value2) {
    if (kind < 0 || kind > static_cast<int>(BarKind::Stack)) {
        return;
    }
    if (kind != static_cast<int>(BarKind::Gauge)) {
        const std::size_t slot = kind == static_cast<int>(BarKind::Red) ? 0 : index;
        if (slot < kGenericBars && m_bars.at(slot).exists) {
            m_bars.at(slot).fill = std::clamp(fill, 0.0F, 1.0F);
        }
    }
    // Kind 1 falls through into the gauge, as kind 2 sets it.
    if (kind == static_cast<int>(BarKind::Labelled) || kind == static_cast<int>(BarKind::Gauge)) {
        m_gauge.value = fill;
        m_gauge.max = fill2;
        m_gauge.min = value2;
    }
}

void ScriptedBars::setProperty(std::uint32_t index, bool gradient, graphics::Rgba colour, float width) {
    if (index >= kGenericBars || !m_bars.at(index).exists) {
        return;
    }
    GenericBar& bar = m_bars.at(index);
    bar.colour = colour;
    bar.gradient = gradient;
    bar.width = width;
}

void ScriptedBars::clear() {
    m_bars = {};
    m_gauge = ChaseGauge{};
}

float ScriptedBars::gaugePosition() const {
    const float range = m_gauge.max - m_gauge.min;
    if (range == 0.0F) {
        return 1.0F;
    }
    return 1.0F - ((m_gauge.value - m_gauge.min) / range);
}

void ScriptedBars::renderBar(const HudCanvas& canvas, const GenericBar& bar, float right, float y) {
    graphics::SpriteBatch* parts = canvas.parts;
    if (parts != nullptr) {
        // The rage meter's rectangles, drawn right to left: the fill grows leftward from the right end.
        const float left = right - bar.width;
        const float leftCap = capWidth(parts, kMeterLeftCapRect, bar.height);
        const float rightCap = capWidth(parts, kMeterRightCapRect, bar.height);
        const float inner = bar.width - leftCap;
        const float bodyLeft = left + leftCap;
        const float filled = inner * std::clamp(bar.fill, 0.0F, 1.0F);
        addRect(parts, kMeterLeftCapRect, left + leftCap / 2.0F, y, leftCap, bar.height, kBarBackground);
        addStrip(parts, kMeterBodyRect, bodyLeft, y, inner, bar.height, kBarBackground);
        addStrip(parts, kMeterStripRect, bodyLeft + inner - filled, y, filled, bar.height,
                 bar.gradient ? gradientColour(bar.fill) : bar.colour);
        addRect(parts, kMeterRightCapRect, bodyLeft + inner + rightCap / 2.0F, y, rightCap, bar.height, kBarBackground);
    }
    if (!bar.label.empty() && canvas.text.fonts) {
        // Right-aligned at the column: measured, then moved left by its width.
        gui::TextStyle style;
        style.y = y - kLabelRaise;
        style.scale = kLabelScale;
        style.colour = kLabelColour;
        style.fontSlot = kLabelFontSlot;
        style.x = right - gui::layoutText(bar.label, style, canvas.text.fonts).width;
        gui::addTextSprites(gui::layoutText(bar.label, style, canvas.text.fonts), canvas.text.textBatch);
    }
}

void ScriptedBars::renderGauge(const HudCanvas& canvas) const {
    // Track, then marker, then the end icon; the marker green at the middle and red at both ends.
    const float f = gaugePosition();
    const float t = 1.0F - (2.0F * std::abs(0.5F - f));
    addWord(canvas, m_gauge.track, kGaugeX, kGaugeY, kGaugeSize, graphics::kWhite);
    addWord(canvas, kGaugeMarker, kGaugeX - kGaugeHalfTravel + (f * 2.0F * kGaugeHalfTravel),
            kGaugeY + kGaugeMarkerLift, 2.0F * kGaugeSize, gradientColour(t));
    addWord(canvas, m_gauge.endIcon, kGaugeX + kGaugeHalfTravel, kGaugeY, 2.0F * kGaugeSize, graphics::kWhite);
}

void ScriptedBars::render(const HudCanvas& canvas, float columnShift) const {
    // Slot i centred on the column's top + i steps, less kHiddenStep for each hidden slot before it.
    float raise = 0.0F;
    for (std::size_t i = 0; i < kGenericBars; ++i) {
        const GenericBar& bar = m_bars.at(i);
        if (!bar.exists) {
            continue;
        }
        if (!bar.shown) {
            raise += kHiddenStep;
            continue;
        }
        renderBar(canvas, bar, kColumnRight, kColumnTop + columnShift + (kColumnStep * static_cast<float>(i)) - raise);
    }
    if (m_gauge.exists) {
        renderGauge(canvas);
    }
}

} // namespace coney::hud
