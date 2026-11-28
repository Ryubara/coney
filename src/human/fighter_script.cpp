// SPDX-License-Identifier: GPL-3.0-or-later
// What the level scripts do to a fighter beyond its flags: the stand after a revive and the fighter's steps of the
// normal mode (docs/research/characters-script.md#hu-revive, docs/research/scenes.md#join-reset).
#include "human/fighter.h"

namespace coney::human {

void Fighter::standUp(HumanAnimator& animator) {
    m_victim.clear();
    m_reacting = false;
    animator.stopToIdle();
}

void Fighter::setNormal(HumanAnimator& animator, bool full) {
    // A grab ends (Grab_EndBoth, Tackle_End): the victim it holds stands where it is; a grab holding it lets go.
    if (m_held != nullptr) {
        releaseHold(animator, false);
    }
    m_catch.reset();
    m_grabbed.reset();
    m_holdState.reset();
    m_holdAttached = false;
    m_tacklePending = false;
    m_mountPending = false;
    // Human_GetUp: a human down but not dying, dead or knocked out (his health left) is up.
    const bool down = m_victim.grounded() && !m_health.depleted();
    if (down) {
        m_victim.clear();
        m_reacting = false;
    }
    // Human_EndStun.
    m_victim.endStun();
    // Human_LeaveFightStance: a player leaves the stance and the lock and drops his target. Coney stand-in (#959): an
    // AI human's stance is its fight goal's (setAiStance()), which keeps it.
    if (m_player) {
        leaveStance();
        takeTarget(nullptr);
    }
    // Coney's choice: the move under way ends with the anim state chosen again (state 0x20000000), as Coney keeps it
    // apart from the state word.
    m_combat.interrupt();
    m_combat.release();
    m_notice.reset();
    m_slideUpdates = 0;
    m_steer.clear();
    if (!full) {
        // The anim state chosen again. Coney stand-in: only a human the get-up stood is put in his idle; a standing one
        // keeps what plays, as Coney's controller chooses again each update.
        if (down) {
            animator.stopToIdle();
        }
        return;
    }
    // In full: health at or below the pending damage (+0x118) becomes 1 and the pending damage 0, so a hit waiting to
    // kill does not; the dying state ends and the idle is built with action 0.
    if (m_health.value() <= m_victim.pendingDamage()) {
        m_health.set(1);
        m_victim.zeroPendingDamage();
    }
    m_victim.clear();
    m_reacting = false;
    animator.stopToIdle();
}

} // namespace coney::human
