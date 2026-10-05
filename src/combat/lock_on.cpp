// SPDX-License-Identifier: GPL-3.0-or-later
#include "combat/lock_on.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace coney::combat {

namespace {

constexpr float kPi = std::numbers::pi_v<float>;
// Degrees in one combat-walk sector.
constexpr float kSector = 45.0F;

} // namespace

bool lockedOn(const CombatTuning& tuning, bool hasTarget, bool l1Held) {
    return hasTarget &&
           ((l1Held && tuning.lockOnButton) || tuning.autoLock || tuning.autoLockAndCombat || tuning.autoCombat);
}

int combatWalkClip(float clockwiseDegrees) {
    float angle = std::fmod(clockwiseDegrees, 360.0F);
    if (angle < 0.0F) {
        angle += 360.0F;
    }
    const auto sector = static_cast<int>(std::floor((angle + (kSector / 2.0F)) / kSector)) % 8;
    return kCombatWalkFirst + sector;
}

bool keepsTarget(const CombatTuning& tuning, float distance, bool held) {
    return held || distance <= tuning.targetDropDistance;
}

float grabTurnStep(const CombatTuning& tuning, float remaining, float previous) {
    const float size = std::fabs(remaining);
    if (size < 1e-6F) {
        return 0.0F;
    }
    // The carried turn: kept at k, or pushed back when the turn reverses with little left.
    const bool reverses = previous != 0.0F && (previous > 0.0F) != (remaining > 0.0F) && size < kPi / 4.0F;
    const float carry = reverses ? tuning.grabTurnReverseCarry : tuning.grabTurnCarry;
    const float rate = std::min(tuning.grabTurnMax, (tuning.grabTurnMax * (1.0F - std::cos(size * kPi / 4.0F)) / 2.0F) +
                                                        (std::fabs(previous) * carry));
    const float step = std::clamp(rate, 0.0F, size);
    return remaining > 0.0F ? step : -step;
}

} // namespace coney::combat
