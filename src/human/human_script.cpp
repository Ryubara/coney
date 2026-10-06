// SPDX-License-Identifier: GPL-3.0-or-later
// What the level scripts do to a human (docs/references/bindings/character.md): its health, its normal state, the
// arrest and the rage meter's lock. The flags and the rest of the scripts' state are set directly (human/human.h).

#include "human/human.h"

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
    // Arrested, it stops and whatever it was doing ends; released, it stands up again into the idle.
    m_velocity.x = 0.0F;
    m_velocity.y = 0.0F;
    m_fighter.setNormal(m_animator, false);
    if (!arrested) {
        m_animator.stopToIdle();
    }
}

void Human::setRageLocked(bool locked) {
    setFlag(flag::kRageLocked, locked);
    m_fighter.combat().rage().setLocked(locked, nowMs());
}

} // namespace coney::human
