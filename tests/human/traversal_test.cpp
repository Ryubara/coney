// SPDX-License-Identifier: GPL-3.0-or-later
// The player's traversal on synthetic ground and the synthetic clip set (tests/support/human_fixtures.h): the
// walking body's step rule, the sprint and its stamina, the jump and the climbs, driven as a pad would at partial and
// full deflections, on the fixed 30 Hz step. No game data.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "characters/anim_set.h"
#include "human/body.h"
#include "human/climb.h"
#include "human/human.h"
#include "human/human_animator.h"
#include "support/collision_fixtures.h"
#include "support/human_fixtures.h"

using Catch::Approx;
using coney::anim::Vec3;
using coney::human::AnimState;
using coney::human::Human;
using coney::human::HumanInput;
using coney::human::Traversal;
using coney::test::blockAlongY;
using coney::test::floorAt;
using coney::test::join;
using coney::test::makeMesh;

namespace {

constexpr float kPi = std::numbers::pi_v<float>;
// The camera the tests turn the stick with: looking along +y, so stick up moves along +y.
constexpr Vec3 kAlongY{0.0F, 1.0F, 0.0F};

// The pad for one update: the left stick at `x`, `y`, L2 held or not, triangle pressed this update or not.
HumanInput pad(float x, float y, bool l2 = false, bool triangle = false) {
    return HumanInput{.stickX = x,
                      .stickY = y,
                      .cameraForward = kAlongY,
                      .sprintHeld = l2,
                      .actionPressed = triangle,
                      .command = coney::combat::command::kNone,
                      .buttons = 0,
                      .targets = {}};
}

// The synthetic character and its anim set, held together for a test.
struct TestCharacter {
    coney::characters::CharacterData data = coney::test::locomotionData();
    coney::characters::AnimSet anims{data, nullptr};
};

// A human of `character` at `at` on `mesh`, facing +y.
Human spawnHuman(const TestCharacter& character, const coney::raycast::CollisionMesh* mesh, Vec3 at) {
    Human human(character.anims, coney::human::AnimSlots::player(), coney::test::identityBind());
    human.spawn(mesh, at, 0.0F);
    return human;
}

// Steps `human` `updates` times with the same pad.
void hold(Human& human, const HumanInput& input, int updates, const coney::raycast::CollisionMesh* mesh) {
    for (int i = 0; i < updates; ++i) {
        human.step(input, mesh);
    }
}

// Flat ground over the whole synthetic mesh.
std::vector<coney::test::Tri> ground() { return floorAt(0.0F, 0.0F, 80.0F, 0.0F, 80.0F); }

} // namespace

TEST_CASE("a step under 0.25 m is walked onto in one update with no change of speed or clip", "[human][traversal]") {
    // The runtime's test step (docs/research/characters.md#walls): walked at 35 % and 50 % up to 0.24 m, run at 100 %
    // onto 0.245 m; the feet rise by the step's height on one update, the speed and the clip unchanged.
    const TestCharacter character;
    struct Case {
        float height;
        float stick;
    };
    for (const Case c : {Case{0.10F, 0.35F}, Case{0.20F, 0.5F}, Case{0.24F, 0.35F}, Case{0.245F, 1.0F}}) {
        // Low ground to y = 40, then a step `height` high with its face at y = 40.
        const auto mesh =
            makeMesh(join(join(floorAt(0.0F, 0.0F, 80.0F, 0.0F, 40.0F), floorAt(c.height, 0.0F, 80.0F, 40.0F, 80.0F)),
                          coney::test::wallFacingMinusY(40.0F, 0.0F, 80.0F, 0.0F, c.height)));
        Human human(character.anims, coney::human::AnimSlots::player(), coney::test::identityBind(), 0.97F);
        human.spawn(mesh.get(), Vec3{40.0F, 38.0F, 0.0F}, 0.0F);
        // Toward the step (the walk reaches its steady speed first; the run is still in its start clip), then over it.
        hold(human, pad(0.0F, c.stick), 15, mesh.get());
        int rises = 0;
        for (int i = 0; i < 150 && human.position().y < 41.0F; ++i) {
            const float zBefore = human.position().z;
            const float speedBefore = human.speed();
            const std::uint32_t clipBefore = human.animator().animId();
            human.step(pad(0.0F, c.stick), mesh.get());
            if (human.position().z > zBefore + 1e-4F) {
                ++rises;
                CHECK(human.position().z == Approx(c.height));
                CHECK(zBefore == Approx(0.0F).margin(1e-4));
                CHECK(human.position().y > 40.0F);
                if (c.stick < 0.95F) {
                    CHECK(human.speed() == Approx(speedBefore).margin(1e-3));
                    CHECK(human.animator().animId() == clipBefore);
                }
            }
        }
        CHECK(rises == 1);
        CHECK_FALSE(human.airborne());
        CHECK(human.position().y > 41.0F);
    }
}

