// SPDX-License-Identifier: GPL-3.0-or-later
#include "human/human.h"

#include <cmath>
#include <memory>
#include <numbers>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "characters/anim_set.h"
#include "human/body.h"
#include "human/human_animator.h"
#include "support/collision_fixtures.h"
#include "support/human_fixtures.h"

using Catch::Approx;
using coney::anim::Vec3;
using coney::human::AnimState;
using coney::human::Human;
using coney::human::HumanInput;

namespace {

// The camera the tests turn the stick with: looking along +y, so stick up walks along +y.
constexpr Vec3 kAlongY{0.0F, 1.0F, 0.0F};

// A stick at `x`, `y` seen from a camera looking along +y.
HumanInput stick(float x, float y) { return HumanInput{.stickX = x, .stickY = y, .cameraForward = kAlongY}; }

// The synthetic character and its anim set, held together for a test.
struct TestCharacter {
    coney::characters::CharacterData data = coney::test::locomotionData();
    coney::characters::AnimSet anims{data, nullptr};
};

// A human of `character` spawned at `at` on `mesh`.
Human spawnHuman(const TestCharacter& character, const coney::raycast::CollisionMesh* mesh, Vec3 at) {
    Human human(character.anims, coney::human::AnimSlots::player(), coney::test::identityBind());
    human.spawn(mesh, at, 0.0F);
    return human;
}

} // namespace

TEST_CASE("the anim set gives each clip its range-flag rate and its root-motion speed", "[human]") {
    const TestCharacter character;
    CHECK(character.anims.rate(408) == Approx(1.0F));  // flag 0x1000
    CHECK(character.anims.rate(413) == Approx(0.75F)); // no flag
    CHECK(character.anims.speed(408) == Approx(1.5F));
    CHECK(character.anims.speed(410) == Approx(7.5F));
    const coney::human::Speeds speeds = coney::human::speedsOf(character.anims, coney::human::AnimSlots::player());
    CHECK(speeds.walk == Approx(1.5F));
    CHECK(speeds.sprint == Approx(10.0F));
    CHECK(coney::human::HumanAnimator::clipsMissing(character.anims, coney::human::AnimSlots::player()) == 0);
}

TEST_CASE("a human is created 0.01 above the ground and then stands exactly on it", "[human]") {
    const TestCharacter character;
    const auto mesh = coney::test::makeMesh(coney::test::floorAt(2.0F, 0.0F, 80.0F, 0.0F, 80.0F));
    Human human = spawnHuman(character, mesh.get(), Vec3{40.0F, 40.0F, 2.8F});
    CHECK(human.position().z == Approx(2.01F));
    human.step(stick(0.0F, 0.0F), mesh.get());
    CHECK(human.position().z == Approx(2.0F));
    CHECK_FALSE(human.airborne());
    CHECK(human.animator().state() == AnimState::Idle);
}

TEST_CASE("a walk starts with the walk start's root motion, then walks at the walk speed whatever the stick",
          "[human]") {
    const TestCharacter character;
    const auto mesh = coney::test::makeMesh(coney::test::floorAt(0.0F, 0.0F, 80.0F, 0.0F, 80.0F));
    for (const float deflection : {0.3F, 0.6F, 0.9F}) {
        Human human = spawnHuman(character, mesh.get(), Vec3{40.0F, 10.0F, 0.0F});
        human.step(stick(0.0F, deflection), mesh.get()); // the first update picks the move state
        CHECK(human.animator().startClipPlaying());
        human.step(stick(0.0F, deflection), mesh.get());
        // The walk start's own root velocity, 1.0 at its rate of 0.75, is the only velocity.
        CHECK(human.speed() == Approx(0.75F));
        for (int i = 0; i < 40; ++i) {
            human.step(stick(0.0F, deflection), mesh.get());
        }
        CHECK(!human.animator().startClipPlaying());
        CHECK(human.speed() == Approx(1.5F));
        CHECK(human.gait() == coney::human::Gait::Walk);
        CHECK(human.animator().animId() == 408U);
        CHECK(human.heading() == Approx(0.0F).margin(1e-5));
        CHECK(human.position().y > 10.0F);
        CHECK(human.position().x == Approx(40.0F).margin(1e-4));
    }
}

TEST_CASE("a full stick runs: the run start, then 0.8 m/s more each update up to the run speed", "[human]") {
    const TestCharacter character;
    const auto mesh = coney::test::makeMesh(coney::test::floorAt(0.0F, 0.0F, 80.0F, 0.0F, 80.0F));
    Human human = spawnHuman(character, mesh.get(), Vec3{40.0F, 5.0F, 0.0F});
    human.step(stick(0.0F, 1.0F), mesh.get());
    CHECK(human.animator().animId() == 414U);
    float last = 0.0F;
    for (int i = 0; i < 40; ++i) {
        human.step(stick(0.0F, 1.0F), mesh.get());
        if (!human.animator().startClipPlaying() && human.speed() < 7.5F && last > 0.0F) {
            CHECK(human.speed() - last <= 0.8F + 1e-4F);
        }
        last = human.speed();
    }
    CHECK(human.speed() == Approx(7.5F));
    CHECK(human.gait() == coney::human::Gait::Run);
    CHECK(human.animator().gaitValue() == Approx(2.0F));
    // Let go: the speed is gone at once and the idle comes back.
    human.step(stick(0.0F, 0.0F), mesh.get());
    CHECK(human.speed() == 0.0F);
    CHECK(human.animator().state() == AnimState::Idle);
}

