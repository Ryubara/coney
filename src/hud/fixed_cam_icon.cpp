// SPDX-License-Identifier: GPL-3.0-or-later
#include "hud/fixed_cam_icon.h"

namespace coney::hud {

bool FixedCamIcon::stickPushed(const Pad& pad) {
    // The raw right-stick bytes (right x, right y), as the camera's right-stick step reads them.
    const auto pushed = [](std::uint8_t byte) { return byte < kStickPushLow || byte > kStickPushHigh; };
    return pushed(pad.rawSticks()[0]) || pushed(pad.rawSticks()[1]);
}

void FixedCamIcon::update(bool ignoresStick, const Pad* pad, std::uint64_t nowMs) {
    // On a camera that ignores the stick, a push shows the icon and cancels its fade; any other camera hides it.
    if (ignoresStick) {
        if (pad != nullptr && stickPushed(*pad)) {
            m_shown = true;
            m_fadeEndMs = 0;
        }
    } else {
        m_shown = false;
        m_fadeEndMs = 0;
    }
    // A shown icon starts its 1 s fade when none runs (so a held stick keeps restarting it) and hides when it ends.
    if (m_shown) {
        if (m_fadeEndMs == 0) {
            m_fadeEndMs = nowMs + kFixedCamFadeMs;
        } else if (nowMs >= m_fadeEndMs) {
            m_shown = false;
            m_fadeEndMs = 0;
        }
    }
}

float FixedCamIcon::alpha(std::uint64_t nowMs) const {
    if (!m_shown) {
        return 0.0F;
    }
    if (m_fadeEndMs == 0 || nowMs >= m_fadeEndMs) {
        return m_fadeEndMs == 0 ? 1.0F : 0.0F;
    }
    // BaseWidget_RenderAlpha: the time left over the fade's length.
    return static_cast<float>(m_fadeEndMs - nowMs) / static_cast<float>(kFixedCamFadeMs);
}

void FixedCamIcon::render(const HudCanvas& canvas, std::uint64_t nowMs) const {
    graphics::SpriteBatch* parts = canvas.parts;
    if (!m_enabled || !m_shown || parts == nullptr || kFixedCamRect >= parts->sheet().page.rects.size()) {
        return;
    }
    const graphics::UvRect uv = parts->sheet().page.rect(kFixedCamRect);
    const float width = squareTexelWidth(parts->sheet(), uv, kFixedCamSize);
    parts->addSprite(
        guiSprite(kFixedCamPlace.x, kFixedCamPlace.y, width, kFixedCamSize, uv, faded(kFixedCamColour, alpha(nowMs))));
}

} // namespace coney::hud
