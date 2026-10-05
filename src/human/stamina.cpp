// SPDX-License-Identifier: GPL-3.0-or-later
#include "human/stamina.h"

#include <algorithm>
#include <cmath>

namespace coney::human {

StaminaTuning& staminaTuning() {
    static StaminaTuning tuning;
    return tuning;
}

Stamina::Stamina(int maximum) : m_value(std::max(maximum, 0)), m_maximum(std::max(maximum, 0)) {}

int Stamina::change(float points) {
    // **Coney's choice**: one fraction carried, as the original keeps one per meter (`+0x158`); it starts again
    // from 0 when the direction changes, so a refill never pays back a drain's remainder.
    if ((points > 0.0F) != (m_fraction > 0.0F) && m_fraction != 0.0F) {
        m_fraction = 0.0F;
    }
    const float total = m_fraction + points;
    const float whole = std::trunc(total);
    m_fraction = total - whole;
    const int applied = static_cast<int>(whole);
    m_value = std::clamp(m_value + applied, 0, m_maximum);
    return applied;
}

bool Stamina::drain(Gait gait, bool busy, float seconds) {
    if (gait != Gait::Sprint || busy || m_value == 0) {
        return false;
    }
    change(-staminaTuning().drainPerSecond * seconds);
    if (m_value <= 0) {
        m_value = 0;
        m_fraction = 0.0F;
        return true;
    }
    return false;
}

void Stamina::refill(const RefillBlocks& blocks, float seconds) {
    // Nothing is gained, and no time is carried, at the sprint gait, in the air, or running with L2 held.
    if (blocks.gait == Gait::Sprint || blocks.airborne || (blocks.gait == Gait::Run && blocks.sprintHeld)) {
        return;
    }
    if (m_value >= m_maximum) {
        m_fraction = 0.0F;
        return;
    }
    change(staminaTuning().refillPerSecond * seconds);
}

void Stamina::fill() {
    m_value = m_maximum;
    m_fraction = 0.0F;
}

bool sprintAsked(bool l2Held, int stamina, bool forbidden) { return l2Held && stamina != 0 && !forbidden; }

} // namespace coney::human
