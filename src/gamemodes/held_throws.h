// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

// Stub: which clips throw what a human holds, and when a throw lets go of it.

namespace coney {

[[nodiscard]] inline bool isHeldThrowClip(std::uint32_t /*animId*/) { return false; }

[[nodiscard]] inline bool releasesHeldObject(bool /*releaseEvent*/, std::uint32_t /*animId*/, double /*held*/) {
    return false;
}

} // namespace coney
