// SPDX-License-Identifier: GPL-3.0-or-later
// What the level scripts do to a fighter beyond its flags: the revive and the return to the normal state
// (docs/references/bindings/character.md#hurevive, docs/references/bindings/character.md#husetnormalmode).
#include <algorithm>

#include "human/fighter.h"

namespace coney::human {

void Fighter::revive(HumanAnimator& animator) {
    // Full health (0x0022eb40 with full = 1), the down and stunned states over, and the idle.
    m_health.set(m_health.maximum());
    setNormal(animator, true);
}

void Fighter::setNormal(HumanAnimator& animator, bool full) {
    // A grab ends: the victim it holds stands where it is; a grab holding it lets go.
    if (m_held != nullptr) {
        releaseHold(animator, false);
    }
    m_catch.reset();
    m_grabbed.reset();
    m_holdState.reset();
    m_holdAttached = false;
    m_tacklePending = false;
    m_mountPending = false;
    // The stun, the ground and rage end; whatever it was doing is lost.
    m_victim.endStun();
    m_combat.rage().stop();
    m_combat.interrupt();
    m_combat.release();
    m_notice.reset();
    m_slideUpdates = 0;
    m_steer.clear();
    if (!full) {
        return;
    }
    // In full: up from the ground with at least 1 health, and the state machine back to the idle.
    m_victim.clear();
    m_health.set(std::max(m_health.value(), 1));
    m_reacting = false;
    animator.stopToIdle();
}

} // namespace coney::human
