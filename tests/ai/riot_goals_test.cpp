// SPDX-License-Identifier: GPL-3.0-or-later
// The fourth mission's goals (ai/riot_goals.h): a stationary thrower returns to its spot, faces the nearest enemy in
// range, plays its throw clip and waits; a rioter decides near a player (or at once when the player is outside its
// gang's turf), picks fights only as a gang soldier and the player only by its draw, counts its acts down, roams toward
// a far player, and leaves. Synthetic scenes.
#include "ai/riot_goals.h"

#include <algorithm>
#include <memory>
#include <utility>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "ai/brain.h"
#include "ai/goal.h"
#include "ai/move_action.h"
#include "animation/anim_math.h"
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

namespace {

// Riot services over `scene`: player 1's position, the given candidates, and counters for the acts and the leaving.
struct RiotHarness {
    int smashes = 0;
    int loots = 0;
    int left = 0;
    std::vector<Brain*> candidates;
    coney::ai::RiotServices services;

    RiotHarness(coney::test::AiScene& scene, std::vector<Brain*> chosen) : candidates(std::move(chosen)) {
        services.players = [&scene] { return std::vector<Vec3>{scene.player().human().position()}; };
        services.candidates = [this] { return candidates; };
        services.smash = [this](Brain& /*brain*/) { return ++smashes > 0; };
        services.loot = [this](Brain& /*brain*/) { return ++loots > 0; };
        services.leave = [this](Brain& /*brain*/) { return ++left > 0; };
    }
};

// The riot goal on `brain`'s stack (under any goal pushed over it).
const coney::ai::RiotGoal* riotOf(Brain& brain) {
    return dynamic_cast<const coney::ai::RiotGoal*>(brain.findGoal(GoalType::Riot));
}

// Whether `brain` picked a fight with `other`: it is among its enemies.
bool fought(const Brain& brain, const Brain& other) {
    return std::ranges::find(brain.enemies(), &other) != brain.enemies().end();
}

} // namespace

TEST_CASE("a gang rioter near a player picks a fight with a civilian and then leaves", "[ai][riot]") {
    coney::test::AiScene scene;
    Brain& rioter = scene.add(Vec3{40.0F, 46.0F, 0.0F}, 0.0F);
    Brain& civilian = scene.add(Vec3{42.0F, 48.0F, 0.0F}, 0.0F, BrainType::Civilian);
    RiotHarness harness(scene, {&civilian});
    REQUIRE(rioter.pushGoal(std::make_unique<coney::ai::RiotGoal>(
        coney::ai::RiotOrder{
            .radius = 60.0F, .actChance = 0, .acts = 2, .fightChance = 100, .playerFightChance = 0, .shout = false},
        harness.services)));
    // A decision at a 60th update, half the time: within a few the fight starts (the civilian is its enemy; Coney's
    // fight goal ends at once when he is out of melee range, the melee goals that would close in not being built).
    for (int k = 0; k < 20 && !riotOf(rioter)->decided(); ++k) {
        rioter.human().place(Vec3{40.0F, 46.0F, 0.0F}, 0.0F);
        scene.run(60);
    }
    REQUIRE(riotOf(rioter)->decided());
    CHECK(fought(rioter, civilian));
    CHECK(riotOf(rioter)->state() == coney::ai::RiotState::Leave);
}

TEST_CASE("a rioter whose nearest player is outside the gang's turf decides at once and leaves", "[ai][riot]") {
    coney::test::AiScene scene;
    Brain& rioter = scene.add(Vec3{40.0F, 46.0F, 0.0F}, 0.0F);
    Brain& civilian = scene.add(Vec3{42.0F, 48.0F, 0.0F}, 0.0F, BrainType::Civilian);
    RiotHarness harness(scene, {&civilian});
    // The turf: everything but where the player stands.
    harness.services.inTurf = [&scene](const Brain& /*brain*/, Vec3 point) {
        return coney::anim::distance(point, scene.player().human().position()) > 1.0F;
    };
    REQUIRE(rioter.pushGoal(std::make_unique<coney::ai::RiotGoal>(
        coney::ai::RiotOrder{
            .radius = 60.0F, .actChance = 100, .acts = 3, .fightChance = 100, .playerFightChance = 0, .shout = true},
        harness.services)));
    scene.run(5);
    // Outside the turf neither a fight nor an act is taken: the riot ends.
    CHECK(rioter.enemies().empty());
    REQUIRE(riotOf(rioter) != nullptr);
    CHECK(riotOf(rioter)->state() == coney::ai::RiotState::Leave);
    CHECK(harness.left > 0);
}

