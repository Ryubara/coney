// SPDX-License-Identifier: GPL-3.0-or-later
#include "graphics/screen_fade.h"

#include <algorithm>
#include <cmath>
#include <span>

#include "graphics/screen.h"

namespace coney::graphics {

void ScreenFade::queue(int type, double seconds, std::uint64_t /*nowMs*/) {
    if (type != kFadeIn && type != kFadeOut) {
        return;
    }
    m_from = type == kFadeIn ? 1.0F : 0.0F;
    m_to = type == kFadeIn ? 0.0F : 1.0F;
    // A fade out ends 0.2 s early when it can.
    double length = seconds;
    if (type == kFadeOut && length > kFadeOutShortening) {
        length -= kFadeOutShortening;
    }
    if (length <= 0.0) {
        m_level = m_to;
        m_state = State::Idle;
        m_durationMs = 0;
        return;
    }
    m_durationMs = static_cast<std::uint64_t>(std::llround(length * 1000.0));
    m_level = m_from;
    m_state = State::Requested;
}

void ScreenFade::update(std::uint64_t nowMs) {
    switch (m_state) {
    case State::Idle:
        return;
    case State::Requested:
        // The first frame only marks the fade running.
        m_state = State::Running;
        m_startMs = nowMs;
        return;
    case State::Running:
        break;
    }
    const std::uint64_t elapsed = nowMs > m_startMs ? nowMs - m_startMs : 0;
    if (elapsed >= m_durationMs) {
        m_level = m_to;
        m_state = State::Idle;
        m_durationMs = 0;
        return;
    }
    const float t = static_cast<float>(elapsed) / static_cast<float>(m_durationMs);
    m_level = std::clamp(m_from + ((m_to - m_from) * t), 0.0F, 1.0F);
}

void ScreenFade::draw(RenderDevice& device, float level) {
    if (level <= 0.0F) {
        return;
    }
    const auto alpha = static_cast<std::uint8_t>(std::clamp(std::lround(std::min(level, 1.0F) * 255.0F), 0L, 255L));
    drawWash(device, Rgba{0, 0, 0, alpha});
}

void ScreenFade::drawWash(RenderDevice& device, Rgba colour) {
    const LogicalQuad quad{0.0F, 0.0F, kLogicalWidth, kLogicalHeight, UvRect{}, colour};
    device.drawQuads(nullptr, std::span(&quad, 1));
}

} // namespace coney::graphics
