// SPDX-License-Identifier: GPL-3.0-or-later
#include "hud/mug_meter.h"

#include <algorithm>
#include <array>
#include <cmath>

#include "graphics/overlay_camera.h"
#include "gui/text_layout.h"

namespace coney::hud {

namespace {

// Player 0's places (GUI, with the update's -0.07 already in); player 1's x in the second entry of each pair.
constexpr std::array<float, kPlayers> kMugBarLeftX{0.09F, 0.74F};
constexpr float kMugUsedBarY = 0.57F;
constexpr float kMugOnTargetBarY = 0.545F;
constexpr GuiSize kMugUsedBarSize{0.2F, 0.014F};
constexpr GuiSize kMugOnTargetBarSize{0.2F, 0.025F};
constexpr std::array<float, kPlayers> kMugStickX{0.04F, 0.935F};
constexpr float kMugStickY = 0.55F;
constexpr float kMugStickBaseSize = 0.06F;
constexpr float kMugStickDotSize = 0.05F;
constexpr float kMugStickTravel = 0.01F;
constexpr std::array<float, kPlayers> kMugPromptX{-0.01F, 0.69F};
constexpr float kMugPromptY = 0.63F;
constexpr float kMugPromptScale = 1.0F;
// The original's font slot 3 is `part_page0`, Coney's text slot.
constexpr int kMugPromptFontSlot = gui::kTextFontSlot;
constexpr graphics::Rgba kMugPromptColour{178, 178, 178, 255};
constexpr float kMugArcSize = 0.05F;
// Player 1's arcs sit this much right of player 0's places.
constexpr float kMugPlayer1ArcShift = 0.898F;
// The stick past this on an axis picks a direction; the straight ones need the other axis within the second.
constexpr float kMugPick = 0.5F;
constexpr float kMugStraight = 0.1F;

// The eight arc places (GUI) and angles (radians, clockwise on screen): up, down, left, right, up-left, up-right,
// down-right, down-left.
struct ArcPlace {
    GuiPoint place;
    float angle;
};
constexpr std::array<ArcPlace, 8> kMugArcPlaces{{{{0.04F, 0.50F}, 2.4F},
                                                 {{0.04F, 0.60F}, 5.5F},
                                                 {{0.00F, 0.555F}, 0.65F},
                                                 {{0.075F, 0.55F}, 3.85F},
                                                 {{0.01F, 0.515F}, 1.5F},
                                                 {{0.07F, 0.515F}, 3.2F},
                                                 {{0.07F, 0.585F}, 4.7F},
                                                 {{0.005F, 0.585F}, 0.1F}}};

// The colours: amber, red and blue, the bars' back, and the stick's grey.
constexpr graphics::Rgba kMugAmber{128, 100, 0, 255};
constexpr graphics::Rgba kMugRed{170, 43, 43, 255};
constexpr graphics::Rgba kMugBlue{35, 83, 188, 255};
constexpr graphics::Rgba kMugBarBack{32, 32, 32, 255};
constexpr graphics::Rgba kMugStickGrey{128, 128, 128, 255};
// An arc lit before the newest is drawn at half alpha.
constexpr std::uint8_t kMugHalfAlpha = 0x80;

// Whether `batch` has rectangle `rect`.
bool hasRect(const graphics::SpriteBatch* batch, std::size_t rect) {
    return batch != nullptr && rect < batch->sheet().page.rects.size();
}

// A square sprite `size` overlay units high centred at GUI `place`, turned `angle`.
void addSquare(graphics::SpriteBatch* batch, std::size_t rect, GuiPoint place, float size, graphics::Rgba colour,
               float angle = 0.0F) {
    if (!hasRect(batch, rect)) {
        return;
    }
    batch->addSprite(graphics::Sprite{graphics::OverlayCamera::guiToOverlay(place.x, place.y), size, size,
                                      batch->sheet().page.rect(rect), colour},
                     angle);
}

} // namespace

void MugMeter::setActive(bool on) {
    if (on && !m_active) {
        m_used = 0.0F;
        m_onTarget = 0.0F;
        m_ripple = 0;
    }
    m_active = on;
}

void MugMeter::setFills(float used, float onTarget) {
    m_used = std::clamp(used, 0.0F, 1.0F);
    m_onTarget = std::clamp(onTarget, 0.0F, 1.0F);
}

void MugMeter::setStick(float x, float y) {
    m_stickX = x;
    m_stickY = y;
    m_stickSet = true;
    // The eight directions: the diagonals past 0.5 on both axes, the straight ones with the other axis near 0.
    const bool up = y > kMugPick;
    const bool down = y < -kMugPick;
    const bool left = x < -kMugPick;
    const bool right = x > kMugPick;
    const bool xNear = std::fabs(x) < kMugStraight;
    const bool yNear = std::fabs(y) < kMugStraight;
    if (up && xNear) {
        m_direction = 0;
    } else if (down && xNear) {
        m_direction = 1;
    } else if (left && yNear) {
        m_direction = 2;
    } else if (right && yNear) {
        m_direction = 3;
    } else if (up && left) {
        m_direction = 4;
    } else if (up && right) {
        m_direction = 5;
    } else if (down && right) {
        m_direction = 6;
    } else if (down && left) {
        m_direction = 7;
    }
}

void MugMeter::update() {
    // Modes 0 and 2 show the arcs while the stick is on target, 1 and 3 while it is off; only after a stick update.
    const bool onRule = m_mode == MugMeterMode::Mugging || m_mode == MugMeterMode::Holding;
    m_arcsShown = m_active && m_stickSet && m_direction.has_value() && (m_stickOnTarget == onRule);
    if (m_arcsShown) {
        ++m_ripple;
    }
    m_stickSet = false;
}

void MugMeter::renderBar(graphics::SpriteBatch* sheet, GuiPoint left, GuiSize size, float fill, graphics::Rgba colour) {
    if (sheet == nullptr) {
        return;
    }
    // Left to right: the left cap, the body over the inner width, the fill over its share, the right cap.
    const float leftCap = capWidth(sheet, kMeterLeftCapRect, size.height);
    const float rightCap = capWidth(sheet, kMeterRightCapRect, size.height);
    const float inner = size.width - leftCap;
    const float bodyLeft = left.x + leftCap;
    addRect(sheet, kMeterLeftCapRect, left.x + leftCap / 2.0F, left.y, leftCap, size.height, kMugBarBack);
    addStrip(sheet, kMeterBodyRect, bodyLeft, left.y, inner, size.height, kMugBarBack);
    addStrip(sheet, kMeterStripRect, bodyLeft, left.y, inner * fill, size.height, colour);
    addRect(sheet, kMeterRightCapRect, bodyLeft + inner + rightCap / 2.0F, left.y, rightCap, size.height, kMugBarBack);
}

void MugMeter::render(const HudCanvas& canvas, std::size_t player, const std::string& prompt) const {
    if (!m_active) {
        return;
    }
    const std::size_t p = std::min(player, kPlayers - 1);
    // The colours by mode: mugging puts amber on the time used and red (blue in a hold) on the time on target and the
    // arcs; the other side swaps them.
    const bool hold = m_mode == MugMeterMode::Holding || m_mode == MugMeterMode::Held;
    const bool swapped = m_mode == MugMeterMode::Mugged || m_mode == MugMeterMode::Held;
    const graphics::Rgba strong = hold ? kMugBlue : kMugRed;
    const graphics::Rgba usedColour = swapped ? strong : kMugAmber;
    const graphics::Rgba targetColour = swapped ? kMugAmber : strong;

    // 1. The two bars.
    graphics::SpriteBatch* parts = canvas.parts;
    renderBar(parts, GuiPoint{kMugBarLeftX.at(p), kMugUsedBarY}, kMugUsedBarSize, m_used, usedColour);
    renderBar(parts, GuiPoint{kMugBarLeftX.at(p), kMugOnTargetBarY}, kMugOnTargetBarSize, m_onTarget, targetColour);

    // 2. The stick: the ball, and the dot moved a little toward the stick.
    graphics::SpriteBatch* sheet = canvas.minigames;
    const GuiPoint stick{kMugStickX.at(p), kMugStickY};
    addSquare(sheet, kMugStickBaseRect, stick, kMugStickBaseSize, kMugStickGrey);
    addSquare(sheet, kMugStickDotRect,
              GuiPoint{stick.x + (kMugStickTravel * m_stickX), stick.y - (kMugStickTravel * m_stickY)},
              kMugStickDotSize, kMugStickGrey);

    // 3. The arcs, rippling out: the first alone, then it at half with the second, then the second at half with the
    // third, then none.
    if (m_arcsShown && m_direction) {
        const ArcPlace& arc = kMugArcPlaces.at(*m_direction);
        const GuiPoint place{arc.place.x + (p == 1 ? kMugPlayer1ArcShift : 0.0F), arc.place.y};
        graphics::Rgba half = swapped ? kMugAmber : strong;
        const graphics::Rgba full = half;
        half.a = kMugHalfAlpha;
        const std::size_t phase = ripplePhase();
        if (phase < 3) {
            if (phase > 0) {
                addSquare(sheet, kMugFirstArcRect + phase - 1, place, kMugArcSize, half, arc.angle);
            }
            addSquare(sheet, kMugFirstArcRect + phase, place, kMugArcSize, full, arc.angle);
        }
    }

    // 4. The prompt above, left-aligned.
    if (!prompt.empty() && canvas.text.fonts) {
        gui::TextStyle style;
        style.x = kMugPromptX.at(p);
        style.y = kMugPromptY;
        style.scale = kMugPromptScale;
        style.colour = kMugPromptColour;
        style.fontSlot = kMugPromptFontSlot;
        gui::addTextSprites(gui::layoutText(prompt, style, canvas.text.fonts), canvas.text.textBatch);
    }
}

} // namespace coney::hud
