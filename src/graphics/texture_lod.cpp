// SPDX-License-Identifier: GPL-3.0-or-later
#include "graphics/texture_lod.h"

#include <algorithm>
#include <cmath>

namespace coney::graphics {

float TextureLod::at(float depth) const {
    // The GS takes log2 of 1/Q, which is never zero for a vertex in front of the camera; clamp so log2 stays finite.
    constexpr float kNearest = 1.0F / 1024.0F;
    return std::log2(std::max(depth, kNearest)) * std::ldexp(1.0F, l) + k;
}

TextureLod unpackTextureLod(std::uint32_t kl) {
    // 12 bits of K, two's complement, sign-extended, then four bits of fraction.
    const int raw = static_cast<int>(kl & 0xFFFU);
    const int signedK = raw >= 0x800 ? raw - 0x1000 : raw;
    return TextureLod{.k = static_cast<float>(signedK) / 16.0F, .l = static_cast<int>((kl >> 12U) & 0x3U)};
}

} // namespace coney::graphics
