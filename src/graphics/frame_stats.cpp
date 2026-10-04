// SPDX-License-Identifier: GPL-3.0-or-later
#include "graphics/frame_stats.h"

#include <cstddef>

#include "core/assert.h"

namespace coney::graphics {

FrameStats summarizeFrame(std::span<const std::uint8_t> rgba, Rgba background) {
    CONEY_ASSERT(rgba.size() % 4 == 0);
    FrameStats stats;
    stats.pixels = rgba.size() / 4;
    // FNV-1a: small, dependency-free and good enough to tell two frames apart in a log.
    constexpr std::uint64_t kFnvOffset = 0xcbf29ce484222325ULL;
    constexpr std::uint64_t kFnvPrime = 0x100000001b3ULL;
    stats.hash = kFnvOffset;
    for (std::size_t i = 0; i < rgba.size(); i += 4) {
        if (rgba[i] != background.r || rgba[i + 1] != background.g || rgba[i + 2] != background.b) {
            ++stats.notBackground;
        }
        for (std::size_t k = 0; k < 4; ++k) {
            stats.hash = (stats.hash ^ rgba[i + k]) * kFnvPrime;
        }
    }
    return stats;
}

} // namespace coney::graphics
