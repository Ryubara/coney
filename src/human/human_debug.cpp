// SPDX-License-Identifier: GPL-3.0-or-later
// What the Entity editor and the Movement tunables change on a human (docs/guides/debug-menu.md#entity-editor): the
// gait speed factors, and the meters as percentages. Coney's own debug tooling; every default leaves the human as the
// original has it.

#include <algorithm>
#include <cmath>

#include "human/human.h"
#include "human/locomotion.h"

namespace coney::human {

void Human::refreshSpeedFactors() {
    // The player's gait scales (the Movement tunables) and this human's own scale; the combat walk is the entity scale
    // alone, as its speed is a tunable of its own (Combat walk speed).
    const LocomotionTuning& tuning = locomotionTuning();
    const bool player = m_profile.player;
    const auto factor = [this, player](float gaitScale) { return (player ? gaitScale : 1.0F) * m_speedScale; };
    m_animator.setSpeedFactors(Speeds{.base = m_speedScale,
                                      .sneak = factor(tuning.sneakSpeedScale),
                                      .walk = factor(tuning.walkSpeedScale),
                                      .jog = factor(tuning.jogSpeedScale),
                                      .run = factor(tuning.runSpeedScale),
                                      .sprint = factor(tuning.sprintSpeedScale)});
}

void Human::setSpeedScale(float scale) {
    m_speedScale = scale < 0.0F ? 0.0F : scale;
    refreshSpeedFactors();
}

void Human::setHealthShare(float percent) {
    combat::Health& health = m_fighter.health();
    const float share = std::clamp(percent, 0.0F, 100.0F) / 100.0F;
    health.set(std::max(1, static_cast<int>(std::lround(static_cast<float>(health.maximum()) * share))));
}

void Human::setStaminaShare(float percent) {
    const float share = std::clamp(percent, 0.0F, 100.0F) / 100.0F;
    m_stamina.set(static_cast<int>(std::lround(static_cast<float>(m_stamina.maximum()) * share)));
}

void Human::setPowerShare(float percent) {
    combat::PowerMeter& power = m_fighter.combat().power();
    const float share = std::clamp(percent, 0.0F, 100.0F) / 100.0F;
    power.set(static_cast<int>(std::lround(static_cast<float>(power.maximum()) * share)));
}

void Human::setRageShare(float percent) { setRageFraction(std::clamp(percent, 0.0F, 100.0F) / 100.0F); }

} // namespace coney::human
