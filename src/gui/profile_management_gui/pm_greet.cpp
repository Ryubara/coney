// SPDX-License-Identifier: GPL-3.0-or-later
#include "gui/profile_management_gui/pm_greet.h"

#include <array>

#include "core/pad.h"
#include "graphics/overlay_camera.h"

namespace coney::gui {

float PmGreet::logoHeight(float width) const {
    const graphics::SpriteBatch* batch = m_shared.menuSprites;
    if (batch == nullptr || kLogoRect >= batch->sheet().page.rects.size() || batch->sheet().texture == nullptr) {
        return width;
    }
    // The rectangle's shape in texels, kept on the 4:3 screen: a GUI width is W / H overlay units, a GUI height one.
    const graphics::UvRect& uv = batch->sheet().page.rect(kLogoRect);
    const float texelWidth = (uv.u1 - uv.u0) * static_cast<float>(batch->sheet().texture->width());
    const float texelHeight = (uv.v1 - uv.v0) * static_cast<float>(batch->sheet().texture->height());
    if (texelWidth <= 0.0F) {
        return width;
    }
    return graphics::OverlayCamera::guiWidthToOverlay(width) * texelHeight / texelWidth;
}

PmGreet::PmGreet(PmShared& shared) : m_shared(shared), m_logo(shared.menuSprites, kLogoRect) {}

void PmGreet::enter(ScreenFlowController& /*flow*/) {
    // Init: the logo and the prompt, placed by the layout. The original also resets the pad handlers here, which Coney
    // does not have yet.
    const PmLayout& layout = m_shared.layout;
    m_logo.init();
    m_logo.setBatch(m_shared.menuSprites);
    m_logo.setup(WidgetRect{layout.logoX, layout.logoY, layout.logoWidth, logoHeight(layout.logoWidth)},
                 graphics::kWhite, true);
    m_prompt.init();
    m_prompt.setText(m_shared.string(kPromptString));
    m_prompt.centreOn(0.5F, layout.promptY, layout.textBoxWidth);
    m_prompt.style().scale = layout.textScale;
    m_prompt.setFade(0.0F);
    m_enteredMs = m_shared.frame.timeMs;
    m_lastActivityMs = m_enteredMs;
}

int PmGreet::update() {
    const GuiFrame& frame = m_shared.frame;
    int result = kStay;

    // A screen fade in progress keeps the prompt fully lit and restarts the idle time; otherwise the prompt blinks
    // while the profile manager is not finishing.
    const bool fading = m_shared.fade != nullptr && m_shared.fade->active();
    if (fading) {
        m_prompt.setFade(1.0F);
        m_lastActivityMs = frame.timeMs;
    } else if (!m_shared.finishing) {
        m_prompt.setFade(static_cast<float>(promptAlpha(m_enteredMs, frame.timeMs)) / 255.0F);
    }
    m_prompt.update(frame);

    // START leads to the main menu.
    if (frame.pad != nullptr && (frame.pad->pressedWithRepeat() & pad::kStart) != 0) {
        result = kToMode;
        if (m_shared.playSound) {
            m_shared.playSound(kStartCue);
        }
    }

    // The idle time, restarted by a fade (above), not by the pad; the attract movie when it runs out.
    if (frame.timeMs - m_lastActivityMs >= kIdleMs) {
        if (m_shared.callScript) {
            const std::array<double, 1> movie{kAttractMovie};
            m_shared.callScript("Menu.playMovie", movie);
        }
        m_lastActivityMs = frame.timeMs;
    }

    // Render: the text, then the sprite (they go to different batches, so the order within the frame is the 2D pass's).
    m_prompt.render(m_shared.canvas);
    m_logo.render(m_shared.canvas);
    return result;
}

void PmGreet::exit() {
    m_prompt.shutdown();
    m_logo.shutdown();
}

std::uint8_t PmGreet::promptAlpha(std::uint64_t enteredMs, std::uint64_t timeMs) {
    const std::uint64_t phase = (timeMs - enteredMs) % (2 * kBlinkHalfMs);
    const std::uint64_t rising = phase < kBlinkHalfMs ? phase : 2 * kBlinkHalfMs - phase;
    return static_cast<std::uint8_t>(rising * 255 / kBlinkHalfMs);
}

} // namespace coney::gui
