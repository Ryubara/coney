// SPDX-License-Identifier: GPL-3.0-or-later
// The debug menus' Animation player on a human (Human::playDebugClip(), docs/guides/debug-menu.md#animation-player):
// a clip played once from start to end and the idle back, a loop until stopped, the speed, and in place. Over the
// synthetic character of support/human_fixtures.h on a flat floor, on the fixed 1/30 s step.
#include <cmath>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "animation/anim_clip.h"
#include "characters/anim_set.h"
#include "human/human.h"
#include "human/human_animator.h"
#include "support/collision_fixtures.h"
#include "support/human_fixtures.h"

using Catch::Approx;
using coney::anim::Vec3;
using coney::human::AnimState;
using coney::human::Human;
using coney::human::HumanInput;

namespace {

// The pad at rest, seen from a camera looking along +y.
const HumanInput kStill{.stickX = 0.0F, .stickY = 0.0F, .cameraForward = Vec3{0.0F, 1.0F, 0.0F}, .targets = {}};

// The synthetic character and its anim set, held together for a test.
struct TestCharacter {
    coney::characters::CharacterData data = coney::test::locomotionData();
    coney::characters::AnimSet anims{data, nullptr};
};

// A human of `character` standing idle at the middle of `mesh`.
Human standingHuman(const TestCharacter& character, const coney::raycast::CollisionMesh* mesh) {
    Human human(character.anims, coney::human::AnimSlots::player(), coney::test::identityBind());
    human.spawn(mesh, Vec3{40.0F, 40.0F, 0.0F}, 0.0F);
    human.step(kStill, mesh);
    return human;
}

// Steps `human` with the pad at rest until the debug clip stops playing, at most `limit` updates; returns the updates.
int updatesPlaying(Human& human, const coney::raycast::CollisionMesh* mesh, int limit = 1000) {
    int updates = 0;
    while (human.animator().debugClipPlaying() && updates < limit) {
        human.step(kStill, mesh);
        ++updates;
    }
    return updates;
}

} // namespace

TEST_CASE("a debug clip plays once from its first frame to its end, then the idle comes back", "[human][debug]") {
    const TestCharacter character;
    const auto mesh = coney::test::makeMesh(coney::test::floorAt(0.0F, 0.0F, 80.0F, 0.0F, 80.0F));
    Human human = standingHuman(character, mesh.get());
    // The run stop (417): 0.6 s at rate 0.75 plays 0.8 s, 24 updates.
    const coney::anim::AnimClip* clip = character.anims.clip(417);
    REQUIRE(clip != nullptr);
    human.playDebugClip(*clip, 417, character.anims.rate(417), false, true);
    CHECK(human.animator().state() == AnimState::Debug);
    CHECK(human.animator().debugClipPlaying());
    CHECK(human.animator().animId() == 417);
    REQUIRE(human.animator().tasks().top() != nullptr);
    CHECK(human.animator().tasks().top()->time() == 0.0F);
    // No fade: the clip alone makes the pose from the first update.
    CHECK(human.animator().tasks().layerCount() == 1);

    CHECK(updatesPlaying(human, mesh.get()) == 24);
    CHECK(human.animator().state() == AnimState::Idle);
    CHECK(human.animator().animId() == 388);
    // And he plays on as before: a push of the stick walks him.
    for (int i = 0; i < 20; ++i) {
        human.step(HumanInput{.stickX = 0.0F, .stickY = 0.6F, .cameraForward = Vec3{0.0F, 1.0F, 0.0F}, .targets = {}},
                   mesh.get());
    }
    CHECK(human.animator().state() == AnimState::Move);
}

TEST_CASE("a debug clip's speed scales its playing time", "[human][debug]") {
    const TestCharacter character;
    const auto mesh = coney::test::makeMesh(coney::test::floorAt(0.0F, 0.0F, 80.0F, 0.0F, 80.0F));
    Human human = standingHuman(character, mesh.get());
    const coney::anim::AnimClip* clip = character.anims.clip(417);
    REQUIRE(clip != nullptr);
    // Twice the speed: 0.4 s, 12 updates; half: 1.6 s, 48 updates.
    human.playDebugClip(*clip, 417, character.anims.rate(417) * 2.0F, false, true);
    CHECK(updatesPlaying(human, mesh.get()) == 12);
    human.playDebugClip(*clip, 417, character.anims.rate(417) * 0.5F, false, true);
    CHECK(updatesPlaying(human, mesh.get()) == 48);
}

TEST_CASE("a looping debug clip plays until it is stopped, then the idle comes back", "[human][debug]") {
    const TestCharacter character;
    const auto mesh = coney::test::makeMesh(coney::test::floorAt(0.0F, 0.0F, 80.0F, 0.0F, 80.0F));
    Human human = standingHuman(character, mesh.get());
    const coney::anim::AnimClip* clip = character.anims.clip(417);
    REQUIRE(clip != nullptr);
    human.playDebugClip(*clip, 417, 1.0F, true, true);
    // Five times its length, still playing, and its time wrapped.
    CHECK(updatesPlaying(human, mesh.get(), 90) == 90);
    CHECK(human.animator().debugClipPlaying());
    REQUIRE(human.animator().tasks().top() != nullptr);
    CHECK(human.animator().tasks().top()->time() < 0.6F);
    human.stopDebugClip();
    CHECK_FALSE(human.animator().debugClipPlaying());
    CHECK(human.animator().state() == AnimState::Idle);
    human.step(kStill, mesh.get());
    CHECK(human.animator().animId() == 388);
    // Stopping again does nothing.
    human.stopDebugClip();
    CHECK(human.animator().state() == AnimState::Idle);
}

TEST_CASE("a debug clip's root motion moves the body, unless it plays in place", "[human][debug]") {
    const TestCharacter character;
    const auto mesh = coney::test::makeMesh(coney::test::floorAt(0.0F, 0.0F, 80.0F, 0.0F, 80.0F));
    // The run stop's section A is 2 m/s along +y: 0.8 s of it at rate 0.75 moves him about 1.2 m.
    const coney::anim::AnimClip* clip = character.anims.clip(417);
    REQUIRE(clip != nullptr);
    Human moving = standingHuman(character, mesh.get());
    const Vec3 start = moving.position();
    moving.playDebugClip(*clip, 417, character.anims.rate(417), false, false);
    static_cast<void>(updatesPlaying(moving, mesh.get()));
    CHECK(moving.position().y - start.y == Approx(1.2F).margin(0.1F));

    Human still = standingHuman(character, mesh.get());
    still.playDebugClip(*clip, 417, character.anims.rate(417), false, true);
    static_cast<void>(updatesPlaying(still, mesh.get()));
    CHECK(std::abs(still.position().y - start.y) < 0.01F);
}
