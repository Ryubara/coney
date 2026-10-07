// SPDX-License-Identifier: GPL-3.0-or-later
#include "ai/targeting.h"

#include <cstdint>
#include <memory>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "ai/ai_config.h"
#include "ai/brain.h"
#include "ai/brains.h"
#include "ai/gangs.h"
#include "human/human.h"
#include "human/humans.h"
#include "scripting/lua_value.h"
#include "scripting/script_bindings.h"
#include "support/collision_fixtures.h"
#include "support/fight_fixtures.h"
#include "support/human_fixtures.h"

// The enemy score and the pick of the best enemy (docs/research/ai.md#enemy-score): the terms, the ruled-out cases,
// the previous target's bonus, and the weights from the configuration script's calls. Synthetic humans on a floor.

using coney::ai::Brain;
using coney::ai::BrainType;
using coney::ai::kRuledOut;
using coney::ai::scoreEnemy;
using coney::human::Human;
namespace term = coney::ai::score_term;

namespace {

// Humans on an 80 m floor, each with a brain; no planner, so every straight line counts as walkable.
struct Scene {
    coney::test::FightCharacter character;
    std::unique_ptr<coney::raycast::CollisionMesh> mesh =
        coney::test::makeMesh(coney::test::floorAt(0.0F, 0.0F, 80.0F, 0.0F, 80.0F));
    std::vector<std::unique_ptr<Human>> humans;
    coney::ai::Brains brains;

    // A human with its feet at (x, y) facing `headingDegrees` (0 is +y), with a brain of `type`.
    Brain& add(float x, float y, float headingDegrees, BrainType type) {
        humans.push_back(std::make_unique<Human>(character.anims, coney::human::AnimSlots::player(),
                                                 coney::test::identityBind(), 1.0F, &character.ranges));
        Human& made = *humans.back();
        made.setFighterProfile(coney::human::FighterProfile{
            .player = type == BrainType::Player, .powerClass = coney::ai::kWarriorPowerClass, .health = 1400});
        made.spawn(mesh.get(), coney::anim::Vec3{x, y, 0.0F}, headingDegrees);
        return brains.add(made, type, coney::ai::FightSettings{}, static_cast<std::uint32_t>(humans.size()));
    }
};

} // namespace

TEST_CASE("the distance term pays 4 points a metre inside the sight range, and rules out one beyond it",
          "[ai][targeting]") {
    Scene scene;
    Brain& scorer = scene.add(40.0F, 40.0F, 0.0F, BrainType::Gang);
    Brain& near = scene.add(40.0F, 50.0F, 0.0F, BrainType::Gang);
    Brain& far = scene.add(40.0F, 75.0F, 0.0F, BrainType::Gang);
    // 10 m ahead in a 30 m sight range: 4 × 20.
    CHECK(scoreEnemy(scorer, near, term::kDistance) == Catch::Approx(80.0F));
    // 35 m: beyond the range.
    CHECK(scoreEnemy(scorer, far, term::kDistance) == kRuledOut);
    // Without the term the range does not matter.
    CHECK(scoreEnemy(scorer, far, 0) == Catch::Approx(0.0F));
}

TEST_CASE("an enemy out of view, a player, or one targeting the scorer changes the score", "[ai][targeting]") {
    Scene scene;
    Brain& scorer = scene.add(40.0F, 40.0F, 0.0F, BrainType::Gang);
    Brain& behind = scene.add(40.0F, 35.0F, 0.0F, BrainType::Gang);
    Brain& player = scene.add(40.0F, 45.0F, 0.0F, BrainType::Player);
    CHECK(scoreEnemy(scorer, behind, term::kOutOfView) == Catch::Approx(-5.0F));
    CHECK(scoreEnemy(scorer, player, term::kOutOfView | term::kPlayer) == Catch::Approx(20.0F));
    player.setTarget(&scorer);
    CHECK(scoreEnemy(scorer, player, term::kPlayer | term::kTargetsMe) == Catch::Approx(24.0F));
}

