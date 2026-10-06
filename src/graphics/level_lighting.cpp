// SPDX-License-Identifier: GPL-3.0-or-later
#include "graphics/level_lighting.h"

#include <algorithm>

namespace coney::graphics {

namespace {

// One fog component as a byte: × 255, truncated, kept to 0-255.
std::uint8_t fogByte(float value) { return static_cast<std::uint8_t>(std::clamp(value * 255.0F, 0.0F, 255.0F)); }

} // namespace

Rgba fogColourOf(float r, float g, float b) { return Rgba{fogByte(r), fogByte(g), fogByte(b), 255}; }

} // namespace coney::graphics
