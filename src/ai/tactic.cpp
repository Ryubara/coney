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
    // The start, on the first process: the limit made absolute, every AI member flushed (its move stopped with it).
    if (!m_started) {
        m_endsAtMs = m_timeLimitMs < 0 ? std::numeric_limits<std::uint64_t>::max()
                                       : now + static_cast<std::uint64_t>(m_timeLimitMs);
        for (Brain* member : gang.members()) {
            if (member->type() != BrainType::Player) {
                member->flush();
                member->stopMove();
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

void Tactic::fireCallback(Gang& gang, int code) {
    ScriptServices* scripts = gang.owner().scripts();
    if (m_callback.empty() || scripts == nullptr) {
        return;
    }
    const std::array<double, 2> args{static_cast<double>(gang.id()), static_cast<double>(code)};
    scripts->call(m_callback, args);
}

} // namespace coney::ai
