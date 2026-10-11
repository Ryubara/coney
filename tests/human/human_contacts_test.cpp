// SPDX-License-Identifier: GPL-3.0-or-later
// What a walking human meets of another human (docs/research/physics.md#human-contacts): one upright capsule swept
// against another, the half push the one met is given, and the characters' step running them, so the player stops at
// a human he cannot push, pushes one he can, two AI humans never walk into each other, a linked pair passes through
// each other and a human whose health is out is walked over. Driven as a pad (the left stick at a partial deflection)
// or as a brain's move. Synthetic clips and floor; no game data.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <span>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "combat/commands.h"
#include "combat/player_combat.h"
#include "combat/power_class.h"
#include "human/human.h"
#include "human/human_contacts.h"
#include "human/humans.h"
#include "support/collision_fixtures.h"
#include "support/fight_fixtures.h"
#include "support/human_fixtures.h"

using Catch::Approx;
using coney::anim::Vec3;
using coney::human::Human;
using coney::human::UprightCapsule;

namespace {

// Two capsules of scale 1 meet with their axes this far apart: 0.35 + 0.35.
constexpr float kReach = 0.7F;

// The distance across the ground between two humans' feet.
float apart(const Human& a, const Human& b) {
    return std::hypot(a.position().x - b.position().x, a.position().y - b.position().y);
}

// A profile no player controls.
coney::human::FighterProfile aiProfile() {
    return coney::human::FighterProfile{
        .player = false, .powerClass = coney::combat::kCivilianPowerClass, .health = 600};
}

// A floor, the pad's human (the player) at (40, 40) facing +y and a human no pad drives `ahead` metres north of him,
// both of scale 1, stepped together by the characters' step.
struct Street {
    coney::test::FightCharacter character;
    std::unique_ptr<coney::raycast::CollisionMesh> mesh =
        coney::test::makeMesh(coney::test::floorAt(0.0F, 0.0F, 80.0F, 0.0F, 80.0F));
    Human player{character.anims, coney::human::AnimSlots::player(), coney::test::identityBind(), 1.0F,
                 &character.ranges};
    Human other{character.anims, coney::human::AnimSlots::player(), coney::test::identityBind(), 1.0F,
                &character.ranges};
    coney::human::Humans humans;

    explicit Street(float ahead) {
        player.spawn(mesh.get(), Vec3{40.0F, 40.0F, 0.0F}, 0.0F);
        other.setFighterProfile(aiProfile());
        other.spawn(mesh.get(), Vec3{40.0F, 40.0F + ahead, 0.0F}, 180.0F);
        humans.add(player, true);
        humans.add(other, false);
    }

    // One step with the player's left stick at (`x`, `y`), the camera looking along +y.
    void step(float x, float y) {
        player.record() = coney::human::recordOf(coney::human::HumanInput{.stickX = x,
                                                                          .stickY = y,
                                                                          .cameraForward = Vec3{0.0F, 1.0F, 0.0F},
                                                                          .sprintHeld = false,
                                                                          .actionPressed = false,
                                                                          .command = coney::combat::command::kNone,
                                                                          .buttons = 0,
                                                                          .targets = {}});
        humans.update(mesh.get());
    }
};

} // namespace

