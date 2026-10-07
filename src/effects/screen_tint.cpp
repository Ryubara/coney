// SPDX-License-Identifier: GPL-3.0-or-later
#include "effects/screen_tint.h"

#include <algorithm>
#include <cmath>

namespace coney::effects {

namespace {

// One channel `t` of the way from `from` to `to`: from × (1 − t) + to × t in floats, truncated.
// @orig 0x0017a258 Colour_LerpPacked (unknown)
std::uint8_t mix(std::uint8_t from, std::uint8_t to, float t) {
    const float value = static_cast<float>(from) * (1.0F - t) + static_cast<float>(to) * t;
    return static_cast<std::uint8_t>(std::clamp(static_cast<int>(value), 0, 255));
}

} // namespace

std::uint8_t ScreenTint::deviceAlphaOf(std::uint8_t alpha) {
    return static_cast<std::uint8_t>(std::clamp(std::lround(opacityOf(alpha) * 255.0F), 0L, 255L));
}

std::uint8_t ScreenTint::byteOf(double component) {
    // Float_ToUInt of the component × 255; **Coney's choice**: kept to its low byte, a negative as 0.
    const double scaled = std::trunc(component * 255.0);
    return scaled <= 0.0 ? 0 : static_cast<std::uint8_t>(static_cast<std::uint32_t>(scaled) & 0xFFU);
}

void ScreenTint::setLevelColour(Colour colour) {
    m_level = colour;
    m_look = kLevelLook;
    blendTo(m_level, 0.0F);
}

void ScreenTint::enterStore(Colour colour) {
    m_store = colour;
    m_look = kStoreLook;
    blendTo(m_store, kStoreBlendSeconds);
}

void ScreenTint::exitStore() {
    m_look = kLevelLook;
    blendTo(m_level, kStoreBlendSeconds);
}

void ScreenTint::blendTo(Colour target, float seconds) {
    // A new blend starts from wherever the last one has got to.
    m_from = current();
    m_to = target;
    m_seconds = std::max(seconds, 0.0F);
    m_elapsed = 0.0F;
}

void ScreenTint::step(float seconds) { m_elapsed = std::min(m_elapsed + seconds, m_seconds); }

ScreenTint::Colour ScreenTint::current() const {
    if (m_seconds <= 0.0F || m_elapsed >= m_seconds) {
        return m_to;
    }
    const float t = m_elapsed / m_seconds;
    return Colour{mix(m_from.r, m_to.r, t), mix(m_from.g, m_to.g, t), mix(m_from.b, m_to.b, t),
                  mix(m_from.a, m_to.a, t)};
}

} // namespace coney::effects
