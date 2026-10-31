// SPDX-License-Identifier: GPL-3.0-or-later
#include "graphics/level_lighting.h"

#include <algorithm>
#include <cstdint>

namespace coney::graphics {

namespace {

// One fog component as a byte: × 255, truncated as Float_ToUInt (0x0042c718) does (a negative number is 0, one past
// the 32-bit range all ones), then narrowed to its low byte, so a component above 1 wraps rather than saturates.
// @orig 0x0040c868 Level_SetFogColour (unknown)
std::uint8_t fogByte(float value) {
    const float scaled = value * 255.0F;
    if (!(scaled > 0.0F)) {
        return 0;
    }
    if (scaled >= 4294967296.0F) {
        return 0xff;
    }
    return static_cast<std::uint8_t>(static_cast<std::uint32_t>(scaled));
}

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
