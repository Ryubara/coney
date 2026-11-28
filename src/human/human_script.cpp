// SPDX-License-Identifier: GPL-3.0-or-later
// What the level scripts do to a human (docs/references/bindings/character.md): its health, its normal state, the
// arrest, the wound and the rage meter's lock; and the throw aim's stance (docs/research/objects.md#throws). The flags
// and the rest of the scripts' state are set directly (human/human.h).

#include "human/human.h"

#include <algorithm>
#include <array>
#include <cstdint>

#include "core/ps2_float.h"
#include "human/fighter_clips.h"
#include "human/locomotion.h"

namespace coney::human {

void Human::setHealthPercent(float percent) {
    combat::Health& health = m_fighter.health();
    if (percent <= 0.0F || percent > 100.0F) {
        health.set(health.maximum());
        return;
    }
    // A hundredth of the maximum first, then the percentage, each product a single-precision float rounded toward
    // zero as the PS2's FPU rounds it, then cut to whole points. 0.01 as a float is a little under a hundredth, so
    // the products land just under the exact value: 35 % of 900 is 314, and even 100 % of 900 is 899
    // (docs/references/bindings/character.md#husethealthpercent, run r-20261025-health-percent-rounding).
    constexpr float kHundredth = 0.01F;
    const float hundredth = ps2::mul(static_cast<float>(health.maximum()), kHundredth);
    health.set(static_cast<int>(ps2::mul(percent, hundredth)));
}

void Human::revive() {
    // Down before the heal: knocked out, or out of health with the death chain still playing.
    const bool down = knockedOut() || m_fighter.health().depleted();
    heal(true);
    // State 0x20000000: the anim state chosen again, which stands a woken human. Coney stand-in: a human still in his
    // death chain is stood up too, where the original keeps the chain and knocks him out at its end (#685), because
    // Coney's chain end reads the health, which would leave him lying neither knocked out nor up.
    if (down) {
        m_fighter.standUp(m_animator);
    }
}

void Human::heal(bool full) {
    // Coney stand-in: byte +0x3be, whose reader is not known, is not kept.
    if (m_script.wounded) {
        endWounded();
    }
    if (knockedOut()) {
        // Human_WakeUp, then Human_EnterFightStance. A knock-out by health ends with the health below. Coney stand-in
        // (#959): an AI human's stance is its fight goal's (Fighter::setAiStance()), so the stance shows for a player.
        m_script.knockedOut = false;
        m_fighter.setStanceOn(true);
    }
    // Coney stand-in: state 0x4000000000 is not kept.
    combat::Health& health = m_fighter.health();
    const int maximum = health.maximum();
    health.set(full ? maximum : std::min(health.value() + (maximum >> 1), maximum));
    heal();
    // Not ported: the linked object's release (#1640) and the glow colour (+0x644) zeroed (#685).
}

void Human::setNormalMode(bool full) {
    // Not ported (#685): a human with a hold flag drops its overlay task.
    // Humans_EndAllRage: a player's rage ends; the other players' are the caller's.
    if (m_fighter.player()) {
        setRageMode(false);
    }
    // Coney stand-in: no fire or mini-game is modelled, so there is none to end. A throw's aim is cancelled.
    setThrowAiming(false);
    // Not ported (#685): the glow colour (+0x644) zeroed.
    // The pair and down states: a pair is broken only in full; a wound ends. Coney stand-in: no mini-game, special
    // state, workout, mugging, tag or tackle hold is modelled here to end.
    if (full) {
        breakPair();
    }
    if (m_script.wounded) {
        endWounded();
    }
    // The grab, the get-up, the stun, the fight stance and, in full, the health and the idle (Fighter::setNormal()).
    m_fighter.setNormal(m_animator, full);
    // Coney stand-in: no Coney human holds an object of kind 0x27 or 0x10 to drop. The carried props go.
    releaseCarriedProps();
    // The players' slow motion ends (takeSlowMotionReset()).
    m_slowMotionReset = true;
    // Coney stand-in: in full the airborne bits, the climb's body and a picked object are not kept apart here.
}

void Human::setArrested(bool arrested) {
    if (arrested) {
        arrest();
    } else {
        unarrest();
    }
}

void Human::arrest() {
    // Coney's choice: every Coney human has a body, so the bodiless branch (only the state, the style and action 0)
    // never runs. Already cuffed: nothing.
    if (m_script.arrested) {
        return;
    }
    loseBlockHumans();
    setThrowAiming(false);
    // Not ported (#711): the drop of a held weapon of kind 4 or 6, or of anything held without flag 0x2000.
    if (m_fighter.victim().stunned() && !m_fighter.victim().grounded()) {
        m_fighter.endStun(nowMs());
    }
    // Cuffed: push weight 1e9 (pushWeight()). Coney stand-in: block 0x8000 is not kept apart, the cuffed idle ending
    // any block. Style 0x11 loops 320 and the action is 0, which stops the human where he stands.
    m_script.arrested = true;
    m_velocity.x = 0.0F;
    m_velocity.y = 0.0F;
    m_animator.playCombat(clips::kNoClips, clips::kArrestedIdle, AnimState::Hold);
}

void Human::unarrest() {
    if (!m_script.arrested) {
        return;
    }
    // Body flags 0x80000004: BLOCKHUMANS back.
    m_blockHumansFlag = true;
    releaseCarriedProps();
    // Style 0x11 removed, no longer cuffed (push weight 1), and the anim state chosen again: the idle.
    m_script.arrested = false;
    m_animator.stopToIdle();
}

void Human::setThrowAiming(bool aiming) {
    if (aiming == m_script.throwAiming) {
        return;
    }
    m_script.throwAiming = aiming;
    m_velocity.x = 0.0F;
    m_velocity.y = 0.0F;
    m_aimTurning = false;
    if (!aiming) {
        m_animator.endHold();
        return;
    }
    static constexpr std::array<std::uint32_t, 1> kEnter{clips::kThrowAimEnter};
    m_animator.playCombat(kEnter, clips::kThrowAimCycle, AnimState::Hold);
}

void Human::aimThrow(float heading, bool turning) {
    if (!m_script.throwAiming) {
        return;
    }
    m_heading = wrapAngle(heading);
    if (turning != m_aimTurning) {
        m_aimTurning = turning;
        m_animator.playCombat(clips::kNoClips, turning ? clips::kThrowAimTurn : clips::kThrowAimCycle, AnimState::Hold);
    }
}

void Human::setWounded(bool wounded) {
    // Coney stand-in: the binding reads `on` as a boolean, where the original starts the wound only for exactly 1; and
    // the wounded clips (style 0xd, #990) are not played, so choosing the anim state again (0x20000000) changes
    // nothing.
    if (wounded) {
        startWounded();
    } else {
        endWounded();
    }
}

void Human::startWounded() {
    // How long the wound stamp reaches past now (`0x00510794`).
    constexpr std::uint64_t kWoundMs = 14000;
    // States 0x14800f: the stance, the stun and a knock-out end.
    m_fighter.setStanceOn(false);
    m_fighter.endStun(nowMs());
    m_script.knockedOut = false;
    if (!m_script.wounded) {
        // Human_ReleaseAll: the pair broken, the grab and rage ended. Not ported: the held object's drop (#711) and
        // the wounded style 0xd (#990).
        breakPair();
        m_fighter.setNormal(m_animator, false);
        setRageMode(false);
        m_script.woundedUntilMs = nowMs() + kWoundMs;
        combat::Health& health = m_fighter.health();
        health.set(health.maximum() / 4);
        loseBlockHumans();
    }
    // Wounded: push weight 1e9 (pushWeight()). Coney stand-in: the brain's knocked-out event is the caller's flush.
    m_script.wounded = true;
}

void Human::endWounded() {
    // No longer wounded: push weight 1 (pushWeight()). Not ported: style 0xd's removal (#990) and body shape
    // 2's enabling (#685).
    m_script.wounded = false;
}

void Human::setRageLocked(bool locked) {
    setFlag(flag::kRageLocked, locked);
    m_fighter.combat().rage().setLocked(locked, nowMs());
}

void Human::setRageMode(bool on) {
    combat::RageMeter& rage = m_fighter.combat().rage();
    if (on) {
        rage.force(nowMs());
    } else if (rage.raging()) {
        rage.stop();
    }
}

} // namespace coney::human
