// SPDX-License-Identifier: GPL-3.0-or-later
#include "hud/stereo_hud.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <utility>

#include "graphics/overlay_camera.h"

namespace coney::hud {

namespace {

// The stages' four corners of the backdrop, as signs of the x and y offsets: top right, top left, bottom left,
// bottom right (anticlockwise on screen).
constexpr std::array<std::pair<float, float>, 4> kCorners{{{1.0F, -1.0F}, {-1.0F, -1.0F}, {-1.0F, 1.0F}, {1.0F, 1.0F}}};
constexpr int kStages = 4;

// The panel's anchor for `player`: player 1's x mirrored.
GuiPoint anchorOf(std::size_t player) {
    return GuiPoint{player == 0 ? kStereoAnchor.x : 1.0F - kStereoAnchor.x, kStereoAnchor.y};
}

// A sprite of `size` overlay units high (and `width` wide) centred at GUI `place`.
graphics::Sprite overlaySprite(GuiPoint place, float width, float size, graphics::UvRect uv, graphics::Rgba colour) {
    return graphics::Sprite{graphics::OverlayCamera::guiToOverlay(place.x, place.y), width, size, uv, colour};
}

// Whether `batch` has rectangle `rect`.
bool hasRect(const graphics::SpriteBatch* batch, std::size_t rect) {
    return batch != nullptr && rect < batch->sheet().page.rects.size();
}

} // namespace

void StereoHud::start(float target) {
    m_shown = true;
    m_target = target > 0.0F ? target : 1.0F;
    m_value = 0.0F;
    m_stage = 0;
    m_step = kStereoPopStep;
    m_drop = 0.0F;
}

void StereoHud::setProgress(float value, int stage) {
    m_value = std::clamp(value, 0.0F, m_target);
    m_stage = std::clamp(stage, 0, kStages - 1);
}

void StereoHud::update(std::uint64_t nowMs) {
    m_nowMs = nowMs;
    if (!m_shown) {
        return;
    }
    // A complete stage (the pause before the next): the gauge drops, faster each update; otherwise it is back on the
    // arrow.
    if (m_value >= m_target) {
        m_drop += m_step;
        m_step *= 2.0F;
    } else {
        m_step = kStereoPopStep;
        m_drop = 0.0F;
    }
}

float StereoHud::gaugeSize() const {
    return kStereoGaugeMin + ((kStereoGaugeMax - kStereoGaugeMin) * m_value / m_target);
}

GuiPoint StereoHud::arrowPlace(std::size_t player) const {
    const GuiPoint anchor = anchorOf(player);
    const auto [sx, sy] = kCorners.at(static_cast<std::size_t>(m_stage));
    return GuiPoint{anchor.x + (sx * kStereoSize), anchor.y + (sy * kStereoCornerY)};
}

float StereoHud::arrowAngle() const { return -std::numbers::pi_v<float> / 2.0F * static_cast<float>(m_stage); }

std::size_t StereoHud::stickRect() const {
    return kStereoStickFirstRect -
           static_cast<std::size_t>((m_nowMs % (4 * kStereoStickFrameMs)) / kStereoStickFrameMs);
}

GuiPoint StereoHud::stickPlace(std::size_t player) const {
    const GuiPoint anchor = anchorOf(player);
    return GuiPoint{anchor.x + kStereoStickX, anchor.y + (m_stage < 2 ? kStereoSize : -kStereoSize)};
}

void StereoHud::render(const HudCanvas& canvas, std::size_t player) const {
    if (!m_shown) {
        return;
    }
    // The backdrop, its width from its rectangle's shape; then the arrow and the gauge, square, turned.
    if (graphics::SpriteBatch* sheet = canvas.minigames; hasRect(sheet, kStereoBackdropRect)) {
        const graphics::UvRect uv = sheet->sheet().page.rect(kStereoBackdropRect);
        const float width =
            graphics::OverlayCamera::guiWidthToOverlay(squareTexelWidth(sheet->sheet(), uv, kStereoSize));
        sheet->addSprite(overlaySprite(anchorOf(player), width, kStereoSize, uv, kStereoGrey));
        const GuiPoint arrow = arrowPlace(player);
        if (hasRect(sheet, kStereoArrowRect)) {
            sheet->addSprite(overlaySprite(arrow, kStereoArrowSize, kStereoArrowSize,
                                           sheet->sheet().page.rect(kStereoArrowRect), kStereoGrey),
                             arrowAngle());
        }
        if (hasRect(sheet, kStereoGaugeRect)) {
            const float size = gaugeSize();
            sheet->addSprite(overlaySprite(GuiPoint{arrow.x, arrow.y + m_drop}, size, size,
                                           sheet->sheet().page.rect(kStereoGaugeRect), kStereoGrey),
                             -m_value);
        }
    }
    // The stick in its four positions, and the ring turning beside it, always mirrored left to right (the update swaps
    // u0 and u1 of a rectangle rebuilt from its sprite word each time, so it never flickers: confirmed (runtime)).
    graphics::SpriteBatch* parts = canvas.parts;
    const GuiPoint stick = stickPlace(player);
    if (hasRect(parts, stickRect())) {
        parts->addSprite(overlaySprite(stick, kStereoStickSize, kStereoStickSize, parts->sheet().page.rect(stickRect()),
                                       kStereoGrey));
    }
    if (hasRect(parts, kStereoRingRect)) {
        graphics::UvRect uv = parts->sheet().page.rect(kStereoRingRect);
        std::swap(uv.u0, uv.u1);
        // Its turn, kept within one turn so the float stays exact however long the game runs.
        const auto angle = static_cast<float>(
            std::fmod(static_cast<double>(kStereoRingTurn) * static_cast<double>(m_nowMs), 2.0 * std::numbers::pi));
        parts->addSprite(overlaySprite(GuiPoint{stick.x + kStereoRingX, stick.y}, kStereoRingSize, kStereoRingSize, uv,
                                       kStereoRingColour),
                         angle);
    }
}

} // namespace coney::hud