TEST_CASE("the always-counting terms: not attackable, and the gang's chosen target", "[ai][targeting]") {
    Scene scene;
    Brain& scorer = scene.add(40.0F, 40.0F, 0.0F, BrainType::Gang);
    Brain& enemy = scene.add(40.0F, 45.0F, 0.0F, BrainType::Gang);
    const int gang = scene.brains.gangs().create(19, "Hunters");
    const int theirs = scene.brains.gangs().create(19, "Hunted");
    scene.brains.gangs().addMember(gang, scorer);
    scene.brains.gangs().addMember(theirs, enemy);
    scene.brains.gangs().setAttackable(theirs, false);
    CHECK_FALSE(enemy.attackable());
    CHECK(scoreEnemy(scorer, enemy, 0) == Catch::Approx(-10.0F));
    scene.brains.gangs().find(gang)->setChosenTarget(&enemy);
    CHECK(scoreEnemy(scorer, enemy, 0) == Catch::Approx(390.0F));
    // Gone: no gang keeps him as its target.
    scene.brains.gangs().forget(enemy);
    CHECK(scene.brains.gangs().find(gang)->chosenTarget() == nullptr);
}

TEST_CASE("an enemy whose attack slots are full to nearer attackers is ruled out", "[ai][targeting]") {
    Scene scene;
    Brain& target = scene.add(40.0F, 40.0F, 0.0F, BrainType::Player);
    target.setAttackSlotCount(1);
    Brain& holder = scene.add(40.0F, 42.0F, 180.0F, BrainType::Gang);
    Brain& farther = scene.add(40.0F, 46.0F, 180.0F, BrainType::Gang);
    holder.setTarget(&target);
    REQUIRE(holder.hasAttackSlot());
    CHECK_FALSE(coney::ai::canTakeSlotOn(farther, target));
    CHECK(scoreEnemy(farther, target, term::kMelee) == kRuledOut);
    // A nearer one may take the slot from the holder.
    Brain& nearer = scene.add(40.0F, 41.0F, 180.0F, BrainType::Gang);
    CHECK(coney::ai::canTakeSlotOn(nearer, target));
}

TEST_CASE("the pick takes the best score above 0, keeps the previous target on a near tie and skips the invalid",
          "[ai][targeting]") {
    Scene scene;
    Brain& brain = scene.add(40.0F, 40.0F, 0.0F, BrainType::Gang);
    Brain& a = scene.add(40.0F, 50.0F, 0.0F, BrainType::Gang); // 10 m: 80
    Brain& b = scene.add(41.0F, 50.0F, 0.0F, BrainType::Gang); // 10.05 m: 79.8
    brain.addEnemy(b);
    brain.addEnemy(a);
    CHECK(coney::ai::pickBestEnemy(brain, nullptr, term::kDistance) == &a);
    CHECK(brain.target() == &a);
    // With b the previous target, its 3 points win.
    brain.setTarget(&b);
    CHECK(coney::ai::pickBestEnemy(brain, nullptr, term::kDistance) == &b);
    // An untargetable enemy is not chosen.
    b.human().script().targetable = false;
    CHECK(coney::ai::pickBestEnemy(brain, nullptr, term::kDistance) == &a);
    // Nothing above 0: no target.
    brain.setTarget(nullptr);
    CHECK(coney::ai::pickBestEnemy(brain, nullptr, term::kOutOfView) == nullptr);
    CHECK(brain.target() == nullptr);
}

TEST_CASE("the score weights come from the configuration script's CfgSetTargetingPoints calls", "[ai][targeting]") {
    coney::script::RecordedCalls recorded;
    using coney::script::Value;
    const std::vector<Value> plain{2.5, 7.0, -6.0, 2.0, 3.0, 99.0, 5.0, 8.0, 4.0};
    const std::vector<Value> extra{1.5, 9.0, 10.0, 11.0, 12.0, 13.0, -14.0, -15.0, 16.0};
    recorded.add("CfgSetTargetingPoints", plain);
    recorded.add("CfgSetTargetingPointsEx", extra);
    const coney::ai::TargetingPoints points = coney::ai::aiConfigFrom(recorded).settings.targeting;
    CHECK(points.perMetre == Catch::Approx(2.5F));
    CHECK(points.stunned == 7);
    CHECK(points.outOfView == -6);
    CHECK(points.down == 2);
    CHECK(points.grabbed == 3);
    CHECK(points.nearTrain == 5);
    CHECK(points.targetsMe == 8);
    CHECK(points.previousTarget == 4);
    CHECK(points.leaderPerMetre == Catch::Approx(1.5F));
    CHECK(points.enemyLeads == 9);
    CHECK(points.grabbedFromRear == 10);
    CHECK(points.running == 11);
    CHECK(points.tagging == 12);
    CHECK(points.targetsMeArmed == 13);
    CHECK(points.noWalkableLine == -14);
    CHECK(points.cannotChase == -15);
    CHECK(points.player == 16);
}
