// SPDX-License-Identifier: GPL-3.0-or-later
#include "ai/fight_reactions.h"

#include <cmath>
#include <cstdint>
#include <memory>
#include <numbers>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "ai/ai_config.h"
#include "ai/attack_kinds.h"
#include "ai/brain.h"
#include "ai/brains.h"
#include "ai/reaction_goals.h"
#include "combat/commands.h"
#include "human/human.h"
#include "human/humans.h"
#include "support/collision_fixtures.h"
#include "support/fight_fixtures.h"
#include "support/human_fixtures.h"

// The fight reaction goals: the grabber's, mounter's, held man's and downed man's moves. Synthetic clips only; game
// time is the steps run (1/30 s each).
// Research: docs/research/ai.md#fight-reactions

using Catch::Approx;
using coney::ai::Brain;
using coney::ai::BrainType;
using coney::ai::GrabMove;
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
        add(coney::anim::Vec3{40.0F, 40.0F, 0.0F}, 0.0F, BrainType::Player);
        step.setBrains(brains.hook());
    }

    // A human with its feet at `feet` facing `headingDegrees`, with a brain of `type`.
    Brain& add(coney::anim::Vec3 feet, float headingDegrees, BrainType type) {
        humans.push_back(std::make_unique<Human>(character.anims, coney::human::AnimSlots::player(),
                                                 coney::test::identityBind(), 1.0F, &character.ranges));
        Human& made = *humans.back();
        const bool player = type == BrainType::Player;
        made.setFighterProfile(coney::human::FighterProfile{
            .player = player, .powerClass = coney::ai::kWarriorPowerClass, .health = 1400});
        made.spawn(mesh.get(), feet, headingDegrees);
        step.add(made, player);
        return brains.add(made, type, coney::ai::FightSettings{}, static_cast<std::uint32_t>(humans.size()));
    }

    void run(int steps) {
        for (int k = 0; k < steps; ++k) {
            step.update(mesh.get());
        }
    }
};

} // namespace

TEST_CASE("a grab's throw directions turn the grabber's heading by a quarter each way, behind by a half", "[ai]") {
    constexpr float kHeading = 0.3F;
    constexpr float kQuarter = std::numbers::pi_v<float> / 2.0F;
    CHECK(coney::ai::grabMoveHeading(GrabMove::Ahead, kHeading) == Approx(kHeading));
    CHECK(coney::ai::grabMoveHeading(GrabMove::Left, kHeading) == Approx(kHeading + kQuarter));
    CHECK(coney::ai::grabMoveHeading(GrabMove::Right, kHeading) == Approx(kHeading - kQuarter));
    CHECK(std::abs(coney::ai::grabMoveHeading(GrabMove::Behind, kHeading)) ==
          Approx(std::numbers::pi_v<float> - kHeading));
}

TEST_CASE("a healthy held man struggles at once and a hurt one waits the whole chain delay", "[ai]") {
    // The hurt fraction 0.3: full health no delay, at or below 0.3 the whole delay, halfway between half of it.
    CHECK(coney::ai::struggleDelayMs(400, 1.0F, 0.3F) == 0);
    CHECK(coney::ai::struggleDelayMs(400, 0.3F, 0.3F) == 400);
    CHECK(coney::ai::struggleDelayMs(400, 0.1F, 0.3F) == 400);
    CHECK(coney::ai::struggleDelayMs(400, 0.65F, 0.3F) == 200);
}

TEST_CASE("the get-up attack comes 1.9 s after going down, counted up to 2 s", "[ai]") {
    CHECK(coney::ai::getUpDelayMs(0) == 1900);
    CHECK(coney::ai::getUpDelayMs(500) == 1400);
    CHECK(coney::ai::getUpDelayMs(1900) == 0);
    CHECK(coney::ai::getUpDelayMs(5000) == 0);
}

TEST_CASE("a knocked-down man under attack presses the get-up attack about 1.9 s after going down", "[ai]") {
    Scene scene;
    Brain& gang = scene.add({40.0F, 41.0F, 0.0F}, 180.0F, BrainType::Gang);
    // Another AI targets him, so holds an attack slot on him.
    Brain& hitter = scene.add({41.0F, 41.0F, 0.0F}, 90.0F, BrainType::Gang);
    hitter.setTarget(&gang);
    REQUIRE(gang.attackSlots().size() == 1);
    scene.run(2);
    // Hurt (below 35 % of 1400), a heavy reaction (code 0x26) from the player in front knocks him down.
    gang.human().fighter().health().apply(1000);
    coney::human::IncomingHit hit;
    hit.damage = 46;
    hit.attackAnim = 13;
    hit.code = 0x26;
    hit.attacker = coney::anim::Vec3{40.0F, 40.0F, 0.0F};
    gang.human().hit(hit);
    // He goes down as the reaction's clip lands; the reaction goal follows the state one update later.
    for (int k = 0; k < 60 && !gang.human().fighter().victim().grounded(); ++k) {
        scene.run(1);
    }
    REQUIRE(gang.human().fighter().victim().grounded());
    scene.run(2);
    CHECK(dynamic_cast<const coney::ai::GroundedGoal*>(gang.reactionGoal()) != nullptr);
    coney::combat::CombatRandom random(1);
    const coney::combat::CommandId getUp = coney::ai::commandOf(42, random);
    int pressedAt = -1;
    for (int k = 2; k < 90 && pressedAt < 0; ++k) {
        scene.run(1);
        if (gang.human().record().command == getUp) {
            pressedAt = k;
        }
    }
    // 1900 ms is 57 steps; the goal starts one update after the fall.
    CHECK(pressedAt >= 55);
    CHECK(pressedAt <= 62);
}
