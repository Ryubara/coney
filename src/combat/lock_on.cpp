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

// @orig 0x00241b90 Human_FightStanceMove (unknown)
bool lockedOn(const CombatTuning& tuning, bool hasTarget, bool l1Held) {
    return hasTarget &&
           ((l1Held && tuning.lockOnButton) || tuning.autoLock || tuning.autoLockAndCombat || tuning.autoCombat);
}

// @orig 0x00241b90 Human_FightStanceMove (unknown)
int combatWalkClip(float clockwiseDegrees) {
    float angle = std::fmod(clockwiseDegrees, 360.0F);
    if (angle < 0.0F) {
        angle += 360.0F;
    }
    const auto sector = static_cast<int>(std::floor((angle + (kSector / 2.0F)) / kSector)) % 8;
    return kCombatWalkFirst + sector;
}

// @coney-only stand-in: an AI human's target drop, Coney's reading (the original's rule for it is not traced)
bool keepsTarget(const CombatTuning& tuning, float distance, bool held) { return held || distance <= tuning.stanceMid; }

// @orig 0x00245310 Human_MoveGrabbing (unknown)
float grabTurnStep(const CombatTuning& tuning, float remaining, float previous) {
    const float size = std::fabs(remaining);
    if (size < 1e-6F) {
        return 0.0F;
    }
    // The carried turn: kept at k, or pushed back when the turn reverses with little left. **Coney stand-in** (#1011):
    // the original carries the rate before its cut to the angle left (+0x5e0) and tests the reverse against the last
    // angle left (+0x5e4); Coney carries the last step, cut and 0 once the heading is reached, so the turn starts
    // slower again after the pair has reached the stick's direction.
    const bool reverses = previous != 0.0F && (previous > 0.0F) != (remaining > 0.0F) && size < kPi / 4.0F;
    const float carry = reverses ? tuning.grabTurnReverseCarry : tuning.grabTurnCarry;
    const float rate = std::min(tuning.grabTurnMax, (tuning.grabTurnMax * (1.0F - std::cos(size * kPi / 4.0F)) / 2.0F) +
                                                        (std::fabs(previous) * carry));
    const float step = std::clamp(rate, 0.0F, size);
    return remaining > 0.0F ? step : -step;
}

} // namespace coney::combat
