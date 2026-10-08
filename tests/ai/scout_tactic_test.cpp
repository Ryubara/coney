// SPDX-License-Identifier: GPL-3.0-or-later
#include "ai/tactic_scout.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <optional>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "ai/gangs.h"
#include "ai/tactic_attack.h"
#include "scripting/story_bindings.h"
#include "support/ai_fixtures.h"

// TacticScout (docs/research/stealth.md#scouts): the guards' posts, the scan that spots the player, the fight and the
// run to the phone that calls the gang. Synthetic floor; the player (slot 0) in one gang, the guards in another.

using coney::ai::Brain;
using coney::ai::GoalType;
using coney::anim::Vec3;
using coney::test::AiScene;

namespace {

// A call to the gang the services record.
struct Called {
    int count = 0;
    int delaySeconds = 0;
    Vec3 point;
};

// The level's side of the scouts: a responder spawner when `ready`, a phone at `phone`, and the calls and wanted gangs
// recorded.
struct Level {
    coney::ai::ScoutServices services;
    std::vector<Called> calls;
    std::vector<int> wanted;

    Level(bool ready, std::optional<Vec3> phone) {
        services.responderReady = [ready] { return ready; };
        services.phone = [phone](Vec3 /*from*/, float /*radius*/) { return phone; };
        services.queueGangCall = [this](int count, int delay, Vec3 point) {
            calls.push_back(Called{count, delay, point});
        };
        services.secondWanted = [this](int gang) { return std::ranges::find(wanted, gang) != wanted.end(); };
        services.setSecondWanted = [this](int gang) { wanted.push_back(gang); };
    }
};

// The player's gang and the guards' gang, enemies; returns the guards' gang id and puts the player's in `players`.
int makeSides(AiScene& scene, const std::vector<Brain*>& guards, int& players) {
    auto& gangs = scene.brains.gangs();
    players = gangs.create(0, "Players");
    const int enemies = gangs.create(2, "Guards");
    gangs.addMember(players, scene.player());
    for (Brain* guard : guards) {
        gangs.addMember(enemies, *guard);
    }
    gangs.makeEnemies(players, enemies);
    return enemies;
}

// The scout call: 3 responders after 2 s, phones within 20 m, roaming 4 m.
coney::script::TacticCall scoutCall(int gang) {
    coney::script::TacticCall call;
    call.gang = gang;
    call.kind = coney::script::TacticKind::Scout;
    call.range = 20.0F;
    call.range2 = 4.0F;
    call.count = 3;
    call.count2 = 2;
    return call;
}

} // namespace

TEST_CASE("TacticScout posts each guard, and a guard pushed off his post walks back", "[ai][scout]") {
    AiScene scene;
    scene.brains.setCollision(scene.mesh.get());
    // The guard at (20, 20) faces -y, away from the player at (40, 40).
    Brain& guard = scene.add(Vec3{20.0F, 20.0F, 0.0F}, 180.0F);
    int players = -1;
    const int gang = makeSides(scene, {&guard}, players);
    Level level(true, std::nullopt);
    const coney::ai::TacticServices services{.scripts = &scene.services, .scout = &level.services};
    auto owned = std::make_unique<coney::ai::ScoutTactic>(scoutCall(gang), services);
    coney::ai::ScoutTactic& tactic = *owned;
    scene.brains.gangs().setTactic(gang, std::move(owned));
    scene.run(1);
    REQUIRE(guard.topGoal() != nullptr);
    CHECK(guard.topGoal()->type() == GoalType::Scout);
    CHECK(guard.threatResponse() == 0);
    CHECK(tactic.schedule().interval(guard) == coney::ai::kScoutScanMs);

    // Moved 2 m off, he walks back within the slack.
    guard.human().place(Vec3{22.0F, 20.0F, 0.0F}, 180.0F);
    scene.run(150);
    CHECK(std::hypot(guard.human().position().x - 20.0F, guard.human().position().y - 20.0F) <= 0.5F);
    CHECK(level.calls.empty());
}

TEST_CASE("a guard who spots the player fights him and one runs to the phone to call the gang", "[ai][scout]") {
    AiScene scene;
    scene.brains.setCollision(scene.mesh.get());
    // The guard at (40, 50) faces -y, toward the player at (40, 40); the phone 6 m behind him.
    Brain& guard = scene.add(Vec3{40.0F, 50.0F, 0.0F}, 180.0F);
    int players = -1;
    const int gang = makeSides(scene, {&guard}, players);
    Level level(true, Vec3{40.0F, 56.0F, 0.0F});
    const coney::ai::TacticServices services{.scripts = &scene.services, .scout = &level.services};
    scene.brains.gangs().setTactic(gang, std::make_unique<coney::ai::ScoutTactic>(scoutCall(gang), services));
    scene.run(2);
    CHECK(guard.findGoal(coney::ai::kMeleeGoal) != nullptr);
    REQUIRE(guard.findGoal(GoalType::CallGang) != nullptr);
    CHECK(level.services.callerActive);

    // He runs there, calls for 1.5 s, and the player's gang is wanted.
    for (int k = 0; k < 300 && level.calls.empty(); ++k) {
        scene.run(1);
    }
    REQUIRE(level.calls.size() == 1);
    CHECK(level.calls[0].count == 3);
    CHECK(level.calls[0].delaySeconds == 2);
    CHECK(level.wanted == std::vector<int>{players});
    scene.run(1);
    CHECK_FALSE(level.services.callerActive);
    CHECK(guard.findGoal(GoalType::CallGang) == nullptr);
}

TEST_CASE("with no responder spawner, or the gang called on already, the spotting guard only fights", "[ai][scout]") {
    for (const bool ready : {false, true}) {
        AiScene scene;
        scene.brains.setCollision(scene.mesh.get());
        Brain& guard = scene.add(Vec3{40.0F, 50.0F, 0.0F}, 180.0F);
        int players = -1;
        const int gang = makeSides(scene, {&guard}, players);
        Level level(ready, Vec3{40.0F, 56.0F, 0.0F});
        if (ready) {
            level.wanted.push_back(players);
        }
        const coney::ai::TacticServices services{.scripts = &scene.services, .scout = &level.services};
        scene.brains.gangs().setTactic(gang, std::make_unique<coney::ai::ScoutTactic>(scoutCall(gang), services));
        scene.run(2);
        CHECK(guard.findGoal(coney::ai::kMeleeGoal) != nullptr);
        CHECK(guard.findGoal(GoalType::CallGang) == nullptr);
    }
}

TEST_CASE("a CallGang goal with no phone calls where the caller stands", "[ai][scout]") {
    AiScene scene;
    Brain& caller = scene.add(Vec3{30.0F, 30.0F, 0.0F}, 0.0F);
    Level level(true, std::nullopt);
    caller.pushGoal(std::make_unique<coney::ai::CallGangGoal>(
        coney::ai::CallOrder{.gang = 4, .radius = 10.0F, .count = 1, .delaySeconds = 0}, level.services));
    scene.run(1);
    CHECK(level.calls.empty());
    scene.run(60);
    REQUIRE(level.calls.size() == 1);
    CHECK(level.calls[0].point.x == 30.0F);
    CHECK(level.wanted == std::vector<int>{4});
}
