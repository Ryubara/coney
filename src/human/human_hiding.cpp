// SPDX-License-Identifier: GPL-3.0-or-later
// The hidden state of a human (state `0x200000`): entering it in the shadow, the grace after leaving it with a target,
// and what ends it on the human's side (a sprint, the fight stance). Who may hide and when is the ground rule's
// (ai/hiding.h).
// Research: docs/research/stealth.md#hidden, docs/research/stealth.md#sneaking
#include "human/human.h"

namespace coney::human {

namespace {

// The held object's anim sets that refuse hiding (Human_HoldsKind4Or6, 0x00224000): the overhead weapons and the
// ghetto set.
constexpr int kOverheadSet = 4;
constexpr int kGhettoSet = 6;

} // namespace

void Human::enterHiding() {
    // The grace's end is cleared on every call, refused or not, so a human back on the shadow before the grace runs
    // out stays hidden.
    m_hideGraceUntil.reset();
    // Not while already hidden, sprinting (state 0x1000000) or aiming a throw, nor holding an object of anim set 4 or
    // 6 (its type's +0x87, which the fighter's anim set is).
    const int heldSet = m_fighter.animSet();
    if (m_hidden || m_sprinting || m_script.throwAiming || heldSet == kOverheadSet || heldSet == kGhettoSet) {
        return;
    }
    // Out of the fight stance and its lock (a target kept no longer locks while hidden); then the state and the move
    // style. The aim needs no cancel: it refused above. **Coney stand-in**: the L1 target bit (state 0x8), the anim
    // state's rebuild (0x20000000) and the busy test before the style (0x7839e1f7ff0) are not built, so the style goes
    // on at once; the hidden loop line (Player_UpdateHiddenLoopSound) is not played.
    m_fighter.setLockBlocked(true);
    m_hidden = true;
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

// The stance's own update never enters the stance while hidden, but an attack, a grab or a tackle does, and entering
// it ends the hiding there (`Player_EnterStance`, 0x00280068; Human::fight()).
void Human::updateHiding() {
    if (!m_hidden) {
        return;
    }
    // A sprint (`Player_UpdateSprint`, 0x0027ce90) and the grace's end (`Human_UpdateActions`, 0x00254e78) end it.
    // The fight stance (`Player_EnterStance`, 0x00280068) would too, but hidden the target does not lock, so the
    // human never enters it by itself. The grace is over once its end is below the time, not on its last millisecond.
    const bool graceOver = m_hideGraceUntil && *m_hideGraceUntil < nowMs();
    if (m_sprinting || graceOver) {
        clearHiding();
    }
}

} // namespace coney::human
