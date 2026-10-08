// SPDX-License-Identifier: GPL-3.0-or-later
#include <memory>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "characters/anim_set.h"
#include "human/human.h"
#include "human/human_animator.h"
#include "raycast/collision_mesh.h"
#include "support/collision_fixtures.h"
#include "support/human_fixtures.h"

// A human's side of hiding (docs/research/stealth.md#hidden, docs/research/stealth.md#sneaking): the shadow ground
// under him, the hidden state, its move style and what ends it. Synthetic clips and floors only.

using Catch::Approx;
using coney::anim::Vec3;
using coney::human::AnimState;
using coney::human::Human;
using coney::human::HumanInput;

namespace {

constexpr Vec3 kAlongY{0.0F, 1.0F, 0.0F};

// A stick at `x`, `y` seen from a camera looking along +y, L2 held when `sprint`.
HumanInput stick(float x, float y, bool sprint = false) {
    return HumanInput{.stickX = x, .stickY = y, .cameraForward = kAlongY, .sprintHeld = sprint, .targets = {}};
}

// The synthetic character and its anim set, held together for a test.
struct TestCharacter {
    coney::characters::CharacterData data = coney::test::locomotionData();
    coney::characters::AnimSet anims{data, nullptr};
};

// A floor whose half below x = 40 is shadow ground.
std::unique_ptr<coney::raycast::CollisionMesh> halfShadow() {
    return coney::test::makeMesh(
        coney::test::join(coney::test::floorAt(0.0F, 0.0F, 40.0F, 0.0F, 80.0F, 1, coney::raycast::kTriangleShadow),
                          coney::test::floorAt(0.0F, 40.0F, 80.0F, 0.0F, 80.0F)));
}

} // namespace

TEST_CASE("the ground snap tells whether a human stands on shadow ground", "[human][hiding]") {
    const TestCharacter character;
    const auto mesh = halfShadow();
    Human human(character.anims, coney::human::AnimSlots::player(), coney::test::identityBind());
    human.spawn(mesh.get(), Vec3{20.0F, 40.0F, 0.0F}, 0.0F);
    human.step(stick(0.0F, 0.0F), mesh.get());
    CHECK(human.onShadowGround());
    human.spawn(mesh.get(), Vec3{60.0F, 40.0F, 0.0F}, 0.0F);
    human.step(stick(0.0F, 0.0F), mesh.get());
    CHECK_FALSE(human.onShadowGround());
}

TEST_CASE("hidden, the idle goes through 634 to 630, the walk is 633 at its speed, and leaving plays 394",
          "[human][hiding]") {
    const TestCharacter character;
    const auto mesh = halfShadow();
    Human human(character.anims, coney::human::AnimSlots::player(), coney::test::identityBind());
    human.spawn(mesh.get(), Vec3{20.0F, 10.0F, 0.0F}, 0.0F);
    human.step(stick(0.0F, 0.0F), mesh.get());
    human.enterHiding();
    CHECK(human.hidden());
    CHECK(human.animator().stealthStyle());
    CHECK(human.animator().animId() == coney::human::kAnimStealthFromNormal);
    CHECK(human.speeds().walk == Approx(1.0F));
    for (int k = 0; k < 15; ++k) {
        human.step(stick(0.0F, 0.0F), mesh.get());
    }
    CHECK(human.animator().animId() == coney::human::kAnimStealthIdle);
    // The walk starts with the stealth walk start and goes on at the stealth walk.
    human.step(stick(0.0F, 0.5F), mesh.get());
    CHECK(human.animator().animId() == coney::human::kAnimStealthWalkStart);
    for (int k = 0; k < 30; ++k) {
        human.step(stick(0.0F, 0.5F), mesh.get());
    }
    CHECK(human.speed() == Approx(1.0F).margin(0.05F));
    CHECK(human.hidden());
    // Stopped, then cleared: the idle comes back through 394.
    for (int k = 0; k < 30; ++k) {
        human.step(stick(0.0F, 0.0F), mesh.get());
    }
    CHECK(human.animator().animId() == coney::human::kAnimStealthIdle);
    human.clearHiding();
    CHECK_FALSE(human.hidden());
    CHECK(human.animator().animId() == coney::human::kAnimNormalFromStealth);
    CHECK(human.speeds().walk == Approx(1.5F));
}

TEST_CASE("a sprint ends the hidden state and keeps it from starting", "[human][hiding]") {
    const TestCharacter character;
    const auto mesh = halfShadow();
    Human human(character.anims, coney::human::AnimSlots::player(), coney::test::identityBind());
    human.spawn(mesh.get(), Vec3{20.0F, 10.0F, 0.0F}, 0.0F);
    human.step(stick(0.0F, 0.0F), mesh.get());
    human.enterHiding();
    REQUIRE(human.hidden());
    human.step(stick(0.0F, 1.0F, true), mesh.get());
    CHECK(human.sprinting());
    CHECK_FALSE(human.hidden());
    human.enterHiding();
    CHECK_FALSE(human.hidden());
}

TEST_CASE("leaving the shadow with no target ends the hidden state at once", "[human][hiding]") {
    const TestCharacter character;
    const auto mesh = halfShadow();
    Human human(character.anims, coney::human::AnimSlots::player(), coney::test::identityBind());
    human.spawn(mesh.get(), Vec3{20.0F, 10.0F, 0.0F}, 0.0F);
    human.enterHiding();
    human.leaveHiding();
    CHECK_FALSE(human.hidden());
    CHECK_FALSE(human.animator().stealthStyle());
}
