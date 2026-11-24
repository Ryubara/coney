// SPDX-License-Identifier: GPL-3.0-or-later

// The think step that turns a list of enemies into a fight: a gang soldier's step 3, a cop's step 3 and a Warrior's
// step 1 push the fight goals (Brain_PushFightGoal(-1)) when the brain has a threat response, no fight goal and an
// enemy listed. Synthetic humans on the test floor; no game data.

#include <array>

#include <catch2/catch_test_macros.hpp>

#include "ai/brain.h"
#include "ai/goal.h"
#include "support/ai_fixtures.h"

using coney::ai::Brain;
using coney::ai::BrainType;
using coney::ai::GoalType;

namespace {

// Whether `brain` has the fight's three goals: Goal_Melee's FindEnemy and Melee, and the fight goal on top.
bool fightPushed(Brain& brain) {
    return brain.findGoal(GoalType::FindEnemy) != nullptr && brain.findGoal(GoalType::Melee) != nullptr &&
           brain.findGoal(GoalType::Fight) != nullptr;
}

} // namespace

TEST_CASE("a gang soldier, a cop or a Warrior with an enemy listed starts a fight at his think", "[ai][think]") {
    // Expected: ai-brain-types.md#think-gang, confirmed (code)
    // Expected: ai-brain-types.md#think-cop, confirmed (code)
    // Expected: ai-brain-types.md#think-warrior, confirmed (code)
    // Expected: ai-brain-types.md#think-civilian, confirmed (code)
    for (const BrainType type : std::array{BrainType::Gang, BrainType::Cop, BrainType::Warrior}) {
        coney::test::AiScene scene;
        Brain& thinker = scene.add(coney::anim::Vec3{40.0F, 40.0F, 0.0F}, 0.0F, type);
        Brain& enemy = scene.add(coney::anim::Vec3{40.0F, 50.0F, 0.0F}, 180.0F, BrainType::Gang);
        // An empty list starts nothing.
        thinker.think(1000);
        CHECK(thinker.goalCount() == 0);
        // One enemy listed: the fight is pushed with no target picked (the Melee goal picks), at the next think.
        thinker.addEnemy(enemy);
        thinker.think(1200);
        CHECK(fightPushed(thinker));
        CHECK(thinker.target() == nullptr);
        CHECK(thinker.goalCount() == 3);
    }
    // Not a civilian's (his think only calms him down), nor a dead brain's (BrDead: its think handler is a stub).
    coney::test::AiScene scene;
    Brain& civilian = scene.add(coney::anim::Vec3{40.0F, 40.0F, 0.0F}, 0.0F, BrainType::Civilian);
    Brain& dead = scene.add(coney::anim::Vec3{44.0F, 40.0F, 0.0F}, 0.0F, BrainType::Gang);
    Brain& enemy = scene.add(coney::anim::Vec3{40.0F, 50.0F, 0.0F}, 180.0F, BrainType::Gang);
    civilian.addEnemy(enemy);
    dead.addEnemy(enemy);
    dead.setDead(true);
    civilian.think(1000);
    dead.think(1000);
    CHECK(civilian.goalCount() == 0);
    CHECK(dead.goalCount() == 0);
}

TEST_CASE("a think pushes no second fight, and none without a threat response", "[ai][think]") {
    // Expected: ai-brain-types.md#think-gang, confirmed (code)
    coney::test::AiScene scene;
    Brain& soldier = scene.add(coney::anim::Vec3{40.0F, 40.0F, 0.0F}, 0.0F, BrainType::Gang);
    Brain& enemy = scene.add(coney::anim::Vec3{40.0F, 50.0F, 0.0F}, 180.0F, BrainType::Gang);
    soldier.addEnemy(enemy);
    // Threat response 0 (GangSetThreatResponse 0): never fights.
    soldier.setThreatResponse(0);
    soldier.think(1000);
    CHECK(soldier.goalCount() == 0);
    // Back to 2: one fight, and a later think with Melee still on the stack leaves it as it is.
    soldier.setThreatResponse(2);
    soldier.think(1200);
    REQUIRE(fightPushed(soldier));
    soldier.popGoal();
    REQUIRE(soldier.goalCount() == 2);
    soldier.think(1400);
    CHECK(soldier.goalCount() == 2);
    CHECK(soldier.findGoal(GoalType::Fight) == nullptr);
}
