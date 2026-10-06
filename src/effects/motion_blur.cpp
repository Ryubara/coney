// SPDX-License-Identifier: GPL-3.0-or-later
#include "effects/motion_blur.h"

#include <algorithm>
#include <cmath>

namespace coney::effects {

namespace {

// One channel `t` of the way from `from` to `to`, rounded.
std::uint8_t mix(std::uint8_t from, std::uint8_t to, float t) {
    const float value = static_cast<float>(from) + (static_cast<float>(to) - static_cast<float>(from)) * t;
    return static_cast<std::uint8_t>(std::clamp(std::lround(value), 0L, 255L));
}

} // namespace

void MotionBlur::queueAlpha(std::uint8_t alpha, float seconds) {
    Colour target = current();
    target.a = alpha;
    queueColour(target, seconds);
}

void MotionBlur::queueColour(Colour target, float seconds) {
    // A new blend starts from wherever the last one has got to.
    m_from = current();
    m_to = target;
    m_seconds = std::max(seconds, 0.0F);
    m_elapsed = 0.0F;
}

void MotionBlur::step(float seconds) { m_elapsed = std::min(m_elapsed + seconds, m_seconds); }

MotionBlur::Colour MotionBlur::current() const {
    if (m_seconds <= 0.0F || m_elapsed >= m_seconds) {
        return m_to;
    }
    const float t = m_elapsed / m_seconds;
    return Colour{mix(m_from.r, m_to.r, t), mix(m_from.g, m_to.g, t), mix(m_from.b, m_to.b, t),
                  mix(m_from.a, m_to.a, t)};
}

} // namespace coney::effects
