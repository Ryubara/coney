// SPDX-License-Identifier: GPL-3.0-or-later
#include "graphics/screen_fade.h"

#include <algorithm>
#include <cmath>
#include <span>

#include "graphics/screen.h"

namespace coney::graphics {

void ScreenFade::queue(int type, double seconds, std::uint64_t nowMs) {
    if (type != kFadeIn && type != kFadeOut) {
        return;
    }
    m_from = type == kFadeIn ? 1.0F : 0.0F;
    m_to = type == kFadeIn ? 0.0F : 1.0F;
    m_startMs = nowMs;
    m_durationMs = seconds > 0.0 ? static_cast<std::uint64_t>(std::llround(seconds * 1000.0)) : 0;
    m_level = m_from;
    m_running = true;
    update(nowMs);
}

void ScreenFade::update(std::uint64_t nowMs) {
    if (!m_running) {
        return;
    }
    const std::uint64_t elapsed = nowMs > m_startMs ? nowMs - m_startMs : 0;
    if (elapsed >= m_durationMs) {
        m_level = m_to;
        m_running = false;
        return;
    }
    const float t = static_cast<float>(elapsed) / static_cast<float>(m_durationMs);
    m_level = m_from + ((m_to - m_from) * t);
}

void ScreenFade::draw(RenderDevice& device, float level) {
    if (level <= 0.0F) {
        return;
    }
    const auto alpha = static_cast<std::uint8_t>(std::clamp(std::lround(level * 255.0F), 0L, 255L));
    const LogicalQuad quad{0.0F, 0.0F, kLogicalWidth, kLogicalHeight, UvRect{}, Rgba{0, 0, 0, alpha}};
    device.drawQuads(nullptr, std::span(&quad, 1));
}

} // namespace coney::graphics