TEST_CASE("two upright capsules meet where their axes are the two radii apart, while their heights overlap",
          "[human][contacts]") {
    const UprightCapsule other{.feet = Vec3{0.0F, 2.0F, 0.0F}, .top = 1.8F, .radius = 0.35F};
    const UprightCapsule mover{.feet = Vec3{0.0F, 0.0F, 0.0F}, .top = 1.8F, .radius = 0.3395F};
    // Head-on: met 0.6895 m short of the other's axis, the normal back at the mover.
    const auto headOn = coney::human::sweepCapsules(mover, Vec3{0.0F, 2.0F, 0.0F}, other);
    REQUIRE(headOn.has_value());
    CHECK(headOn->fraction == Approx((2.0F - 0.6895F) / 2.0F));
    CHECK(headOn->normal.y == Approx(-1.0F));
    // Short of it, past it on one side, or the heights apart (the mover 2 m up): not met.
    CHECK_FALSE(coney::human::sweepCapsules(mover, Vec3{0.0F, 1.0F, 0.0F}, other).has_value());
    CHECK_FALSE(
        coney::human::sweepCapsules(UprightCapsule{.feet = Vec3{0.7F, 0.0F, 0.0F}, .top = 1.8F, .radius = 0.3395F},
                                    Vec3{0.0F, 4.0F, 0.0F}, other)
            .has_value());
    CHECK_FALSE(
        coney::human::sweepCapsules(UprightCapsule{.feet = Vec3{0.0F, 0.0F, 2.0F}, .top = 1.8F, .radius = 0.3395F},
                                    Vec3{0.0F, 2.0F, 0.0F}, other)
            .has_value());
    // Already within reach: a move further in is met at once, one away is not.
    const UprightCapsule inside{.feet = Vec3{0.0F, 1.5F, 0.0F}, .top = 1.8F, .radius = 0.3395F};
    const auto atOnce = coney::human::sweepCapsules(inside, Vec3{0.0F, 0.1F, 0.0F}, other);
    REQUIRE(atOnce.has_value());
    CHECK(atOnce->fraction == 0.0F);
    CHECK(atOnce->normal.y == Approx(-1.0F));
    CHECK_FALSE(coney::human::sweepCapsules(inside, Vec3{0.0F, -0.1F, 0.0F}, other).has_value());
}

TEST_CASE("a contact pushes the human met by half of the move into him, over his push weight", "[human][contacts]") {
    const Vec3 normal{0.0F, -1.0F, 0.0F};
    // 0.06 m of move, 30° off the normal: 0.052 goes in, half of it pushes him on.
    const Vec3 move{0.03F, 0.052F, 0.0F};
    const Vec3 push = coney::human::contactPush(move, normal, 1.0F);
    CHECK(push.x == Approx(0.0F));
    CHECK(push.y == Approx(0.026F));
    // Held (weight 1e9) he does not move; a move away pushes nothing.
    CHECK(coney::human::contactPush(move, normal, 1e9F).y == Approx(0.0F).margin(1e-9));
    CHECK(coney::human::contactPush(Vec3{0.0F, -0.05F, 0.0F}, normal, 1.0F).y == 0.0F);
}

TEST_CASE("the player walking into a standing human he cannot push stops with their capsules touching",
          "[human][contacts]") {
    Street street(3.0F);
    street.other.script().pushable = false; // HuSetPushable(false): body flag 0x200
    // Stick up at 60 %, a walk, for 3 s.
    for (int i = 0; i < 90; ++i) {
        street.step(0.0F, 0.6F);
        REQUIRE(apart(street.player, street.other) >= kReach - 0.005F);
    }
    std::printf("  contacts: stopped %.4f m from the unpushable human\n", apart(street.player, street.other));
    CHECK(apart(street.player, street.other) == Approx(kReach).margin(0.01F));
    CHECK(street.other.position().y == Approx(43.0F));
    // The slide took the speed into him from the player's velocity.
    CHECK(std::abs(street.player.velocity().y) < 0.2F);
}

TEST_CASE("the player walking into a standing human pushes him on, never into him", "[human][contacts]") {
    Street street(3.0F);
    for (int i = 0; i < 120; ++i) {
        street.step(0.0F, 0.6F);
        REQUIRE(apart(street.player, street.other) >= kReach - 0.005F);
    }
    std::printf("  contacts: the pushed human moved %.3f m\n", street.other.position().y - 43.0F);
    CHECK(street.other.position().y > 43.5F);
    CHECK(std::abs(street.other.position().x - 40.0F) < 0.05F);
}