TEST_CASE("a face from 0.25 m stops the body where its 0.4704 m sphere meets the face's top edge",
          "[human][traversal]") {
    // The runtime's stops (docs/research/characters.md#walls): 0.395, 0.399 and 0.423 m from faces 0.255, 0.26 and
    // 0.30 m tall, sqrt(0.4704² − (0.5204 − h)²) + the 0.01 m kept clear; a 0.5 m ledge and a full wall 0.480 m.
    const TestCharacter character;
    struct Case {
        float height;
        float stick;
        float stop;
    };
    for (const Case c : {Case{0.255F, 1.0F, 0.398F}, Case{0.26F, 0.5F, 0.402F}, Case{0.30F, 0.5F, 0.426F},
                         Case{0.50F, 0.6F, 0.480F}, Case{3.0F, 0.35F, 0.480F}}) {
        const auto mesh =
            makeMesh(join(join(floorAt(0.0F, 0.0F, 80.0F, 0.0F, 40.0F), floorAt(c.height, 0.0F, 80.0F, 40.0F, 80.0F)),
                          coney::test::wallFacingMinusY(40.0F, 0.0F, 80.0F, 0.0F, c.height)));
        Human human(character.anims, coney::human::AnimSlots::player(), coney::test::identityBind(), 0.97F);
        human.spawn(mesh.get(), Vec3{40.0F, 37.0F, 0.0F}, 0.0F);
        hold(human, pad(0.0F, c.stick), 120, mesh.get());
        CHECK_FALSE(human.airborne());
        CHECK(human.position().z == Approx(0.0F).margin(1e-4));
        CHECK(40.0F - human.position().y == Approx(c.stop).margin(0.003));
    }
}

TEST_CASE("L2 sprints only with the stick above 0.95; a walk with L2 held neither sprints nor drains",
          "[human][traversal]") {
    const TestCharacter character;
    const auto mesh = makeMesh(ground());
    SECTION("stick 0.5 and 0.8 with L2: the walk speed, stamina full") {
        for (const float deflection : {0.5F, 0.8F}) {
            Human human = spawnHuman(character, mesh.get(), Vec3{40.0F, 5.0F, 0.0F});
            hold(human, pad(0.0F, deflection, true), 90, mesh.get());
            CHECK(human.speed() == Approx(1.5F));
            CHECK(human.sprinting());
            CHECK(human.stamina().value() == 135);
        }
    }
    SECTION("stick 0.96, full, and a full diagonal with L2: the sprint speed, stamina draining") {
        for (const auto& [x, y] : {std::pair{0.0F, 0.96F}, std::pair{0.0F, 1.0F}, std::pair{0.71F, 0.71F}}) {
            Human human = spawnHuman(character, mesh.get(), Vec3{10.0F, 5.0F, 0.0F});
            hold(human, pad(x, y, true), 45, mesh.get());
            CHECK(human.speed() == Approx(10.0F));
            CHECK(human.gait() == coney::human::Gait::Sprint);
            CHECK(human.stamina().value() < 135);
        }
    }
    SECTION("full stick without L2: the run speed") {
        Human human = spawnHuman(character, mesh.get(), Vec3{40.0F, 5.0F, 0.0F});
        hold(human, pad(0.0F, 1.0F), 45, mesh.get());
        CHECK(human.speed() == Approx(7.5F));
        CHECK_FALSE(human.sprinting());
    }
}

