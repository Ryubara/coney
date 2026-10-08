// SPDX-License-Identifier: GPL-3.0-or-later
#include "combat/attack_target.h"

#include <catch2/catch_test_macros.hpp>

// Which target each attack goes for (docs/research/combat-moves.md#targeting).

using coney::combat::attackTarget;
using coney::combat::TargetSearch;
namespace id = coney::combat::anim_id;

TEST_CASE("square in the stance keeps the current target; its moving attacks and cross pick afresh at 2 m",
          "[combat][targets]") {
    CHECK(attackTarget(id::kAttackS1, false, false, false, true).search == TargetSearch::KeepCurrent);
    CHECK(attackTarget(id::kGroundedStrike1, false, false, false, false).search == TargetSearch::KeepCurrent);
    for (const int moving : {id::kAttackFromWalk, id::kAttackFromRun}) {
        const auto how = attackTarget(moving, false, false, false, false);
        CHECK(how.search == TargetSearch::Pick);
        CHECK(how.range == coney::combat::kAttackPickRange);
    }
    const auto cross = attackTarget(id::kAttackX1, true, false, false, true);
    CHECK(cross.search == TargetSearch::Pick);
    CHECK(cross.range == 2.0F);
}

TEST_CASE("armed swings pick at 2.5 m; the snaps search their own way", "[combat][targets]") {
    const auto armed = attackTarget(34, false, true, false, false);
    CHECK(armed.search == TargetSearch::Pick);
    CHECK(armed.range == 2.5F);
    CHECK(attackTarget(id::kSnapLeft, false, false, false, false).search == TargetSearch::Snap);
}

TEST_CASE("a cross chain step re-searches at its far range unless locked; a square step keeps the target",
          "[combat][targets]") {
    for (const int step : {id::kAttackSX2, id::kAttackXX2, id::kAttackSSX3}) {
        const auto free = attackTarget(step, false, false, true, false);
        CHECK(free.search == TargetSearch::FindAttack);
        CHECK(free.farId == step);
        CHECK(attackTarget(step, false, false, true, true).search == TargetSearch::KeepCurrent);
    }
    for (const int step : {id::kAttackSS2, id::kAttackXS2, id::kAttackSSS3}) {
        CHECK(attackTarget(step, false, false, true, false).search == TargetSearch::KeepCurrent);
    }
}

TEST_CASE("the special searches at the charge's far range", "[combat][targets]") {
    for (const int special : {id::kSpecial, id::kSpecialRage, id::kSpecialRear, id::kSpecialRageRear}) {
        const auto how = attackTarget(special, false, false, false, false);
        CHECK(how.search == TargetSearch::FindAttack);
        CHECK(how.farId == id::kRunningAttackCharge);
    }
}