TEST_CASE("a human turns toward the stick at no more than its turn limit", "[human]") {
    const TestCharacter character;
    const auto mesh = coney::test::makeMesh(coney::test::floorAt(0.0F, 0.0F, 80.0F, 0.0F, 80.0F));
    Human human = spawnHuman(character, mesh.get(), Vec3{40.0F, 40.0F, 0.0F});
    // Stick right: a heading of -90°.
    float last = human.heading();
    for (int i = 0; i < 30; ++i) {
        human.step(stick(0.6F, 0.0F), mesh.get());
        CHECK(std::abs(human.heading() - last) <= coney::human::maxTurn(coney::human::Gait::Walk) + 1e-5F);
        last = human.heading();
    }
    CHECK(human.heading() == Approx(-std::numbers::pi_v<float> / 2.0F).margin(1e-4));
}

TEST_CASE("walking off a ledge falls with gravity from the second airborne update and lands below", "[human]") {
    const TestCharacter character;
    // A ledge 3 m high ending at y = 20, the ground below it.
    const auto mesh = coney::test::makeMesh(coney::test::join(coney::test::floorAt(3.0F, 0.0F, 80.0F, 0.0F, 20.0F),
                                                              coney::test::floorAt(0.0F, 0.0F, 80.0F, 0.0F, 80.0F)));
    Human human = spawnHuman(character, mesh.get(), Vec3{40.0F, 17.0F, 3.0F});
    int airborneUpdates = 0;
    float firstFall = 1.0F;
    float secondFall = 1.0F;
    for (int i = 0; i < 120; ++i) {
        human.step(stick(0.0F, 0.5F), mesh.get());
        if (!human.airborne()) {
            continue;
        }
        // The vertical speed after the first and second airborne updates (the update that found no ground is the
        // first, and its own z speed is still 0).
        ++airborneUpdates;
        if (airborneUpdates == 2) {
            firstFall = human.velocity().z;
        } else if (airborneUpdates == 3) {
            secondFall = human.velocity().z;
        }
    }
    CHECK(airborneUpdates > 2);
    CHECK(firstFall == 0.0F);
    CHECK(secondFall == Approx(-15.68F / 30.0F));
    CHECK_FALSE(human.airborne());
    CHECK(human.position().z == Approx(0.0F).margin(1e-4));
    CHECK(human.lastLandingSpeed() < -1.0F);
    CHECK(human.velocity().z == 0.0F);
}

TEST_CASE("a step up to 1 m is climbed by the ground snap, a deeper drop is a fall", "[human]") {
    const TestCharacter character;
    const auto mesh = coney::test::makeMesh(coney::test::join(coney::test::floorAt(0.0F, 0.0F, 80.0F, 0.0F, 20.0F),
                                                              coney::test::floorAt(0.4F, 0.0F, 80.0F, 20.0F, 80.0F)));
    Human human = spawnHuman(character, mesh.get(), Vec3{40.0F, 18.0F, 0.0F});
    for (int i = 0; i < 60; ++i) {
        human.step(stick(0.0F, 0.5F), mesh.get());
        CHECK(!human.airborne());
    }
    CHECK(human.position().y > 20.0F);
    CHECK(human.position().z == Approx(0.4F));
}

TEST_CASE("a wall stops the body a radius away and lets it slide along", "[human]") {
    const TestCharacter character;
    // A wall across x = 50 facing -x, from the ground up.
    const auto mesh =
        coney::test::makeMesh(coney::test::join(coney::test::floorAt(0.0F, 0.0F, 80.0F, 0.0F, 80.0F),
                                                coney::test::wallFacingMinusX(50.0F, 0.0F, 80.0F, -1.0F, 5.0F)));
    Human human = spawnHuman(character, mesh.get(), Vec3{45.0F, 40.0F, 0.0F});
    // Walk straight into it (stick right), then diagonally (up and right): the body slides along +y.
    for (int i = 0; i < 150; ++i) {
        human.step(stick(1.0F, 0.0F), mesh.get());
    }
    CHECK(human.position().x == Approx(50.0F - coney::human::walkingRadius(1.0F)).margin(0.02));
    const float y = human.position().y;
    for (int i = 0; i < 30; ++i) {
        human.step(stick(0.7F, 0.7F), mesh.get());
    }
    CHECK(human.position().x <= 50.0F - coney::human::walkingRadius(1.0F) + 0.02F);
    CHECK(human.position().y > y + 1.0F);
}

TEST_CASE("the stick is camera-relative: up walks away from the camera", "[human]") {
    const TestCharacter character;
    const auto mesh = coney::test::makeMesh(coney::test::floorAt(0.0F, 0.0F, 80.0F, 0.0F, 80.0F));
    Human human = spawnHuman(character, mesh.get(), Vec3{40.0F, 40.0F, 0.0F});
    // The camera looks along -x: stick up walks along -x.
    for (int i = 0; i < 60; ++i) {
        human.step(HumanInput{.stickX = 0.0F, .stickY = 0.6F, .cameraForward = Vec3{-1.0F, 0.0F, -0.2F}}, mesh.get());
    }
    CHECK(human.position().x < 38.0F);
    CHECK(human.position().y == Approx(40.0F).margin(0.3));
    CHECK(human.heading() == Approx(std::numbers::pi_v<float> / 2.0F).margin(1e-3));
}