TEST_CASE("empty stamina drops the sprint to the run at once and stays empty until L2 is let go",
          "[human][traversal]") {
    const TestCharacter character;
    // A long lane along +x so the sprint has room.
    const auto mesh = makeMesh(ground(), 8);
    Human human = spawnHuman(character, mesh.get(), Vec3{40.0F, 40.0F, 0.0F});
    // Run in circles at full stick (the stick turning slowly, so it never skids) with L2 until stamina is gone
    // (6.75 s of sprinting after the run-up).
    int updates = 0;
    const auto circling = [&updates](bool l2) {
        const float angle = static_cast<float>(updates) * 0.02F;
        ++updates;
        return pad(std::sin(angle), std::cos(angle), l2);
    };
    while (human.stamina().value() > 0 && updates < 400) {
        human.step(circling(true), mesh.get());
    }
    REQUIRE(human.stamina().value() == 0);
    CHECK(updates > 200);
    CHECK_FALSE(human.sprinting());
    human.step(circling(true), mesh.get());
    CHECK(human.speed() == Approx(7.5F));
    // L2 still held: no refill at the run.
    for (int i = 0; i < 45; ++i) {
        human.step(circling(true), mesh.get());
    }
    CHECK(human.stamina().value() == 0);
    CHECK(human.speed() == Approx(7.5F));
    // L2 let go: 40 a second, still running.
    for (int i = 0; i < 30; ++i) {
        human.step(circling(false), mesh.get());
    }
    CHECK(human.stamina().value() >= 39);
    CHECK(human.stamina().value() <= 41);
    // L2 again: a sprint at once.
    for (int i = 0; i < 6; ++i) {
        human.step(circling(true), mesh.get());
    }
    CHECK(human.speed() == Approx(10.0F));
}

TEST_CASE("letting go in a sprint or a run plays the run stop; a reversal at a run turns one step and skids too",
          "[human][traversal]") {
    const TestCharacter character;
    const auto mesh = makeMesh(ground());
    Human sprinter = spawnHuman(character, mesh.get(), Vec3{40.0F, 5.0F, 0.0F});
    hold(sprinter, pad(0.0F, 1.0F, true), 45, mesh.get());
    REQUIRE(sprinter.speed() == Approx(10.0F));
    sprinter.step(pad(0.0F, 0.0F), mesh.get());
    CHECK(sprinter.animator().state() == AnimState::RunStop);
    CHECK(sprinter.animator().animId() == 417U);
    CHECK(sprinter.traversal() == Traversal::RunStop);
    // The run stop moves the body by its root (2 m/s at rate 0.75) once its fade is over, then the idle.
    hold(sprinter, pad(0.0F, 0.0F), 5, mesh.get());
    CHECK(sprinter.speed() == Approx(1.5F));
    hold(sprinter, pad(0.0F, 0.0F), 30, mesh.get());
    CHECK(sprinter.animator().state() == AnimState::Idle);
    CHECK(sprinter.speed() == 0.0F);

    Human runner = spawnHuman(character, mesh.get(), Vec3{20.0F, 5.0F, 0.0F});
    hold(runner, pad(0.0F, 1.0F), 45, mesh.get());
    REQUIRE(runner.speed() == Approx(7.5F));
    runner.step(pad(0.0F, 0.0F), mesh.get());
    CHECK(runner.animator().state() == AnimState::RunStop);
    CHECK(runner.animator().animId() == 417U);

    // Reversed at a run: one turn step of the run's limit, then the run stop holds the facing while it slides.
    Human reverser = spawnHuman(character, mesh.get(), Vec3{60.0F, 5.0F, 0.0F});
    hold(reverser, pad(0.0F, 1.0F), 45, mesh.get());
    const float heading = reverser.heading();
    reverser.step(pad(0.0F, -1.0F), mesh.get());
    CHECK(reverser.animator().state() == AnimState::RunStop);
    const float turned = std::abs(coney::human::wrapAngle(reverser.heading() - heading));
    CHECK(turned == Approx(coney::human::maxTurn(coney::human::Gait::Run)).margin(1e-4));
    hold(reverser, pad(0.0F, -1.0F), 5, mesh.get());
    CHECK(std::abs(coney::human::wrapAngle(reverser.heading() - heading)) == Approx(turned).margin(1e-4));
}

