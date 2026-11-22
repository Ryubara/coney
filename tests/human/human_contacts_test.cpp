// SPDX-License-Identifier: GPL-3.0-or-later
// What a walking human meets of another human (docs/research/physics.md#human-contacts): one upright capsule swept
// against another, the half push the one met is given, and the characters' step running them, so the player stops at
// a human he cannot push, pushes one he can, two AI humans never walk into each other, a linked pair passes through
// each other and a human whose health is out is walked over. Driven as a pad (the left stick at a partial deflection)
// or as a brain's move. Synthetic clips and floor; no game data.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
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
    // The if repeats the REQUIRE for clang-tidy, which cannot see that a failed REQUIRE ends the test.
    if (headOn) {
        CHECK(headOn->fraction == Approx((2.0F - 0.6895F) / 2.0F));
        CHECK(headOn->normal.y == Approx(-1.0F));
    }
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
    if (atOnce) {
        CHECK(atOnce->fraction == 0.0F);
        CHECK(atOnce->normal.y == Approx(-1.0F));
    }
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
        humans[0]->record().move = coney::human::BrainMove{.heading = 0.0F, .speed = 1.6F, .turnBoost = 0, .aim = {}};
        humans[1]->record().move =
            coney::human::BrainMove{.heading = 3.14159265F, .speed = 1.6F, .turnBoost = 0, .aim = {}};
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
    street.other.fighter().health().set(0);
    CHECK_FALSE(street.other.blocksHumans());
    float nearest = 3.0F;
    for (int i = 0; i < 90; ++i) {
        street.step(0.0F, 0.6F);
        nearest = std::min(nearest, apart(street.player, street.other));
    }
    CHECK(nearest < 0.3F);
    // Health given back without a heal (a script's) leaves him passable: only the heal makes the body again.
    street.other.fighter().health().set(street.other.fighter().health().maximum());
    CHECK_FALSE(street.other.blocksHumans());
    street.other.heal();
    CHECK(street.other.blocksHumans());
}

