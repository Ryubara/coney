// SPDX-License-Identifier: GPL-3.0-or-later
#include "combat/lock_on.h"

#include <cmath>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "combat/combat_tuning.h"

// When the fight stance locks onto its target, the combat-walk clip by the stick's angle, the target's drop distance,
// and the grabbing player's stick turn (docs/research/combat.md#targets, docs/research/combat.md#grab-turn).

using Catch::Approx;
using namespace coney::combat;

TEST_CASE("the street locks onto any target; with only CfgLockOn L1 alone does not lock", "[combat]") {
    CombatTuning street;
    CHECK(lockedOn(street, true, false));
    CHECK_FALSE(lockedOn(street, false, true));
    // CfgAutoLockAndCombat written to 0, as in the runtime test: L1 held gives a target but no lock.
    street.autoLockAndCombat = false;
    CHECK_FALSE(lockedOn(street, true, true));
    street.lockOnButton = true;
    CHECK(lockedOn(street, true, true));
    CHECK_FALSE(lockedOn(street, true, false));
}

TEST_CASE("the combat-walk clip follows the stick's clockwise angle from the facing", "[combat]") {
    // The runtime's readings: 18° and 354° forward, 27° forward right, 79° and 88° right, 120° and 143° back right,
    // 185° back, 329° forward left.
    CHECK(combatWalkClip(18.0F) == 380);
    CHECK(combatWalkClip(354.0F) == 380);
    CHECK(combatWalkClip(27.0F) == 381);
    CHECK(combatWalkClip(79.0F) == 382);
    CHECK(combatWalkClip(88.0F) == 382);
    CHECK(combatWalkClip(120.0F) == 383);
    CHECK(combatWalkClip(143.0F) == 383);
    CHECK(combatWalkClip(185.0F) == 384);
    CHECK(combatWalkClip(-90.0F) == 386);
    CHECK(combatWalkClip(329.0F) == 387);
}

TEST_CASE("a target beyond 2.5 m is dropped unless L1 or a hold keeps it", "[combat]") {
    const CombatTuning tuning;
    CHECK(keepsTarget(tuning, 2.4F, false));
    CHECK_FALSE(keepsTarget(tuning, 2.6F, false));
    CHECK(keepsTarget(tuning, 4.0F, true));
}

TEST_CASE("the grab's turn starts slow, reaches 0.192 rad an update and eases into the heading", "[combat]") {
    const CombatTuning tuning;
    // 118° to turn from rest: about 0.1 rad, then the carried share brings it to the limit.
    float left = 118.0F * 3.14159265F / 180.0F;
    float previous = 0.0F;
    float total = 0.0F;
    int updates = 0;
    float largest = 0.0F;
    while (left > 0.001F && updates < 100) {
        const float step = grabTurnStep(tuning, left, previous);
        CHECK(step <= tuning.grabTurnMax + 1e-6F);
        CHECK(step <= left + 1e-6F);
        largest = std::fmax(largest, step);
        left -= step;
        total += step;
        previous = step;
        ++updates;
    }
    CHECK(largest == Approx(0.192F));
    CHECK(total == Approx(118.0F * 3.14159265F / 180.0F).margin(0.002F));
    // About 18 updates at runtime.
    CHECK(updates >= 12);
    CHECK(updates <= 30);
    // A turn the other way keeps its sign.
    CHECK(grabTurnStep(tuning, -1.0F, 0.0F) < 0.0F);
}
