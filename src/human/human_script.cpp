// SPDX-License-Identifier: GPL-3.0-or-later
// What the level scripts do to a human (docs/references/bindings/character.md): its health, its normal state, the
// arrest, the wound and the rage meter's lock; and the throw aim's stance (docs/research/objects.md#throws). The flags
// and the rest of the scripts' state are set directly (human/human.h).

#include "human/human.h"

#include <array>
#include <cstdint>

#include "human/fighter_clips.h"
#include "human/locomotion.h"

namespace coney::human {

void Human::setHealthPercent(float percent) {
    combat::Health& health = m_fighter.health();
    if (percent <= 0.0F || percent > 100.0F) {
        health.set(health.maximum());
        return;
    }
    health.set(static_cast<int>(static_cast<float>(health.maximum()) * percent / 100.0F));
}

void Human::setNormalMode(bool full) {
    m_script.arrested = false;
    m_fighter.setNormal(m_animator, full);
}

void Human::setArrested(bool arrested) {
    if (arrested == m_script.arrested) {
        return;
    }
    m_script.arrested = arrested;
    // Arrested, it stops and whatever it was doing ends; released, it stands up again into the idle. **Coney's
    // reading**: the push weight (1e9 while cuffed) is not kept, as Coney's bodies do not push each other yet.
    m_velocity.x = 0.0F;
    m_velocity.y = 0.0F;
    m_fighter.setNormal(m_animator, false);
    if (!arrested) {
        m_animator.stopToIdle();
        return;
    }
    // Move style 0x11's idle, 320 `ANIM_ARRESTED_IDLE`, loops until the release.
    m_animator.playCombat(clips::kNoClips, clips::kArrestedIdle, AnimState::Hold);
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
    // How long the wound stamp reaches past now (`0x00510794`).
    constexpr std::uint64_t kWoundMs = 14000;
    if (!wounded) {
        m_script.wounded = false;
        return;
    }
    if (m_script.wounded) {
        return;
    }
    m_script.wounded = true;
    m_script.woundedUntilMs = nowMs() + kWoundMs;
    // Its stance, grab or throw ends (state bits `0x14800f`), and its health drops to a quarter.
    m_fighter.setNormal(m_animator, false);
    combat::Health& health = m_fighter.health();
    health.set(health.maximum() / 4);
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
