// SPDX-License-Identifier: GPL-3.0-or-later
#include "ai/tactic.h"

#include <array>
#include <cstdint>
#include <limits>

#include "ai/gangs.h"
#include "ai/script_services.h"

namespace coney::ai {

int Tactic::process(Gang& gang) {
    const std::uint64_t now = gang.owner().nowMs();
    // The start, on the first process: the limit made absolute; every AI member's goal base marked over the goals he
    // has (a script's goal given in the same step stays under the tactic's) and his target dropped. Nothing is popped.
    if (!m_started) {
        m_endsAtMs = m_timeLimitMs < 0 ? std::numeric_limits<std::uint64_t>::max()
                                       : now + static_cast<std::uint64_t>(m_timeLimitMs);
        for (Brain* member : gang.members()) {
            if (member->type() != BrainType::Player) {
                member->markGoalBase();
                member->setTarget(nullptr);
            }
        }
        start(gang);
        m_started = true;
    }
    if (now > m_endsAtMs) {
        return 2;
    }
    return update(gang);
}

void Tactic::finish(Gang& gang) {
    end(gang);
    // Every AI member back down to his goal base: the tactic's goals and any pushed since its start go.
    for (Brain* member : gang.members()) {
        if (member->type() != BrainType::Player) {
            member->popToGoalBase();
        }
    }
}

void Tactic::fireCallback(Gang& gang, int code) {
    ScriptServices* scripts = gang.owner().scripts();
    if (m_callback.empty() || scripts == nullptr) {
        return;
    }
    const std::array<double, 2> args{static_cast<double>(gang.id()), static_cast<double>(code)};
    scripts->call(m_callback, args);
}

} // namespace coney::ai