TEST_CASE("a jump from a run leaves at 5.5 m/s up and the run speed, rises 1.06 m and lands running",
          "[human][traversal]") {
    const TestCharacter character;
    const auto mesh = makeMesh(ground());
    Human human = spawnHuman(character, mesh.get(), Vec3{40.0F, 5.0F, 0.0F});
    hold(human, pad(0.0F, 1.0F), 45, mesh.get());
    REQUIRE(human.speed() == Approx(7.5F));
    const Vec3 takeOff = human.position();
    human.step(pad(0.0F, 1.0F, false, true), mesh.get());
    REQUIRE(human.airborne());
    CHECK(human.traversal() == Traversal::Jumping);
    CHECK(human.velocity().z == Approx(5.5F));
    CHECK(human.speed() == Approx(7.5F));
    CHECK(human.animator().animId() == 434U);
    // No gravity on the first airborne update, then 15.68 m/s² (0.5227 m/s an update).
    human.step(pad(0.0F, 1.0F), mesh.get());
    CHECK(human.velocity().z == Approx(5.5F));
    human.step(pad(0.0F, 1.0F), mesh.get());
    CHECK(human.velocity().z == Approx(5.5F - 15.68F / 30.0F));
    float apex = human.position().z;
    int airborne = 3;
    while (human.airborne() && airborne < 60) {
        human.step(pad(0.0F, 1.0F), mesh.get());
        apex = std::max(apex, human.position().z);
        CHECK((!human.airborne() || human.speed() == Approx(7.5F)));
        ++airborne;
    }
    CHECK(apex - takeOff.z == Approx(1.058F).margin(0.01));
    CHECK(airborne >= 22);
    CHECK(airborne <= 25);
    CHECK(human.position().y - takeOff.y == Approx(static_cast<float>(airborne - 1) * 7.5F / 30.0F).margin(0.3));
    // Landing with the stick pushed: the jump end running, which moves by its root, then the run again.
    CHECK(human.traversal() == Traversal::Landing);
    CHECK(human.animator().animId() == 436U);
    hold(human, pad(0.0F, 1.0F), 60, mesh.get());
    CHECK(human.traversal() == Traversal::None);
    CHECK(human.speed() == Approx(7.5F));
}

TEST_CASE("a sprint jump leaves at the sprint speed and turns at 4 degrees an update in the air",
          "[human][traversal]") {
    const TestCharacter character;
    const auto mesh = makeMesh(ground());
    Human human = spawnHuman(character, mesh.get(), Vec3{40.0F, 5.0F, 0.0F});
    hold(human, pad(0.0F, 1.0F, true), 45, mesh.get());
    human.step(pad(0.0F, 1.0F, true, true), mesh.get());
    REQUIRE(human.airborne());
    CHECK(human.speed() == Approx(10.0F));
    // The stick turned right: the heading follows at most 4° an update, the speed kept.
    float last = human.heading();
    for (int i = 0; i < 10 && human.airborne(); ++i) {
        human.step(pad(0.8F, 0.0F, true), mesh.get());
        CHECK(std::abs(coney::human::wrapAngle(human.heading() - last)) <= 4.0F * kPi / 180.0F + 1e-5F);
        CHECK(human.speed() == Approx(10.0F));
        last = human.heading();
    }
    CHECK(human.heading() < -0.3F);
}

