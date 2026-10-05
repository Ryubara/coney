// SPDX-License-Identifier: GPL-3.0-or-later
#include "combat/grab.h"

#include <cmath>

namespace coney::combat {

namespace {

// Straight-line distance between two points.
float distanceBetween(const anim::Vec3& a, const anim::Vec3& b) {
    const float dx = a.x - b.x;
    const float dy = a.y - b.y;
    const float dz = a.z - b.z;
    return std::sqrt((dx * dx) + (dy * dy) + (dz * dz));
}

} // namespace

bool grabAllowed(std::uint32_t phaseFlags) { return (phaseFlags & kGrabRefusingPhases) == 0; }

float grabSearchRange(const AnimRangeList& ranges, GrabKind kind, float scale) {
    const int id = kind == GrabKind::Tackle ? anim_id::kTackleIntro : anim_id::kGrabIntro;
    return ranges.farRange(static_cast<std::size_t>(id)) * scale;
}

std::size_t nearestTarget(const anim::Vec3& from, std::span<const TargetCandidate> candidates, float range) {
    std::size_t best = kNoTarget;
    float bestDistance = range;
    for (std::size_t i = 0; i < candidates.size(); ++i) {
        if (!candidates[i].available) {
            continue;
        }
        const float distance = distanceBetween(from, candidates[i].position);
        if (distance <= bestDistance) {
            best = i;
            bestDistance = distance;
        }
    }
    return best;
}

int throwAttack(Side side, bool wallInReach) {
    switch (side) {
    case Side::Front:
        return wallInReach ? anim_id::kThrow2Front : anim_id::kThrow1Front;
    case Side::Right:
        return wallInReach ? anim_id::kThrow2Right : anim_id::kThrow1Right;
    case Side::Rear:
        return wallInReach ? anim_id::kThrow2Rear : anim_id::kThrow1Rear;
    case Side::Left:
        return wallInReach ? anim_id::kThrow2Left : anim_id::kThrow1Left;
    }
    return anim_id::kThrow1Front;
}

GrabOutcome updateGrab(const GrabInput& input, PowerMeter& power, const CombatTuning& tuning, CombatRandom& random) {
    GrabOutcome outcome;
    const float strikeCost = tuning.grabStrikeCost * kPlayerStrikeCostShare;
    switch (input.command) {
    case command::kSquarePressed:
        outcome.action = GrabAction::Strike;
        outcome.animId = random.coin() ? anim_id::kGrabComboStrike2 : anim_id::kGrabComboStrike1;
        outcome.powerSpent = power.spend(strikeCost);
        break;
    case command::kCrossLongHold:
        outcome.action = GrabAction::Strike;
        outcome.animId = anim_id::kGrabComboStrike3;
        outcome.powerSpent = power.spend(strikeCost);
        break;
    case command::kCrossSquare:
        if (power.fraction() > tuning.powerEndurance) {
            outcome.action = GrabAction::PowerStrike;
            outcome.animId = input.raging     ? anim_id::kGrabPower2Strike1
                             : input.fromRear ? anim_id::kGrabRearSpinVictim
                                              : anim_id::kGrabPower1Strike1;
        }
        break;
    case command::kTrianglePressed:
        if (input.victimMuggable) {
            outcome.action = GrabAction::Mug;
        }
        break;
    case command::kCirclePressed:
        // The stick decides between a throw and a spin.
        if (input.stick.magnitude() > kThrowStick) {
            outcome.action = GrabAction::Throw;
            outcome.animId = throwAttack(sideOf(input.stick.angleDegrees()), input.wallInReach);
            outcome.powerSpent = power.spend(tuning.powerEndurance);
        } else {
            outcome.action = GrabAction::Spin;
        }
        break;
    default:
        break;
    }
    return outcome;
}

} // namespace coney::combat