TEST_CASE("a civilian rioter picks no fight, and a decision with neither fight nor act ends the riot", "[ai][riot]") {
    coney::test::AiScene scene;
    Brain& rioter = scene.add(Vec3{40.0F, 46.0F, 0.0F}, 0.0F, BrainType::Civilian);
    Brain& civilian = scene.add(Vec3{42.0F, 48.0F, 0.0F}, 0.0F, BrainType::Civilian);
    RiotHarness harness(scene, {&civilian});
    REQUIRE(rioter.pushGoal(std::make_unique<coney::ai::RiotGoal>(
        coney::ai::RiotOrder{
            .radius = 60.0F, .actChance = 0, .acts = 1, .fightChance = 100, .playerFightChance = 100, .shout = true},
        harness.services)));
    for (int k = 0; k < 20 && !riotOf(rioter)->decided(); ++k) {
        scene.run(60);
    }
    REQUIRE(riotOf(rioter)->decided());
    scene.run(2);
    CHECK(rioter.enemies().empty());
    CHECK(riotOf(rioter)->state() == coney::ai::RiotState::Leave);
}

TEST_CASE("a rioter picks the player only when the player-fight draw allows it", "[ai][riot]") {
    for (const int chance : {0, 100}) {
        coney::test::AiScene scene;
        Brain& rioter = scene.add(Vec3{40.0F, 44.0F, 0.0F}, 0.0F);
        RiotHarness harness(scene, {&scene.player()});
        REQUIRE(rioter.pushGoal(std::make_unique<coney::ai::RiotGoal>(coney::ai::RiotOrder{.radius = 60.0F,
                                                                                           .actChance = 0,
                                                                                           .acts = 1,
                                                                                           .fightChance = 100,
                                                                                           .playerFightChance = chance,
                                                                                           .shout = false},
                                                                      harness.services)));
        // Each second the rioter is put back near the player, so the decision finds him in range.
        for (int k = 0; k < 20 && !riotOf(rioter)->decided(); ++k) {
            rioter.human().place(Vec3{40.0F, 44.0F, 0.0F}, 0.0F);
            scene.run(60);
        }
        REQUIRE(riotOf(rioter)->decided());
        CHECK(fought(rioter, scene.player()) == (chance == 100));
    }
}

TEST_CASE("a rioter's decided acts count down, each looking for a target, before it leaves", "[ai][riot]") {
    coney::test::AiScene scene;
    Brain& rioter = scene.add(Vec3{40.0F, 46.0F, 0.0F}, 0.0F);
    RiotHarness harness(scene, {});
    REQUIRE(rioter.pushGoal(std::make_unique<coney::ai::RiotGoal>(
        coney::ai::RiotOrder{
            .radius = 60.0F, .actChance = 100, .acts = 3, .fightChance = 0, .playerFightChance = 0, .shout = false},
        harness.services)));
    // Init's free act (about half the time) is one look that does not count; then a decision and 3 looks.
    const int free = harness.smashes + harness.loots;
    for (int k = 0; k < 20 && !riotOf(rioter)->decided(); ++k) {
        scene.run(60);
    }
    scene.run(5);
    CHECK(riotOf(rioter)->state() == coney::ai::RiotState::Leave);
    CHECK(riotOf(rioter)->actsLeft() == 0);
    const int looks = harness.smashes + harness.loots - free;
    CHECK((looks == 3 || looks == 4));
}

TEST_CASE("a roaming rioter far from the player first heads for a point 15 m from him", "[ai][riot]") {
    coney::test::AiScene scene;
    Brain& rioter = scene.add(Vec3{40.0F, 70.0F, 0.0F}, 0.0F);
    RiotHarness harness(scene, {});
    REQUIRE(rioter.pushGoal(std::make_unique<coney::ai::RiotGoal>(
        coney::ai::RiotOrder{
            .radius = 1.0F, .actChance = 0, .acts = 1, .fightChance = 0, .playerFightChance = 0, .shout = false},
        harness.services)));
    const coney::ai::MoveAction* move = nullptr;
    for (int k = 0; k < 5 && move == nullptr; ++k) {
        scene.run(1);
        move = dynamic_cast<const coney::ai::MoveAction*>(rioter.frontAction());
    }
    REQUIRE(move != nullptr);
    CHECK(coney::anim::distance(move->request().point, scene.player().human().position()) ==
          Catch::Approx(coney::ai::kRiotTowardPlayerRange).margin(0.01));
    CHECK(move->request().gait == coney::ai::kRiotGait);
    CHECK(move->request().radius == coney::ai::kRiotArrival);
}
