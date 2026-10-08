// SPDX-License-Identifier: GPL-3.0-or-later
// The hidden state of a human (state `0x200000`): entering it in the shadow, the grace after leaving it with a target,
// and what ends it on the human's side (a sprint, the fight stance). Who may hide and when is the ground rule's
// (ai/hiding.h).
// Research: docs/research/stealth.md#hidden, docs/research/stealth.md#sneaking
#include "human/human.h"

namespace coney::human {

void Human::enterHiding() {
    // Not while already hidden or sprinting (state 0x1000000).
    if (m_hidden || m_sprinting) {
        return;
    }
    // Out of the fight stance and its lock (a target kept no longer locks while hidden); then the state and the move
    // style. **Coney's reading**: the throw aim and the busy test before the style are not built (Coney has no throw
    // aim yet), so the style goes on at once.
    m_fighter.setLockBlocked(true);
    m_hidden = true;
    m_hideGraceUntil.reset();
    m_animator.setStealthStyle(true);
}

void Human::leaveHiding() {
    if (!m_hidden) {
        return;
    }
    // Walking with a target, the state stays for the grace (one deadline: a later leave does not move it).
    if (gait() < Gait::Run && m_fighter.target() != nullptr) {
        if (!m_hideGraceUntil) {
            m_hideGraceUntil = nowMs() + kHideGraceMs;
        }
        return;
    }
    clearHiding();
}

void Human::clearHiding() {
    m_hidden = false;
    m_hideGraceUntil.reset();
    m_fighter.setLockBlocked(false);
    m_animator.setStealthStyle(false);
}

void Human::updateHiding() {
    if (!m_hidden) {
        return;
    }
    // A sprint (`Player_UpdateSprint`, 0x0027ce90) and the grace's end (`Human_UpdateActions`, 0x00254e78) end it.
    // The fight stance (`Player_EnterStance`, 0x00280068) would too, but hidden the target does not lock, so the
    // human never enters it by itself.
    const bool graceOver = m_hideGraceUntil && nowMs() >= *m_hideGraceUntil;
    if (m_sprinting || graceOver) {
        clearHiding();
    }
}

} // namespace coney::human
