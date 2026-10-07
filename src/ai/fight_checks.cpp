// SPDX-License-Identifier: GPL-3.0-or-later
#include "ai/fight_checks.h"

#include <algorithm>

namespace coney::ai {

namespace {

// The ring's outer edge as a share of the far range, and its least width.
constexpr float kOuterShare = 0.95F;
constexpr float kLeastWidth = 0.1F;
// Reason 5's ring for a non-cop: 3 × far to 3 × far + 1 m.
constexpr float kPoliceRingFar = 3.0F;
constexpr float kPoliceRingWidth = 1.0F;
// The move's limits.
constexpr std::uint32_t kRingLimitMs = 2000;
constexpr std::uint32_t kPoliceRingLimitMs = 6000;
// The fight goal's range, as a share of the far range (the taunt needs T beyond half of it).
constexpr float kFightRangeShare = 1.1F;
// Reason 7's taunt chance, percent.
constexpr int kBossHeldTauntPercent = 11;

} // namespace

int checkAttack(const CheckAttackInput& input, const std::function<bool()>& claimPlace) {
    if (!input.cooledDown) {
        return check::kCoolingDown;
    }
    if (!input.kindChosen) {
        return check::kNoKind;
    }
    if (!input.attackable) {
        return check::kNotAttackable;
    }
    // A charge needs no place.
    if (input.chargeOutOfStance) {
        return check::kGo;
    }
    if (input.armedNearLeader) {
        return check::kArmedNearLeader;
    }
    if (input.mateGoesFirst) {
        return check::kMateFirst;
    }
    if (input.policeHaveHim) {
        return check::kPolice;
    }
    if (input.friendInLine) {
        return check::kFriendInLine;
    }
    if (input.bossHeldByPlayer) {
        return check::kBossHeld;
    }
    if (claimPlace && claimPlace()) {
        return check::kGo;
    }
    // A grab from behind needs no place.
    if (input.grabKind) {
        return input.behindTarget ? check::kGo : check::kNoKind;
    }
    return check::kNoKind;
}

bool closeIn(const RepositionInput& input) {
    const bool unarmedMatch = input.targetEmptyHanded || (input.bothArmed && input.fewSlotHolders);
    const bool reasonAllows =
        input.reason != check::kBossHeld && input.reason != check::kMateFirst && input.reason != check::kNotAttackable;
    return unarmedMatch && reasonAllows && (input.targetFacesMe || input.firstOnFreeTarget);
}

RepositionRing repositionRing(const RepositionInput& input) {
    RepositionRing ring;
    if (input.reason == check::kPolice && !input.cop) {
        ring.inner = kPoliceRingFar * input.farRange;
        ring.outer = ring.inner + kPoliceRingWidth;
        ring.limitMs = kPoliceRingLimitMs;
        return ring;
    }
    ring.inner =
        closeIn(input) || input.tacticRing ? 2.0F * input.targetRadius : (input.nearRange + input.farRange) / 2.0F;
    ring.outer = std::max(kOuterShare * input.farRange, ring.inner + kLeastWidth);
    ring.limitMs = input.reason == check::kPolice ? kPoliceRingLimitMs : kRingLimitMs;
    return ring;
}

bool repositionTaunts(const RepositionInput& input, float distance, int slotsOnMe, bool gesturesAllowed, int roll100) {
    const float fightRange = kFightRangeShare * input.farRange;
    const bool farEnough = distance * distance >= fightRange * fightRange / 4.0F;
    const bool allowed = farEnough && slotsOnMe < 2 && !closeIn(input) && !input.tacticRing &&
                         input.targetEmptyHanded && gesturesAllowed;
    return allowed && (input.reason != check::kBossHeld || roll100 < kBossHeldTauntPercent);
}

void TackleMeter::think(bool pressing, int targetGait) {
    if (pressing) {
        m_value = std::min(kCap, m_value + std::min(targetGait, kCap));
    } else {
        m_value = std::max(0, m_value - 2);
    }
}

bool TackleMeter::ready(int gangTackle) const {
    // The value's thresholds: 7 − value sixes (value 1: the cap, which the meter never passes).
    constexpr int kSteps = 7;
    constexpr int kStep = 6;
    return gangTackle > 0 && m_value > (kSteps - gangTackle) * kStep;
}

} // namespace coney::ai