TEST_CASE("triangle does not jump at a walk, in the run start, or within 5.5 m of a climbable wall",
          "[human][traversal]") {
    const TestCharacter character;
    // A climbable block too tall to climb (3.2 m) with its face at y = 40.
    const auto mesh =
        makeMesh(join(ground(), blockAlongY(0.0F, 80.0F, 40.0F, 43.0F, 3.2F, coney::human::kTriangleClimbable)));
    SECTION("a walk at 0.8") {
        Human human = spawnHuman(character, mesh.get(), Vec3{40.0F, 5.0F, 0.0F});
        hold(human, pad(0.0F, 0.8F), 30, mesh.get());
        human.step(pad(0.0F, 0.8F, false, true), mesh.get());
        CHECK_FALSE(human.airborne());
    }
    SECTION("the run start") {
        Human human = spawnHuman(character, mesh.get(), Vec3{40.0F, 5.0F, 0.0F});
        human.step(pad(0.0F, 1.0F), mesh.get());
        human.step(pad(0.0F, 1.0F, false, true), mesh.get());
        CHECK_FALSE(human.airborne());
    }
    SECTION("running at the wall: a jump 6 m away, none 5 m away") {
        Human far = spawnHuman(character, mesh.get(), Vec3{40.0F, 20.0F, 0.0F});
        hold(far, pad(0.0F, 1.0F), 45, mesh.get());
        while (far.position().y < 33.9F) {
            far.step(pad(0.0F, 1.0F), mesh.get());
        }
        far.step(pad(0.0F, 1.0F, false, true), mesh.get());
        CHECK(far.airborne());
        Human near = spawnHuman(character, mesh.get(), Vec3{40.0F, 20.0F, 0.0F});
        hold(near, pad(0.0F, 1.0F), 45, mesh.get());
        while (near.position().y < 34.9F) {
            near.step(pad(0.0F, 1.0F), mesh.get());
        }
        near.step(pad(0.0F, 1.0F, false, true), mesh.get());
        CHECK_FALSE(near.airborne());
        CHECK(near.traversal() == Traversal::None);
    }
}

TEST_CASE("standing at a 1.2 m climbable block, triangle climbs it as a short wall onto its top",
          "[human][traversal]") {
    const TestCharacter character;
    const auto mesh =
        makeMesh(join(ground(), blockAlongY(30.0F, 50.0F, 40.0F, 43.0F, 1.2F, coney::human::kTriangleClimbable)));
    Human human = spawnHuman(character, mesh.get(), Vec3{40.0F, 39.0F, 0.0F});
    human.step(pad(0.0F, 0.5F, false, true), mesh.get());
    REQUIRE(human.traversal() == Traversal::Climbing);
    const auto& climb = human.climb();
    REQUIRE(climb.has_value());
    if (climb) {
        CHECK(climb->kind == coney::human::ClimbKind::ShortWall);
    }
    CHECK(human.animator().animId() == 455U);
    // The move to the start point, 0.9 of the standing reach (0.8) in front of the face, then the clips.
    human.step(pad(0.0F, 0.5F), mesh.get());
    CHECK(human.position().y == Approx(40.0F - 0.72F).margin(0.01));
    // The feet rise in one move at the second clip's start, onto the top.
    float lastZ = human.position().z;
    int rises = 0;
    for (int i = 0; i < 45; ++i) {
        human.step(pad(0.0F, 0.5F), mesh.get());
        rises += human.position().z > lastZ + 0.5F ? 1 : 0;
        lastZ = human.position().z;
    }
    CHECK(rises == 1);
    CHECK(human.position().z == Approx(1.2F));
    CHECK(human.position().y > 40.2F);
    hold(human, pad(0.0F, 0.5F), 30, mesh.get());
    CHECK(human.traversal() == Traversal::None);
    CHECK(human.position().z == Approx(1.2F));
    CHECK_FALSE(human.airborne());
}

TEST_CASE("a standing climb needs the face within reach: 1.3 m away triangle does nothing", "[human][traversal]") {
    const TestCharacter character;
    const auto mesh =
        makeMesh(join(ground(), blockAlongY(30.0F, 50.0F, 40.0F, 43.0F, 1.2F, coney::human::kTriangleClimbable)));
    Human human = spawnHuman(character, mesh.get(), Vec3{40.0F, 38.7F, 0.0F});
    human.step(pad(0.0F, 0.5F, false, true), mesh.get());
    CHECK(human.traversal() == Traversal::None);
}

