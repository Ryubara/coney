// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/game_timer.h"

#include <algorithm>

namespace coney {

std::uint64_t GameTimer::update(std::uint64_t realElapsedTicks) {
    // TODO(docs/research/boot.md#timers): the page records the pause check for the fixed step only; Coney stops the
    // clock in both modes until the real-time path is described.
    if (m_paused) {
        return 0;
    }
    const std::uint64_t step = m_fixedStep ? kFixedStepTicks : std::min(realElapsedTicks, kMaxRealStepTicks);
    m_ticks += step;
    return step;
}

} // namespace coney
