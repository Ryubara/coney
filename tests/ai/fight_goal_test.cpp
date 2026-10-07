// SPDX-License-Identifier: GPL-3.0-or-later
#include "ai/fight_goal.h"

#include <cstdint>
#include <memory>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "ai/ai_config.h"
#include "ai/attack_kinds.h"
#include "ai/attack_views.h"
#include "ai/brain.h"
#include "ai/brains.h"
#include "ai/engage_goals.h"
#include "ai/gangs.h"
#include "ai/idle_goals.h"
#include "ai/melee_goal.h"
#include "combat/commands.h"
#include "human/human.h"
#include "human/human_flags.h"
#include "human/humans.h"
#include "support/collision_fixtures.h"
#include "support/fight_fixtures.h"
#include "support/human_fixtures.h"

// The fight goal with the attack choice: the active-attacker places that make a gang take turns, and the tackle try.
// Synthetic clips only; game time is the steps run (1/30 s each).
// Research: docs/research/ai.md#fight

using coney::ai::Brain;
using coney::ai::BrainType;
using coney::ai::FightSettings;
using coney::human::Human;

namespace {

// A floor with a player at (40, 40) facing +y, and the AI humans added to it.
struct Scene {
    coney::test::FightCharacter character;
    std::unique_ptr<coney::raycast::CollisionMesh> mesh =
        coney::test::makeMesh(coney::test::floorAt(0.0F, 0.0F, 80.0F, 0.0F, 80.0F));
    std::vector<std::unique_ptr<Human>> humans;
    coney::human::Humans step;
    coney::ai::Brains brains;

    Scene() {
        add(coney::anim::Vec3{40.0F, 40.0F, 0.0F}, 0.0F, BrainType::Player, FightSettings{});
        step.setBrains(brains.hook());
    }

    // A human with its feet at `feet` facing `headingDegrees`, with a brain of `type` and `settings`.
    Brain& add(coney::anim::Vec3 feet, float headingDegrees, BrainType type, const FightSettings& settings) {
        humans.push_back(std::make_unique<Human>(character.anims, coney::human::AnimSlots::player(),
                                                 coney::test::identityBind(), 1.0F, &character.ranges));
        Human& made = *humans.back();
        const bool player = type == BrainType::Player;
        made.setFighterProfile(coney::human::FighterProfile{
            .player = player, .powerClass = coney::ai::kWarriorPowerClass, .health = 1400});
        made.spawn(mesh.get(), feet, headingDegrees);
        step.add(made, player);
        return brains.add(made, type, settings, static_cast<std::uint32_t>(humans.size()));
    }

    Brain& playerBrain() { return brains.at(0); }
    void run(int steps) {
        for (int k = 0; k < steps; ++k) {
            step.update(mesh.get());
        }
    }
};

// Settings whose attack table holds only `kind`.
FightSettings onlyKind(int kind) {
    FightSettings settings;
    settings.attackWeights = coney::ai::AttackWeights{};
    settings.attackWeights[static_cast<std::size_t>(kind)] = 10;
    return settings;
}

} // namespace

TEST_CASE("two attackers on a man with a spacing of one take turns at his one active place", "[ai]") {
    Scene scene;
    const FightSettings square = onlyKind(1);
    Brain& front = scene.add({40.0F, 40.9F, 0.0F}, 180.0F, BrainType::Gang, square);
    Brain& back = scene.add({40.0F, 39.1F, 0.0F}, 0.0F, BrainType::Gang, square);
    for (Brain* brain : {&front, &back}) {
        brain->setTarget(&scene.playerBrain());
        REQUIRE(brain->pushGoal(std::make_unique<coney::ai::FightGoal>()));
    }
    REQUIRE(scene.playerBrain().fightBook().spacing.inUse(false) == 1);
    bool frontPressed = false;
    bool backPressed = false;
    for (int k = 0; k < 300; ++k) {
        scene.run(1);
        const auto* a = dynamic_cast<const coney::ai::FightGoal*>(front.topGoal());
        const auto* b = dynamic_cast<const coney::ai::FightGoal*>(back.topGoal());
        // Never both in the place at once.
        CHECK_FALSE((a != nullptr && b != nullptr && a->holdsPlace() && b->holdsPlace()));
        CHECK(scene.playerBrain().fightBook().places.held() <= 1);
        frontPressed = frontPressed || front.human().record().command == coney::combat::command::kSquarePressed;
        backPressed = backPressed || back.human().record().command == coney::combat::command::kSquarePressed;
    }
    // Each had his turn.
    CHECK(frontPressed);
    CHECK(backPressed);
}

TEST_CASE("the fight goal tackles once the tackle meter passes the gang's threshold", "[ai]") {
    Scene scene;
    FightSettings settings = onlyKind(1);
    settings.attackWeights[21] = 10;
    constexpr int kKind = 3;
    settings.gangFight[kKind].tackle = 6; // fires above (7 - 6) × 6 = 6
    Brain& gang = scene.add({40.0F, 40.9F, 0.0F}, 180.0F, BrainType::Gang, settings);
    const int id = scene.brains.gangs().create(kKind, "tacklers");
    scene.brains.gangs().addMember(id, gang);
    REQUIRE(gang.gangFight().tackle == 6);
    gang.setTarget(&scene.playerBrain());
    // A target who keeps running off fills the meter by his gait each think.
    gang.fightBook().tackle.think(true, 4);
    gang.fightBook().tackle.think(true, 4); // one more: the brain's own think may lower it by 2 first
    gang.fightBook().tackle.think(true, 4);
    REQUIRE(gang.fightBook().tackle.ready(6));
    REQUIRE(gang.pushGoal(std::make_unique<coney::ai::FightGoal>()));
    scene.run(1);
    coney::combat::CombatRandom random(1);
    CHECK(gang.human().record().command == coney::ai::commandOf(21, random));
    CHECK(gang.fightBook().tackle.value() == 0);
}

