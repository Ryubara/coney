// SPDX-License-Identifier: GPL-3.0-or-later
#include "hud/armies_hud.h"

#include <format>
#include <string>

#include "graphics/render_device.h"
#include "gui/text_layout.h"
#include "hud/messages.h"

namespace coney::hud {

namespace {

// The credits line's colour, (245, 184, 0).
constexpr graphics::Rgba kArmiesYellow{245, 184, 0, 255};
// The GO sign in the default video mode and English (`GoSign_Setup`; every x less the language's shift): the text at
// (0.8, 0.3); in mode Text one arrow at (0.78, 0.299) turned 4.72 radians, in mode Arrows three at (0.835 / 0.875 /
// 0.915, 0.39) unturned; arrows 0.06 square, `part_page0` rectangle 4 (sprite word 4), coloured as the text. Its font
// size is 3.9.
constexpr GuiPoint kGoTextPlace{0.8F, 0.3F};
constexpr float kGoTextScale = 3.9F;
constexpr GuiPoint kGoSingleArrow{0.78F, 0.299F};
constexpr float kGoSingleTurn = 4.72F;
constexpr std::array<GuiPoint, 3> kGoArrows{GuiPoint{0.835F, 0.39F}, GuiPoint{0.875F, 0.39F}, GuiPoint{0.915F, 0.39F}};
constexpr float kGoArrowSize = 0.06F;
constexpr std::size_t kGoArrowRect = 4;
// The join messages: grey (191, 191, 191), centred at the panel's place (the `ANHud` table `+0x110`).
constexpr graphics::Rgba kJoinGrey{191, 191, 191, 255};
constexpr std::array<GuiPoint, kPlayers> kJoinPlace{GuiPoint{0.145F, 0.08F}, GuiPoint{0.76F, 0.08F}};
// The GO sign's frames wrap at 2 in mode Text and at 4 in mode Arrows.
constexpr std::uint8_t kTextFrames = 2;
constexpr std::uint8_t kArrowFrames = 4;

// A message's style at GUI x `x`, in `colour`, centred or from the left.
// @coney-only the style Coney's text layout takes; the original sets each widget's colour and alignment in place
gui::TextStyle styleAt(float x, graphics::Rgba colour, bool centred) {
    gui::TextStyle style = messageStyle(x);
    style.colour = colour;
    if (centred) {
        style.alignment = gui::TextAlignment::Centre;
    }
    return style;
}

} // namespace

void ArmiesHud::levelSetUp(Language language) {
    m_credits = 0;
    // The sign moves left for the longer Spanish and Italian words.
    switch (language) {
    case Language::Spanish:
        m_goShift = kSpanishGoShift;
        break;
    case Language::Italian:
        m_goShift = kItalianGoShift;
        break;
    default:
        m_goShift = 0.0F;
        break;
    }
    m_goMode = static_cast<std::uint8_t>(GoSignMode::Off);
    m_goFrame = 0;
    // Both panels ask their player to join until he is attached: in level60 player 2's "PRESS THE START BUTTON TO
    // JOIN" shows from the start of play (docs/research/armies.md#the-join-messages).
    m_join = {true, true};
    // **Coney's choice**: a join message starts on its lit half (the blink's first state is not traced).
    m_joinLit = {true, true};
    m_in = {};
}

void ArmiesHud::setJoinMessage(std::size_t player, bool on) {
    if (player < kPlayers) {
        m_join.at(player) = on;
    }
}

// ANHud_Init zeroes the join flag along with the rest of the panel; it runs once, on the panel's first attach.
void ArmiesHud::panelAttached(std::size_t player) {
    if (player < kPlayers) {
        m_join.at(player) = false;
    }
}

bool ArmiesHud::joinShowing(std::size_t player) const {
    return m_active && player < kPlayers && m_join.at(player) && !m_in.at(player) && m_joinLit.at(player);
}

void ArmiesHud::update(std::uint64_t nowMs, int level, std::array<bool, kPlayers> playersIn) {
    m_active = isArmiesLevel(level);
    m_in = playersIn;
    if (!m_active) {
        return;
    }
    // The GO sign: its frame steps once the period has passed since the last step, wrapping by mode.
    if (!m_goTimed) {
        m_goLastMs = nowMs;
        m_goTimed = true;
    }
    if (m_goLastMs + kGoBlinkMs < nowMs) {
        ++m_goFrame;
        if (m_goFrame >= (m_goMode == static_cast<std::uint8_t>(GoSignMode::Text) ? kTextFrames : kArrowFrames)) {
            m_goFrame = 0;
        }
        m_goLastMs = nowMs;
    }
    // The join messages of the panels whose player is not in: one shared timer, so the first panel due takes the
    // step (ANHud_Update on each panel in turn).
    if (!m_joinTimed) {
        m_joinLastMs = nowMs;
        m_joinTimed = true;
    }
    for (std::size_t player = 0; player < kPlayers; ++player) {
        if (m_in.at(player) || !m_join.at(player)) {
            continue;
        }
        if (m_joinLastMs + kJoinBlinkMs < nowMs) {
            m_joinLit.at(player) = !m_joinLit.at(player);
            m_joinLastMs = nowMs;
        }
    }
}

void ArmiesHud::render(const HudCanvas& canvas, const std::function<std::string(std::uint32_t)>& strings) const {
    if (!m_active) {
        return;
    }
    const auto text = [&strings](std::uint32_t id) { return strings ? strings(id) : std::string{}; };
    // The credits line.
    drawMessage(canvas, std::format("{} {:02}", text(kCreditsString), m_credits),
                styleAt(kCreditsPlace.x, kArmiesYellow, false), kCreditsPlace.y);
    // The GO sign: in mode Text the text and the first arrow on frame 0; in mode Arrows the text and arrow `frame`;
    // any other mode hides it. **Coney stand-in**: the text is centred on the sign's place in the text font scaled
    // by its size (its widget's own font slot 4 and anchor are not traced), and the bleep (`vags/misc/bleep27`,
    // started again whenever the last one ended) is not played.
    if (m_goMode == static_cast<std::uint8_t>(GoSignMode::Text) ||
        m_goMode == static_cast<std::uint8_t>(GoSignMode::Arrows)) {
        gui::TextStyle style = styleAt(kGoTextPlace.x - m_goShift, kArmiesYellow, true);
        style.scale = kGoTextScale * messageStyle(0.0F).scale;
        drawMessage(canvas, text(kGoString), style, kGoTextPlace.y);
        if (canvas.parts != nullptr && kGoArrowRect < canvas.parts->sheet().page.rects.size()) {
            const graphics::UvRect uv = canvas.parts->sheet().page.rect(kGoArrowRect);
            if (m_goMode == static_cast<std::uint8_t>(GoSignMode::Text)) {
                if (m_goFrame == 0) {
                    canvas.parts->addSprite(guiSprite(kGoSingleArrow.x - m_goShift, kGoSingleArrow.y, kGoArrowSize,
                                                      kGoArrowSize, uv, kArmiesYellow),
                                            kGoSingleTurn);
                }
            } else if (m_goFrame < kGoArrows.size()) {
                const GuiPoint at = kGoArrows.at(m_goFrame);
                canvas.parts->addSprite(
                    guiSprite(at.x - m_goShift, at.y, kGoArrowSize, kGoArrowSize, uv, kArmiesYellow));
            }
        }
    }
    // The join messages, centred on the panel's place: alignment 2 is Font_Draw's flag 0x02, which moves each run's
    // start left by half its width (docs/research/gui.md); the layout's box starts at its x, so move it by half.
    for (std::size_t player = 0; player < kPlayers; ++player) {
        if (joinShowing(player) && canvas.text.fonts) {
            const GuiPoint at = kJoinPlace.at(player);
            const std::string message = text(kJoinString);
            gui::TextStyle style = styleAt(at.x, kJoinGrey, true);
            style.x -= gui::layoutText(message, style, canvas.text.fonts).width / 2.0F;
            drawMessage(canvas, message, style, at.y);
        }
    }
}

} // namespace coney::hud
