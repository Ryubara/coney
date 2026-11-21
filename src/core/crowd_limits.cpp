// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/crowd_limits.h"

#include <algorithm>

namespace coney::crowd {

int scanTokens(std::size_t scanningBrains) {
    // The original's five for its sixty, then five per sixty rounded up, capped so a crowd's burst waits a step.
    const std::size_t perSixty =
        ((scanningBrains * static_cast<std::size_t>(kOrigScanTokens)) + kOrigMaxHumans - 1) / kOrigMaxHumans;
    const std::size_t tokens =
        std::clamp(perSixty, static_cast<std::size_t>(kOrigScanTokens), static_cast<std::size_t>(kCrowdScanTokenCap));
    return static_cast<int>(tokens);
}

std::size_t humanListCap(std::size_t humans) { return std::max(humans, kOrigMaxHumans); }

} // namespace coney::crowd
