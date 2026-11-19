// SPDX-License-Identifier: GPL-3.0-or-later
#include "ai/tactic.h"

#include <array>
#include <cstdint>
#include <limits>

#include "ai/gangs.h"
#include "ai/script_services.h"
#include "ai/sub_tactics.h"
#include "core/assert.h"

namespace coney::ai {

int Tactic::process(Gang& gang) {
    const std::uint64_t now = gang.owner().nowMs();
    // The start, on the first process: the limit made absolute; every AI member's goal base marked over the goals he
    // has (a script's goal given in the same step stays under the tactic's) and his target dropped. Nothing is popped.
    if (!m_started) {
        m_endsAtMs = m_timeLimitMs < 0 ? std::numeric_limits<std::uint64_t>::max()
                                       : now + static_cast<std::uint64_t>(m_timeLimitMs);
        // The alert state from the vtable `+0x3c` flag, always set: a gang tactic (type below 0x12) puts the gang in
        // the fighting state, any other calms it (docs/research/ai-reimpl.md#alert-state).
        gang.setAlertState(alertsGang() ? kGangFighting : kGangCalm);
        // Stand-in: types 0x16-0x18 and 0x21 also clear gang +0xd1 and arm +0xd2, the flags of the spot line, which
        // Coney does not build (TacticAttack's spot-line stand-in), so nothing would read them.
        for (Brain* member : gang.members()) {
            if (member->type() != BrainType::Player) {
                member->markGoalBase();
                const TargetCauseScope cause(*member, TargetCause::Order);
                member->setTarget(nullptr);
            }
        }
        start(gang);
        m_started = true;
    }
    if (now > m_endsAtMs) {
        return 2;
    }
    // The order it runs goes first, so an order ended now is gone before the class's update.
    updateStrategy(gang);
    return update(gang);
}

void Tactic::finish(Gang& gang) {
    // The order it runs ends first, while the members still hold its goals.
    endStrategy(gang);
    // Every AI member back down to his goal base: the tactic's goals and any pushed since its start go.
    for (Brain* member : gang.members()) {
        if (member->type() != BrainType::Player) {
            member->popToGoalBase();
        }
    }
    // Then the class's end, which sees the members' own goals back on top.
    end(gang);
}

void Tactic::fireCallback(Gang& gang, int code) {
    ScriptServices* scripts = gang.owner().scripts();
    if (m_callback.empty() || scripts == nullptr) {
        return;
    }
    const std::array<double, 2> args{static_cast<double>(gang.id()), static_cast<double>(code)};
    scripts->call(m_callback, args);
}

void Tactic::setStrategy(Gang& gang, std::optional<std::size_t> order) {
    endStrategy(gang);
    m_strategy = order;
}

void Tactic::updateStrategy(Gang& gang) {
    if (!m_strategy) {
        return;
    }
    SubTactic* order = gang.owner().subTactics().at(*m_strategy);
    CONEY_ASSERT(order != nullptr);
    if (order->process(gang, gang.owner().nowMs())) {
        endStrategy(gang);
    }
}

void Tactic::endStrategy(Gang& gang) {
    if (!m_strategy) {
        return;
    }
    SubTacticPool& pool = gang.owner().subTactics();
    SubTactic* order = pool.at(*m_strategy);
    CONEY_ASSERT(order != nullptr);
    // The order's end, then the class hears it while the slot still names it, then the record goes.
    order->finish(gang);
    strategyEnded(gang, *order);
    pool.free(*m_strategy);
    m_strategy.reset();
}

} // namespace coney::ai