TEST_CASE("without the gang's tackle value the meter is emptied and the fight goal never tackles", "[ai]") {
    Scene scene;
    FightSettings settings = onlyKind(21);
    Brain& gang = scene.add({40.0F, 40.9F, 0.0F}, 180.0F, BrainType::Gang, settings);
    gang.setTarget(&scene.playerBrain());
    gang.fightBook().tackle.think(true, 4);
    gang.fightBook().tackle.think(true, 4);
    REQUIRE(gang.pushGoal(std::make_unique<coney::ai::FightGoal>()));
    scene.run(1);
    CHECK(gang.fightBook().tackle.value() == 0);
}

TEST_CASE("a rear-grabbed man is the grab's, and a man is behind another in his rear quarter", "[ai]") {
    Scene scene;
    Brain& behindHim = scene.add({40.0F, 39.0F, 0.0F}, 0.0F, BrainType::Gang, onlyKind(1));
    Brain& before = scene.add({40.0F, 41.0F, 0.0F}, 180.0F, BrainType::Gang, onlyKind(1));
    CHECK(coney::ai::behind(behindHim, scene.playerBrain()));
    CHECK_FALSE(coney::ai::behind(before, scene.playerBrain()));
    CHECK_FALSE(coney::ai::grabbedFromRear(scene.playerBrain()));
    CHECK(coney::ai::rearGrabberOf(scene.playerBrain()) == nullptr);
    CHECK(coney::ai::capsuleRadius(scene.playerBrain()) > 0.0F);
}

TEST_CASE("the Melee goal's and the dealer's spectate arguments are the research's", "[ai]") {
    const coney::ai::SpectateArgs melee = coney::ai::SpectateArgs::melee(0.0F);
    CHECK(melee.join);
    CHECK(melee.mayEngage);
    CHECK(melee.taunt);
    CHECK(melee.shortestPauseMs == 1000);
    CHECK(melee.longestPauseMs == 3000);
    CHECK(melee.timeLimitMs == 2000);
    const coney::ai::SpectateArgs wary = coney::ai::SpectateArgs::dealerWary();
    CHECK(wary.keepDistance == 8.0F);
    CHECK_FALSE(wary.join);
    CHECK(wary.timeLimitMs == 8000);
}

TEST_CASE("a joining spectator watches his nearest enemy without an attack slot and ends at his time limit", "[ai]") {
    Scene scene;
    Brain& gang = scene.add({40.0F, 42.0F, 0.0F}, 180.0F, BrainType::Gang, onlyKind(1));
    gang.addEnemy(scene.playerBrain());
    REQUIRE(gang.pushGoal(std::make_unique<coney::ai::SpectateGoal>(coney::ai::SpectateArgs::melee(0.0F))));
    scene.run(2);
    const auto* spectate = dynamic_cast<const coney::ai::SpectateGoal*>(gang.topGoal());
    REQUIRE(spectate != nullptr);
    CHECK(spectate->watched() == &scene.playerBrain());
    CHECK(scene.playerBrain().attackSlots().empty());
    // 2 s is 60 steps.
    scene.run(62);
    CHECK(gang.topGoal() == nullptr);
}

TEST_CASE("a spectator too close backs out toward his keep distance", "[ai]") {
    Scene scene;
    Brain& gang = scene.add({40.0F, 41.0F, 0.0F}, 180.0F, BrainType::Gang, onlyKind(1));
    gang.addEnemy(scene.playerBrain());
    coney::ai::SpectateArgs args = coney::ai::SpectateArgs::melee(4.0F);
    args.mayEngage = false;
    args.timeLimitMs = 6000;
    REQUIRE(gang.pushGoal(std::make_unique<coney::ai::SpectateGoal>(args)));
    const float before = gang.distanceTo(scene.playerBrain());
    scene.run(90);
    CHECK(gang.distanceTo(scene.playerBrain()) > before + 1.0F);
}

TEST_CASE("the Melee goal with enemies but none to fight spectates", "[ai]") {
    Scene scene;
    Brain& gang = scene.add({40.0F, 42.0F, 0.0F}, 180.0F, BrainType::Gang, onlyKind(1));
    gang.addEnemy(scene.playerBrain());
    // The player may not be picked as a target (flag::kNoTarget), but stays on the enemy list.
    scene.playerBrain().human().setFlag(coney::human::flag::kNoTarget, true);
    REQUIRE(gang.pushGoal(std::make_unique<coney::ai::MeleeGoal>()));
    scene.run(1);
    REQUIRE(gang.topGoal() != nullptr);
    CHECK(gang.topGoal()->type() == coney::ai::GoalType::Spectate);
}

TEST_CASE("the EngageEnemy run-in raises the turn boost by one until it ends", "[ai]") {
    Scene scene;
    Brain& gang = scene.add({40.0F, 50.0F, 0.0F}, 180.0F, BrainType::Gang, onlyKind(1));
    gang.setTarget(&scene.playerBrain());
    REQUIRE(gang.turnBoost() == 0);
    REQUIRE(gang.pushGoal(std::make_unique<coney::ai::EngageEnemyGoal>()));
    scene.run(1);
    REQUIRE(gang.topGoal() != nullptr);
    REQUIRE(gang.topGoal()->type() == coney::ai::GoalType::EngageEnemy);
    CHECK(gang.turnBoost() == 1);
    gang.clearGoals();
    CHECK(gang.turnBoost() == 0);
}