TEST_CASE("BLOCKHUMANS comes back from a heal or the release from an arrest, never from waking up",
          "[human][contacts]") {
    Street street(3.0F);
    REQUIRE(street.other.blocksHumans());
    // A wound takes it; the wound ending does not give it back; the revive (a heal) does.
    street.other.setWounded(true);
    CHECK_FALSE(street.other.blocksHumans());
    street.other.setWounded(false);
    CHECK_FALSE(street.other.blocksHumans());
    street.other.revive();
    CHECK(street.other.blocksHumans());
    // The cuffs take it; the release gives it back.
    street.other.setArrested(true);
    CHECK_FALSE(street.other.blocksHumans());
    street.other.setArrested(false);
    CHECK(street.other.blocksHumans());
    // A knock-out takes it; standing up again (no longer knocked out) does not give it back.
    street.other.loseBlockHumans();
    street.other.script().knockedOut = true;
    street.step(0.0F, 0.0F);
    street.other.script().knockedOut = false;
    street.step(0.0F, 0.0F);
    CHECK_FALSE(street.other.blocksHumans());
    street.other.heal();
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
        other.step(coney::human::HumanInput{.cameraForward = Vec3{0.0F, 1.0F, 0.0F},
                                            .command = coney::combat::command::kNone,
                                            .targets = {}},
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

namespace {

// A contact source that answers every question with the humans it lists and keeps the reaches it was asked for.
struct ListedContacts final : coney::human::ContactSource {
    std::vector<Human*> listed;
    mutable std::vector<float> asked;

    [[nodiscard]] std::span<Human* const> near(const Vec3& /*at*/, float reach) const override {
        asked.push_back(reach);
        return listed;
    }
};

// The player at (40, 40) walking north at 60 % of the stick for `steps` steps, his contact list `both`.
void walkNorth(Human& player, Human& other, const coney::raycast::CollisionMesh& mesh, int steps) {
    const std::vector<Human*> both{&player, &other};
    for (int i = 0; i < steps; ++i) {
        player.setHumanContacts(both);
        player.step(coney::human::HumanInput{.stickX = 0.0F,
                                             .stickY = 0.6F,
                                             .cameraForward = Vec3{0.0F, 1.0F, 0.0F},
                                             .sprintHeld = false,
                                             .actionPressed = false,
                                             .command = coney::combat::command::kNone,
                                             .buttons = 0,
                                             .targets = {}},
                    &mesh);
    }
}

} // namespace

TEST_CASE("a walking body meets only the humans its contact source gives for the move's reach", "[human][contacts]") {
    // The step's contact list holds a human 2 m ahead, unpushable; a source that leaves him out lets the player walk
    // through him, one that gives him stops the player at him. Each move asks for the humans within its own length
    // across the ground plus the walker's radius (0.35 m) and a hair: no one further can be met, as the original's body
    // pass asks its world only for the bodies in the move's box (IPhysics_QueryBox, issue 1556).
    // Expected: physics-bodies.md#u11-body-pass (the candidates from a box query), confirmed (code)
    const coney::test::FightCharacter character;
    const auto mesh = coney::test::makeMesh(coney::test::floorAt(0.0F, 0.0F, 80.0F, 0.0F, 80.0F));
    for (const bool given : {false, true}) {
        Human player(character.anims, coney::human::AnimSlots::player(), coney::test::identityBind(), 1.0F,
                     &character.ranges);
        Human other(character.anims, coney::human::AnimSlots::player(), coney::test::identityBind(), 1.0F,
                    &character.ranges);
        player.spawn(mesh.get(), Vec3{40.0F, 40.0F, 0.0F}, 0.0F);
        other.setFighterProfile(aiProfile());
        other.spawn(mesh.get(), Vec3{40.0F, 42.0F, 0.0F}, 180.0F);
        other.script().pushable = false;
        ListedContacts source;
        if (given) {
            source.listed.push_back(&other);
        }
        player.setContactSource(&source);
        walkNorth(player, other, *mesh, 90);
        player.setContactSource(nullptr);
        REQUIRE_FALSE(source.asked.empty());
        for (const float reach : source.asked) {
            CHECK(reach > 0.35F);
            CHECK(reach < 0.35F + 0.2F);
        }
        if (given) {
            CHECK(player.position().y == Approx(42.0F - kReach).margin(0.01F));
        } else {
            CHECK(player.position().y > 43.0F);
        }
    }
}

namespace {

// Which of two humans standing side by side ahead a walking human meets first.
enum class Met : std::uint8_t { None, Left, Right };

// A human in the last slot walked north by its brain from (40, 40) at two humans 1 m ahead, a quarter metre to either
// side, so that his capsule meets both at the same fraction: the left one in slot `leftSlot` and the right one in the
// other of slots 0 and 1, with a third human far off at (`farX`, `farY`) to move the cells of the step's grid.
Met firstMet(std::size_t leftSlot, float farX, float farY) {
    const coney::test::FightCharacter character;
    const auto mesh = coney::test::makeMesh(coney::test::floorAt(0.0F, 0.0F, 120.0F, 0.0F, 120.0F));
    std::vector<std::unique_ptr<Human>> people;
    const Vec3 left{39.75F, 41.0F, 0.0F};
    const Vec3 right{40.25F, 41.0F, 0.0F};
    const std::vector<Vec3> at{leftSlot == 0 ? left : right, leftSlot == 0 ? right : left, Vec3{farX, farY, 0.0F},
                               Vec3{40.0F, 40.0F, 0.0F}};
    coney::human::Humans humans;
    for (const Vec3& place : at) {
        people.push_back(std::make_unique<Human>(character.anims, coney::human::AnimSlots::player(),
                                                 coney::test::identityBind(), 1.0F, &character.ranges));
        people.back()->setFighterProfile(aiProfile());
        people.back()->spawn(mesh.get(), place, 0.0F);
        humans.add(*people.back(), false);
    }
    humans.setBrains([](std::span<Human* const> all) {
        all[3]->record().move = coney::human::BrainMove{.heading = 0.0F, .speed = 1.6F, .turnBoost = 0, .aim = {}};
    });
    const Human& walker = *people[3];
    for (int i = 0; i < 60; ++i) {
        humans.update(mesh.get());
        if (!walker.contacts().empty()) {
            // The first contact's normal points from the human met to the walker.
            return walker.contacts().front().x > 0.0F ? Met::Left : Met::Right;
        }
    }
    return Met::None;
}

} // namespace

// A stand-in pin: Coney's contacts take the humans in slot order, where the original's body pass takes them in its box
// query's order (x-sorted) and keeps the earlier of equal fractions; #1607 ports that and turns these expectations
// round. Until then the test holds the grid to the rule the contact list had before it (a refactor pin, not the
// original's rule).
TEST_CASE("of two humans met at the same fraction, the walking body meets the one in the earlier slot first",
          "[human][contacts]") {
    // The nearest-contact test keeps the first of equals (`fraction < first`), so the earlier slot is met first
    // whichever side it stands on. The third human at x 37.5 puts the left and the right one in different cells of the
    // step's grid (2.5 m cells from the lowest x and y); at (70, 41) both are in one cell (issue 1556).
    // Expected: physics.md#coneys-implementation, Coney's stand-in (#1607): the original keeps the earlier in x order
    CHECK(firstMet(0, 37.5F, 70.0F) == Met::Left);
    CHECK(firstMet(1, 37.5F, 70.0F) == Met::Right);
    CHECK(firstMet(0, 70.0F, 41.0F) == Met::Left);
    CHECK(firstMet(1, 70.0F, 41.0F) == Met::Right);
}
