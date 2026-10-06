// SPDX-License-Identifier: GPL-3.0-or-later
// The fourth mission's goals (ai/riot_goals.h): a stationary thrower returns to its spot, faces the nearest enemy in
// range, plays its throw clip and waits; a rioter roams, picks fights only as a gang member with a civilian near a
// player, and leaves once it has. Synthetic scenes.
#include "ai/riot_goals.h"

#include <memory>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "ai/brain.h"
#include "ai/goal.h"
#include "support/ai_fixtures.h"

using coney::ai::Brain;
using coney::ai::BrainType;
using coney::ai::GoalType;
using coney::anim::Vec3;

TEST_CASE("a stationary thrower faces the nearest enemy, plays its throw and waits its delay", "[ai][riot]") {
    coney::test::AiScene scene;
    Brain& thrower = scene.add(Vec3{20.0F, 20.0F, 0.0F}, 0.0F);
    Brain& enemy = scene.add(Vec3{20.0F, 26.0F, 0.0F}, 180.0F);
    thrower.addEnemy(enemy);
    REQUIRE(thrower.pushGoal(std::make_unique<coney::ai::StationaryThrowerGoal>(
        2, coney::ai::ThrowerObjects{7, 8, 0, 0, 0, 0, 0, 0}, scene.services)));
    const auto* goal = dynamic_cast<const coney::ai::StationaryThrowerGoal*>(thrower.topGoal());
    REQUIRE(goal != nullptr);
    CHECK(goal->type() == GoalType::StationaryThrower);
    CHECK(goal->objects()[1] == 8);
    scene.run(10);
    // It already faces the enemy: the throw clip plays once, then it waits 2 to 3 s.
    CHECK(goal->throws() == 1);
    CHECK(scene.services.clips == std::vector<int>{coney::ai::kThrowAnim});
    scene.run(30);
    CHECK(goal->throws() == 1);
    CHECK(goal->state() == coney::ai::ThrowerState::Wait);
    scene.run(90);
    CHECK(goal->throws() == 2);
}

TEST_CASE("a stationary thrower with no enemy in three quarters of its sight throws nothing", "[ai][riot]") {
    coney::test::AiScene scene;
    Brain& thrower = scene.add(Vec3{20.0F, 20.0F, 0.0F}, 0.0F);
    Brain& enemy = scene.add(Vec3{20.0F, 20.0F + (thrower.sightRange() * 0.8F), 0.0F}, 180.0F);
    thrower.addEnemy(enemy);
    REQUIRE(thrower.pushGoal(
        std::make_unique<coney::ai::StationaryThrowerGoal>(1, coney::ai::ThrowerObjects{}, scene.services)));
    scene.run(30);
    CHECK(scene.services.clips.empty());
}

TEST_CASE("a gang rioter near a player picks a fight with a civilian and then leaves", "[ai][riot]") {
    coney::test::AiScene scene;
    Brain& rioter = scene.add(Vec3{40.0F, 46.0F, 0.0F}, 0.0F);
    Brain& civilian = scene.add(Vec3{42.0F, 48.0F, 0.0F}, 0.0F, BrainType::Civilian);
    int left = 0;
    coney::ai::RiotServices services{
        .players = [&scene] { return std::vector<Vec3>{scene.player().human().position()}; },
        .candidates = [&civilian] { return std::vector<Brain*>{&civilian}; },
        .leave = [&left](Brain& /*brain*/) { ++left; },
    };
    REQUIRE(rioter.pushGoal(std::make_unique<coney::ai::RiotGoal>(
        coney::ai::RiotOrder{
            .radius = 10.0F, .actChance = 100, .acts = 2, .fightChance = 100, .gangFightChance = 0, .shout = false},
        services)));
    // A decision every 60 updates, half the time: within a few the fight starts.
    scene.run(600);
    CHECK(rioter.findGoal(GoalType::Fight) != nullptr);
    const auto* riot = dynamic_cast<const coney::ai::RiotGoal*>(rioter.findGoal(GoalType::Riot));
    REQUIRE(riot != nullptr);
    CHECK(riot->state() == coney::ai::RiotState::Leave);
}

TEST_CASE("a civilian rioter, or one far from every player, picks no fight", "[ai][riot]") {
    coney::test::AiScene scene;
    Brain& civilianRioter = scene.add(Vec3{40.0F, 46.0F, 0.0F}, 0.0F, BrainType::Civilian);
    Brain& farRioter = scene.add(Vec3{40.0F, 75.0F, 0.0F}, 0.0F);
    Brain& civilian = scene.add(Vec3{42.0F, 48.0F, 0.0F}, 0.0F, BrainType::Civilian);
    Brain& other = scene.add(Vec3{42.0F, 74.0F, 0.0F}, 0.0F, BrainType::Civilian);
    coney::ai::RiotServices services{
        .players = [&scene] { return std::vector<Vec3>{scene.player().human().position()}; },
        .candidates = [&civilian, &other] { return std::vector<Brain*>{&civilian, &other}; },
        .leave = [](Brain& /*brain*/) {},
    };
    const coney::ai::RiotOrder order{
        .radius = 10.0F, .actChance = 0, .acts = 1, .fightChance = 100, .gangFightChance = 100, .shout = true};
    REQUIRE(civilianRioter.pushGoal(std::make_unique<coney::ai::RiotGoal>(order, services)));
    REQUIRE(farRioter.pushGoal(std::make_unique<coney::ai::RiotGoal>(order, services)));
    scene.run(600);
    CHECK(civilianRioter.findGoal(GoalType::Fight) == nullptr);
    CHECK(farRioter.findGoal(GoalType::Fight) == nullptr);
    const auto* riot = dynamic_cast<const coney::ai::RiotGoal*>(farRioter.topGoal());
    REQUIRE(riot != nullptr);
    CHECK(riot->state() == coney::ai::RiotState::Roam);
}