TEST_CASE("a fence stops a walk, and a running climb carries the body through it", "[human][traversal]") {
    const TestCharacter character;
    // A 1 m fence of material 30 (LOW_FENCE), 8 cm thick, its face at y = 40.
    const auto mesh =
        makeMesh(join(ground(), blockAlongY(30.0F, 50.0F, 40.0F, 40.08F, 1.0F, 0, coney::human::kMaterialLowFence)));
    SECTION("walked into, it is a wall") {
        Human human = spawnHuman(character, mesh.get(), Vec3{40.0F, 37.0F, 0.0F});
        hold(human, pad(0.0F, 0.6F), 120, mesh.get());
        CHECK(human.position().y == Approx(40.0F - coney::human::playerWalkingRadius(1.0F)).margin(0.02));
    }
    SECTION("from a run, triangle 3 m before it") {
        Human human = spawnHuman(character, mesh.get(), Vec3{40.0F, 20.0F, 0.0F});
        hold(human, pad(0.0F, 1.0F), 45, mesh.get());
        while (human.position().y < 37.0F) {
            human.step(pad(0.0F, 1.0F), mesh.get());
        }
        human.step(pad(0.0F, 1.0F, false, true), mesh.get());
        REQUIRE(human.traversal() == Traversal::Climbing);
        CHECK(human.animator().animId() == 446U); // the short fence's running chain
        float highest = 0.0F;
        // Each update's move along +y, and the clip it ended in.
        std::vector<float> moved;
        std::vector<std::uint32_t> clips;
        for (int i = 0; i < 60 && human.traversal() == Traversal::Climbing; ++i) {
            const float before = human.position().y;
            human.step(pad(0.0F, 1.0F), mesh.get());
            moved.push_back(human.position().y - before);
            clips.push_back(human.animator().animId());
            highest = std::max(highest, human.position().z);
        }
        // The first clip ran through the 2-update move to the start point, unfaded: on the next update its root
        // motion moves the body at its full 3 m/s × 0.75. The second and third clips move nothing on their first
        // update, then go at their own speed (docs/research/characters.md#climb).
        REQUIRE(moved.size() > 3);
        CHECK(clips[2] == 446U);
        CHECK(moved[2] == Approx(3.0F * 0.75F / 30.0F).margin(1e-4));
        for (const std::uint32_t id : {447U, 448U}) {
            const auto first = std::ranges::find(clips, id);
            REQUIRE(first != clips.end());
            const auto index = static_cast<std::size_t>(first - clips.begin());
            CHECK(moved[index] == Approx(0.0F).margin(1e-5));
            REQUIRE(index + 1 < moved.size());
            CHECK(moved[index + 1] > 0.01F);
        }
        CHECK(human.traversal() == Traversal::None);
        CHECK(human.position().y > 40.3F);
        CHECK(highest == Approx(0.0F).margin(1e-4)); // the feet stay at the ground's height
        hold(human, pad(0.0F, 1.0F), 30, mesh.get());
        CHECK(human.speed() == Approx(7.5F));
        CHECK(human.position().y > 43.0F);
    }
}

TEST_CASE("walking north into a corner's edge, the body slides round it where the edge is named",
          "[human][traversal]") {
    // A building's convex corner at (40, 40), as level5's at (-60.2, -163.7): the player walks due north 0.2 m east of
    // it, so his sphere meets only the vertical edge. With the edge named the corner slide carries him round it
    // (docs/research/characters.md#walls, step 6: past in about a second at runtime); a push along the face's normal
    // alone holds him there.
    const TestCharacter character;
    for (const bool named : {true, false}) {
        const auto mesh = makeMesh(join(ground(), coney::test::convexCorner(30.0F, 40.0F, 40.0F, 50.0F, 3.0F, named)));
        Human human(character.anims, coney::human::AnimSlots::player(), coney::test::identityBind(), 0.97F);
        human.spawn(mesh.get(), Vec3{40.2F, 37.0F, 0.0F}, 0.0F);
        hold(human, pad(0.0F, 0.5F), 240, mesh.get());
        if (named) {
            CHECK(human.position().y > 41.0F);
            CHECK(human.position().x > 40.0F + 0.47F);
        } else {
            CHECK(human.position().y < 40.0F - 0.3F);
        }
    }
}
