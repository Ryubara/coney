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
    // Spends `fraction` of the meter, except in rage.
    const auto spend = [&](float fraction) { return input.raging ? 0 : power.spend(fraction); };
    // A power move goes ahead with more than the endurance fraction (or in rage), else the grab is released.
    const auto powerMove = [&](GrabAction action, int animId) {
        if (!input.raging && power.fraction() <= tuning.powerEndurance) {
            outcome.action = GrabAction::Release;
            return;
        }
        outcome.action = action;
        outcome.animId = animId;
        outcome.powerSpent = spend(tuning.powerEndurance);
    };
    // A move with a partner point needs the victim in its place: otherwise nothing happens.
    const bool placed = input.victimInPlace;
    switch (input.command) {
    case command::kSquarePressed:
        if (!placed) {
            break;
        }
        outcome.action = GrabAction::Strike;
        outcome.animId = random.coin() ? anim_id::kGrabComboStrike2 : anim_id::kGrabComboStrike1;
        outcome.powerSpent = spend(tuning.grabStrikeCost * kPlayerStrikeCostShare);
        break;
    case command::kCrossLongHold:
        if (!placed) {
            break;
        }
        outcome.action = GrabAction::Strike;
        outcome.animId = anim_id::kGrabComboStrike3;
        outcome.powerSpent = spend(tuning.grabStrikeCost * kPlayerStrikeCostShare);
        break;
    case command::kCrossSquare:
    case command::kCircleCross: {
        if (!placed) {
            break;
        }
        const bool rageStrike = input.raging || input.command == command::kCircleCross;
        powerMove(GrabAction::PowerStrike, rageStrike ? anim_id::kGrabPower2Strike1 : anim_id::kGrabPower1Strike1);
        outcome.spinFirst = outcome.action == GrabAction::PowerStrike && input.fromRear;
        break;
    }
    case command::kTrianglePressed:
        if (input.victimMuggable) {
            outcome.action = GrabAction::Mug;
        }
        break;
    case command::kCirclePressed:
        // The stick decides between a throw and, from the rear, the spin to the front.
        if (input.stick.magnitude() > kThrowStick) {
            if (!placed) {
                break;
            }
            powerMove(GrabAction::Throw, throwAttack(sideOf(input.stick.angleDegrees()), input.wallInReach));
        } else if (input.fromRear) {
            outcome.action = GrabAction::Spin;
            outcome.animId = anim_id::kGrabSpinToFront;
        } else {
            // From the front, no check and no cost: the pair goes down to the mount.
            outcome.action = GrabAction::Mount;
            outcome.animId = anim_id::kGrabMount;
        }
        break;
    case command::kR1Pressed:
        outcome.action = GrabAction::Spin;
        outcome.animId = input.fromRear ? anim_id::kGrabSpinToFront : anim_id::kGrabSpinToRear;
        break;
    case command::kL2Held:
        outcome.action = GrabAction::LetGo;
        outcome.animId = anim_id::kGrabLetGo;
        break;
    default:
        break;
    }
    return outcome;
}

MountOutcome updateMount(const MountInput& input, PowerMeter& power, const CombatTuning& tuning, CombatRandom& random) {
    MountOutcome outcome;
    // Spends `fraction` of the meter, except in rage.
    const auto spend = [&](float fraction) { return input.raging ? 0 : power.spend(fraction); };
    switch (input.command) {
    case command::kSquarePressed:
        outcome.action = MountAction::Strike;
        outcome.animId = random.coin() ? anim_id::kMountStrike2 : anim_id::kMountStrike1;
        outcome.powerSpent = spend(tuning.grabStrikeCost * kPlayerStrikeCostShare);
        break;
    case command::kCrossLongHold:
        outcome.action = MountAction::Strike;
        outcome.animId = anim_id::kMountStrike3;
        outcome.powerSpent = spend(tuning.grabStrikeCost * kPlayerStrikeCostShare);
        break;
    case command::kCrossSquare:
        // A power strike needs more than the endurance fraction (or rage); short of it the player gets off.
        if (!input.raging && power.fraction() <= tuning.powerEndurance) {
            outcome.action = MountAction::GetOff;
            outcome.animId = anim_id::kMountRelease;
            break;
        }
        outcome.action = MountAction::PowerStrike;
        outcome.animId = input.raging ? anim_id::kMountPower2 : anim_id::kMountPower1;
        outcome.powerSpent = spend(tuning.powerEndurance);
        break;
    case command::kCirclePressed:
    case command::kCircleTapped:
    case command::kCircleHeld:
        outcome.action = MountAction::ToHold;
        outcome.animId = anim_id::kMountPickup;
        break;
    case command::kL2Held:
        outcome.action = MountAction::GetOff;
        outcome.animId = anim_id::kMountRelease;
        break;
    default:
        break;
    }
    return outcome;
}

} // namespace coney::combat
