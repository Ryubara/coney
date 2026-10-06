// SPDX-License-Identifier: GPL-3.0-or-later
#include "ai/idle_goals.h"

#include "ai/attack_kinds.h"
#include "ai/brain.h"

namespace coney::ai {

GoalStatus IdleGoal::process(Brain& brain) {
    if (brain.actionCount() == 0) {
        brain.stopMove();
    }
    return GoalStatus::Stop;
}

void SpectateGoal::start(Brain& brain) {
    m_untilMs = brain.nowMs() + static_cast<std::uint64_t>(rollRange(brain.random(), m_minMs, m_maxMs));
}

GoalStatus SpectateGoal::process(Brain& brain) {
    if (brain.nowMs() >= m_untilMs) {
        return GoalStatus::Done;
    }
    if (brain.actionCount() == 0) {
        brain.stopMove();
    }
    return GoalStatus::Stop;
}

} // namespace coney::ai
