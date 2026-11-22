// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/crowd_limits.h"

#include <algorithm>

namespace coney::crowd {

int scanTokens(std::size_t scanningBrains, EngineLimits limits) {
    // The engine limits preset: the original's five, however many ask.
    if (limits == EngineLimits::Original) {
        return kOrigScanTokens;
    }
    // The original's five for its sixty, then five per sixty rounded up, capped so a crowd's burst waits a step.
    const std::size_t perSixty =
        ((scanningBrains * static_cast<std::size_t>(kOrigScanTokens)) + kOrigMaxHumans - 1) / kOrigMaxHumans;
    const std::size_t tokens =
        std::clamp(perSixty, static_cast<std::size_t>(kOrigScanTokens), static_cast<std::size_t>(kCrowdScanTokenCap));
    return static_cast<int>(tokens);
}

std::size_t humanListCap(std::size_t humans, EngineLimits limits) {
    // The engine limits preset: the original's 60, however many humans there are.
    return limits == EngineLimits::Original ? kOrigMaxHumans : std::max(humans, kOrigMaxHumans);
}

} // namespace coney::crowd