TEST_CASE("two AI humans walked at each other do not overlap", "[human][contacts]") {
    Street street(4.0F);
    street.player.setFighterProfile(aiProfile());
    street.humans.setPadControlled(street.player, false);
    // Each brain walks its human at the other at 1.6 m/s, as a move action writes it.
    street.humans.setBrains([](std::span<Human* const> humans) {
        humans[0]->record().move = coney::human::BrainMove{.heading = 0.0F, .speed = 1.6F};
        humans[1]->record().move = coney::human::BrainMove{.heading = 3.14159265F, .speed = 1.6F};
    });
    float nearest = 4.0F;
    for (int i = 0; i < 90; ++i) {
        street.step(0.0F, 0.0F);
        nearest = std::min(nearest, apart(street.player, street.other));
    }
    std::printf("  contacts: two AI humans came within %.4f m\n", nearest);
    CHECK(nearest >= kReach - 0.005F);
    CHECK(nearest < kReach + 0.05F);
}

TEST_CASE("a human whose health is out no longer blocks: the player walks over him", "[human][contacts]") {
    Street street(3.0F);
    street.other.script().knockedOut = true;
    CHECK_FALSE(street.other.blocksHumans());
    float nearest = 3.0F;
    for (int i = 0; i < 90; ++i) {
        street.step(0.0F, 0.6F);
        nearest = std::min(nearest, apart(street.player, street.other));
    }
    CHECK(nearest < 0.3F);
    // Up again, he blocks again (**Coney stand-in**: what restores the flag is not traced).
    street.other.script().knockedOut = false;
    CHECK(street.other.blocksHumans());
}

TEST_CASE("a grab links the pair: they pass through each other and neither can be pushed", "[human][contacts]") {
    const coney::test::FightCharacter character;
    const auto mesh = coney::test::makeMesh(coney::test::floorAt(0.0F, 0.0F, 80.0F, 0.0F, 80.0F));
    Human player(character.anims, coney::human::AnimSlots::player(), coney::test::identityBind(), 1.0F,
                 &character.ranges);
    Human other(character.anims, coney::human::AnimSlots::player(), coney::test::identityBind(), 1.0F,
                &character.ranges);
    player.spawn(mesh.get(), Vec3{40.0F, 40.0F, 0.0F}, 0.0F);
    other.setFighterProfile(aiProfile());
    other.spawn(mesh.get(), Vec3{40.0F, 42.0F, 0.0F}, 180.0F);
    const std::vector<Human*> both{&player, &other};
    std::vector<coney::human::Combatant*> targets{&other};
    coney::combat::CommandTables tables = coney::combat::CommandTables::street();
    coney::combat::CommandMatcher matcher;
    CHECK_FALSE(player.linkedWith(other));
    CHECK(other.pushWeight() == 1.0F);
    // Circle held: the tackle runs at him and mounts him, the two bodies one on the other.
    float nearest = 2.0F;
    for (const coney::test::PadFrame& frame : coney::test::playScript("5 press circle\n15 release circle\n", 60)) {
        player.setHumanContacts(both);
        other.setHumanContacts(both);
        const coney::combat::CommandId command =
            matcher.update(frame.buttons, tables, coney::combat::combatTuning().historyHoldSamples);
        player.step(coney::human::HumanInput{.stickX = frame.leftX,
                                             .stickY = frame.leftY,
                                             .cameraForward = Vec3{0.0F, 1.0F, 0.0F},
                                             .sprintHeld = false,
                                             .actionPressed = false,
                                             .command = command,
                                             .buttons = frame.buttons,
                                             .targets = targets},
                    mesh.get());
        other.step(
            coney::human::HumanInput{.cameraForward = Vec3{0.0F, 1.0F, 0.0F}, .command = coney::combat::command::kNone},
            mesh.get());
        if (player.linkedWith(other)) {
            nearest = std::min(nearest, apart(player, other));
        }
    }
    REQUIRE(other.state() == coney::human::TargetState::Mounted);
    CHECK(player.linkedWith(other));
    CHECK(other.linkedWith(player));
    CHECK(other.pushWeight() == 1e9F);
    CHECK(player.pushWeight() == 1e9F);
    std::printf("  contacts: the linked pair came within %.3f m\n", nearest);
    CHECK(nearest < kReach - 0.1F);
}
