// SPDX-License-Identifier: GPL-3.0-or-later
#include "graphics/level_lighting.h"

#include <algorithm>

namespace coney::graphics {

namespace {

// One fog component as a byte: × 255, truncated, kept to 0-255.
// @orig 0x0040c868 Level_SetFogColour (unknown)
std::uint8_t fogByte(float value) { return static_cast<std::uint8_t>(std::clamp(value * 255.0F, 0.0F, 255.0F)); }

} // namespace

Rgba fogColourOf(float r, float g, float b) { return Rgba{fogByte(r), fogByte(g), fogByte(b), 255}; }

void ShadowLight::setColour(float r, float g, float b) {
    colour = LightColour{r, g, b, 1.0F};
    scripted = true;
}

void ShadowLight::setOffset(float x, float y, float z) {
    offset = world::Vec3{x, y, z};
    scripted = true;
}

void ShadowCasters::setEnabled(std::uint32_t object, bool enable) {
    const auto at = std::ranges::lower_bound(m_disabled, object);
    const bool listed = at != m_disabled.end() && *at == object;
    if (!enable && !listed) {
        m_disabled.insert(at, object);
    } else if (enable && listed) {
        m_disabled.erase(at);
    }
}

bool ShadowCasters::enabled(std::uint32_t object) const { return !std::ranges::binary_search(m_disabled, object); }

} // namespace coney::graphics
