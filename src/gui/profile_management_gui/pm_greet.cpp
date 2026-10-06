// SPDX-License-Identifier: GPL-3.0-or-later
#include "gui/profile_management_gui/pm_greet.h"

#include <algorithm>
#include <array>

#include "core/pad.h"
#include "gui/colour_table.h"
#include "gui/profile_management_gui/pm_widgets.h"

namespace coney::gui {

PmGreet::PmGreet(PmShared& shared) : m_shared(shared), m_logo(shared.frontSprites, kLogoRect) {}

void PmGreet::enter(ScreenFlowController& /*flow*/) {
    // Init: the logo, left edge at x, and the prompt under it.
    const PmLayout& layout = m_shared.layout;
    const bool flag02Alone = m_shared.video.flag02 && !m_shared.video.widescreen;
    m_logo.init();
    m_logo.setBatch(m_shared.frontSprites);
    m_logo.setup(BaseWidgetSetup{.x = layout.x,
                                 .y = flag02Alone ? kLogoYFlag02 : kLogoY,
                                 .height = flag02Alone ? kLogoHeightFlag02 : kLogoHeight,
                                 .colour = kMenuRed,
                                 .anchor = SpriteAnchor::Left});
    m_prompt.init();
    m_prompt.setText(m_shared.string(kPromptString));
    m_prompt.setup(TextWidgetSetup{.x = layout.x,
                                   .y = layout.gridFor(1),
                                   .scale = pm::kPromptScale,
                                   .colour = kMenuRed,
                                   .alignment = TextAlignment::Left,
                                   .fontSlot = kBigFontSlot});
    m_prompt.setFade(0.0F);
    m_rising = true;
    m_phaseStartMs = m_shared.frame.timeMs;
    m_idleSinceMs = m_shared.frame.timeMs;
    m_attract = false;
}

int PmGreet::update() {
    const GuiFrame& frame = m_shared.frame;
    const bool fading = m_shared.fadeActive();

    // The blink: fully lit while a fade runs (phase 1, its timer and the idle clock restarted), else the ramps.
    if (fading) {
        m_prompt.setFade(1.0F);
        m_rising = true;
        m_phaseStartMs = frame.timeMs;
        m_idleSinceMs = frame.timeMs;
    } else if (!m_shared.finishing) {
        if (frame.timeMs - m_phaseStartMs >= kBlinkPeriodMs) {
            m_rising = !m_rising;
            m_phaseStartMs = frame.timeMs;
        }
        m_prompt.setFade(static_cast<float>(blinkAlpha(m_rising, frame.timeMs - m_phaseStartMs)) / 255.0F);
    }
    m_prompt.update(frame);

    // The idle clock: the attract movie when it runs out, and the wait starts again.
    if (!fading && frame.timeMs - m_idleSinceMs >= kIdleMs) {
        m_attract = true;
        m_idleSinceMs = frame.timeMs;
        const std::array<double, 1> movie{kAttractMovie};
        m_shared.call(kPlayMovieFunction, movie);
    }

    // START leads to the main menu, and the screen draws no more; not while the attract movie's fade runs.
    const bool startHeld = frame.pad != nullptr && (frame.pad->pressedWithRepeat() & pad::kStart) != 0;
    if (startHeld && !(m_attract && fading)) {
        m_shared.cue(kStartCue);
        return kToMode;
    }

    // Render: the text, then the sprite (they go to different batches; the 2D pass orders them by depth).
    m_prompt.render(m_shared.canvas);
    m_logo.render(m_shared.canvas);
    return kStay;
}

void PmGreet::exit() {
    m_prompt.shutdown();
    m_logo.shutdown();
}

std::uint8_t PmGreet::blinkAlpha(bool rising, std::uint64_t elapsedMs) {
    const std::uint64_t ramp = std::min(elapsedMs, kBlinkPeriodMs) * 255 / kBlinkPeriodMs;
    return static_cast<std::uint8_t>(rising ? ramp : 255 - ramp);
}

std::uint8_t PmGreet::promptAlpha(std::uint64_t enteredMs, std::uint64_t timeMs) {
    // Which phase of the two-phase cycle, and how far into it.
    const std::uint64_t elapsed = timeMs > enteredMs ? timeMs - enteredMs : 0;
    const bool rising = (elapsed / kBlinkPeriodMs) % 2 == 0;
    return blinkAlpha(rising, elapsed % kBlinkPeriodMs);
}

} // namespace coney::gui
