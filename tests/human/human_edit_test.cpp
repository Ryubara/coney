// SPDX-License-Identifier: GPL-3.0-or-later
// What the Entity editor (docs/guides/debug-menu.md#entity-editor) and the Movement tunables change on a human: a speed
// scale on its gaits (one per entity, and the player's own per gait), the meters as percentages, a kill, the animation
// rate, and the body scale with the collision following the mesh or staying as configured. Synthetic character; no
// game data.
#include <cmath>
#include <cstdint>
#include <memory>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "characters/anim_set.h"
#include "combat/meters.h"
#include "combat/player_combat.h"
#include "human/body.h"
#include "human/fighter.h"
#include "human/human.h"
#include "human/human_animator.h"
#include "human/locomotion.h"
#include "support/collision_fixtures.h"
#include "support/human_fixtures.h"

using Catch::Approx;
using coney::anim::Vec3;
using coney::human::Human;
using coney::human::HumanInput;

namespace {

// The camera the tests turn the stick with: looking along +y, so stick up walks along +y.
constexpr Vec3 kAlongY{0.0F, 1.0F, 0.0F};

// A stick at `x`, `y` seen from a camera looking along +y.
HumanInput stick(float x, float y) {
    return HumanInput{.stickX = x, .stickY = y, .cameraForward = kAlongY, .targets = {}};
}

// The synthetic character and its anim set, held together for a test.
struct TestCharacter {
    coney::characters::CharacterData data = coney::test::locomotionData();
    coney::characters::AnimSet anims{data, nullptr};
};

// A human of `character` spawned at `at` on `mesh`; a player unless `player` says otherwise.
Human spawnHuman(const TestCharacter& character, const coney::raycast::CollisionMesh* mesh, Vec3 at,
                 bool player = true) {
    Human human(character.anims, coney::human::AnimSlots::player(), coney::test::identityBind());
    human.spawn(mesh, at, 0.0F);
    if (!player) {
        human.setFighterProfile(coney::human::FighterProfile{
            .player = false, .powerClass = coney::combat::kCivilianPowerClass, .health = 600});
    }
    return human;
}

// Walks the human with the stick at `deflection` until the walk start is over and its speed has settled.
void walkSteadily(Human& human, const coney::raycast::CollisionMesh* mesh, float deflection) {
    for (int i = 0; i < 60; ++i) {
        human.step(stick(0.0F, deflection), mesh);
    }
}

// Puts the locomotion tunables back as they were when the test ends.
struct TuningGuard {
    coney::human::LocomotionTuning saved = coney::human::locomotionTuning();
    ~TuningGuard() { coney::human::locomotionTuning() = saved; }
    TuningGuard() = default;
    TuningGuard(const TuningGuard&) = delete;
    TuningGuard& operator=(const TuningGuard&) = delete;
};

} // namespace

TEST_CASE("a speed scale on a human multiplies the speed of every gait it walks, runs and sprints at",
          "[human][debug]") {
    // Expected: unverified, #723 (Coney's own debug tooling; the base speeds are characters.md#speed-classes)
    const TestCharacter character;
    const auto mesh = coney::test::makeMesh(coney::test::floorAt(0.0F, 0.0F, 400.0F, 0.0F, 400.0F));
    Human human = spawnHuman(character, mesh.get(), Vec3{200.0F, 5.0F, 0.0F});
    CHECK(human.speedScale() == Approx(1.0F));
    walkSteadily(human, mesh.get(), 0.6F);
    CHECK(human.speed() == Approx(1.5F));

    human.setSpeedScale(2.0F);
    CHECK(human.speeds().walk == Approx(3.0F));
    CHECK(human.speeds().run == Approx(15.0F));
    CHECK(human.speeds().sprint == Approx(20.0F));
    // The next step takes the new scale: the walk settles at twice the speed (0.8 m/s more each update).
    for (int i = 0; i < 10; ++i) {
        human.step(stick(0.0F, 0.6F), mesh.get());
    }
    CHECK(human.speed() == Approx(3.0F));
    // The gait follows the scaled speeds: 3.0 m/s is a walk of this human, not a jog.
    CHECK(human.gait() == coney::human::Gait::Walk);

    // Back to 1: the speeds are the clips' again.
    human.setSpeedScale(1.0F);
    CHECK(human.speeds().walk == Approx(1.5F));
}

TEST_CASE("the player's gait scales change the player's speeds only, and each gait on its own", "[human][debug]") {
    // Expected: unverified, #723
    const TuningGuard guard;
    const TestCharacter character;
    const auto mesh = coney::test::makeMesh(coney::test::floorAt(0.0F, 0.0F, 400.0F, 0.0F, 400.0F));
    coney::human::locomotionTuning().walkSpeedScale = 2.0F;
    coney::human::locomotionTuning().runSpeedScale = 0.5F;

    Human player = spawnHuman(character, mesh.get(), Vec3{100.0F, 5.0F, 0.0F});
    Human other = spawnHuman(character, mesh.get(), Vec3{300.0F, 5.0F, 0.0F}, false);
    walkSteadily(player, mesh.get(), 0.6F);
    walkSteadily(other, mesh.get(), 0.6F);
    CHECK(player.speeds().walk == Approx(3.0F));
    CHECK(player.speed() == Approx(3.0F));
    CHECK(player.speeds().jog == Approx(4.0F));
    CHECK(player.speeds().run == Approx(3.75F));
    CHECK(player.speeds().sprint == Approx(10.0F));
    // A human that is not a player keeps the clips' speeds.
    CHECK(other.speeds().walk == Approx(1.5F));
    CHECK(other.speeds().run == Approx(7.5F));
    CHECK(other.speed() == Approx(1.5F));

    // The scales and the entity's own multiply.
    coney::human::locomotionTuning().sprintSpeedScale = 1.5F;
    coney::human::locomotionTuning().sneakSpeedScale = 0.5F;
    coney::human::locomotionTuning().jogSpeedScale = 1.25F;
    player.setSpeedScale(2.0F);
    player.step(stick(0.0F, 0.6F), mesh.get());
    CHECK(player.speeds().sprint == Approx(30.0F));
    CHECK(player.speeds().sneak == Approx(1.2F));
    CHECK(player.speeds().jog == Approx(10.0F));
    // The combat walk's factor is the entity scale alone.
    CHECK(player.speeds().base == Approx(other.speeds().base * 2.0F));
}

TEST_CASE("the meters take a percentage of their maximum, and a kill empties the health", "[human][debug]") {
    // Expected: unverified, #723
    const TestCharacter character;
    const auto mesh = coney::test::makeMesh(coney::test::floorAt(0.0F, 0.0F, 80.0F, 0.0F, 80.0F));
    Human human = spawnHuman(character, mesh.get(), Vec3{40.0F, 40.0F, 0.0F});
    REQUIRE(human.health().maximum() == 900);
    REQUIRE(human.stamina().maximum() == 135);

    human.setHealthShare(50.0F);
    CHECK(human.health().value() == 450);
    human.setHealthShare(1.0F);
    CHECK(human.health().value() == 9);
    // Never below one point: 0 is a kill, which is its own edit.
    human.setHealthShare(0.0F);
    CHECK(human.health().value() == 1);
    human.setHealthShare(250.0F);
    CHECK(human.health().value() == 900);

    human.setStaminaShare(50.0F);
    CHECK(human.stamina().value() == 68); // 67.5 rounds up
    human.setStaminaShare(0.0F);
    CHECK(human.stamina().value() == 0);
    human.setStaminaShare(100.0F);
    CHECK(human.stamina().value() == 135);

    human.setPowerShare(25.0F);
    CHECK(human.fighter().combat().power().value() == 100);
    human.setRageShare(50.0F);
    CHECK(human.fighter().combat().rage().value() == 39);

    human.kill();
    CHECK(human.health().value() == 0);
    CHECK(human.health().depleted());
}

TEST_CASE("an animation rate scales how fast every clip plays and how far its root motion carries", "[human][debug]") {
    // Expected: unverified, #723
    const TestCharacter character;
    const auto mesh = coney::test::makeMesh(coney::test::floorAt(0.0F, 0.0F, 400.0F, 0.0F, 400.0F));
    Human slow = spawnHuman(character, mesh.get(), Vec3{100.0F, 5.0F, 0.0F});
    Human fast = spawnHuman(character, mesh.get(), Vec3{300.0F, 5.0F, 0.0F});
    CHECK(fast.animRate() == Approx(1.0F));
    fast.setAnimRate(2.0F);
    CHECK(fast.animRate() == Approx(2.0F));
    // The idle's clip time moves twice as far in the same steps.
    for (int i = 0; i < 6; ++i) {
        slow.step(stick(0.0F, 0.0F), mesh.get());
        fast.step(stick(0.0F, 0.0F), mesh.get());
    }
    REQUIRE(slow.animator().tasks().top() != nullptr);
    REQUIRE(fast.animator().tasks().top() != nullptr);
    CHECK(fast.animator().tasks().top()->time() == Approx(slow.animator().tasks().top()->time() * 2.0F));
    // A rate of 0 holds the clip.
    fast.setAnimRate(0.0F);
    const float held = fast.animator().tasks().top()->time();
    fast.step(stick(0.0F, 0.0F), mesh.get());
    CHECK(fast.animator().tasks().top()->time() == Approx(held));
}

TEST_CASE("scaling a human scales its collision with the mesh unless it is switched off", "[human][debug]") {
    // Expected: unverified, #723 (the walking sphere's size is characters.md#walls; the toggle is Coney's own)
    const TestCharacter character;
    const auto mesh = coney::test::makeMesh(coney::test::floorAt(0.0F, 0.0F, 80.0F, 0.0F, 80.0F));
    Human human = spawnHuman(character, mesh.get(), Vec3{40.0F, 40.0F, 0.0F});
    const float sphere = human.walkingSphereRadius();
    const float air = human.airSphereRadius();
    const float capsule = human.capsuleRadius();
    const float top = human.uprightCapsule().top;
    CHECK(human.collisionFollowsMesh());
    CHECK(human.configuredScale() == Approx(human.scale()));

    // By default a giant collides at its drawn size.
    const float configured = human.scale();
    human.setScale(configured * 2.0F);
    CHECK(human.collisionScale() == Approx(configured * 2.0F));
    CHECK(human.walkingSphereRadius() > sphere * 1.9F);
    CHECK(human.airSphereRadius() == Approx(air * 2.0F));
    CHECK(human.capsuleRadius() == Approx(capsule * 2.0F));
    CHECK(human.uprightCapsule().top == Approx(top * 2.0F));

    // With the toggle off the mesh is as big and the collision stays as configured.
    human.setCollisionFollowsMesh(false);
    CHECK_FALSE(human.collisionFollowsMesh());
    CHECK(human.scale() == Approx(configured * 2.0F));
    CHECK(human.collisionScale() == Approx(configured));
    CHECK(human.walkingSphereRadius() == Approx(sphere));
    CHECK(human.airSphereRadius() == Approx(air));
    CHECK(human.capsuleRadius() == Approx(capsule));
    CHECK(human.uprightCapsule().top == Approx(top));
    // Scaled again with the toggle still off: the collision still does not move.
    human.setScale(configured * 3.0F);
    CHECK(human.walkingSphereRadius() == Approx(sphere));

    // Back on: the collision follows the mesh again.
    human.setCollisionFollowsMesh(true);
    CHECK(human.collisionScale() == Approx(configured * 3.0F));
    CHECK(human.airSphereRadius() == Approx(air * 3.0F));
}

TEST_CASE("an extreme scale, with the collision following or not, leaves the human on the floor and finite",
          "[human][debug]") {
    // Expected: unverified, #723 (extreme scales may misbehave, but must not crash or corrupt other entities)
    const TestCharacter character;
    const auto mesh = coney::test::makeMesh(coney::test::floorAt(0.0F, 0.0F, 400.0F, 0.0F, 400.0F));
    for (const bool follows : {true, false}) {
        for (const float scale : {0.05F, 3.0F, 60.0F}) {
            Human human = spawnHuman(character, mesh.get(), Vec3{200.0F, 10.0F, 0.0F});
            Human bystander = spawnHuman(character, mesh.get(), Vec3{300.0F, 10.0F, 0.0F});
            const Vec3 bystanderAt = bystander.position();
            human.setScale(scale);
            human.setCollisionFollowsMesh(follows);
            for (int i = 0; i < 60; ++i) {
                human.step(stick(0.0F, 1.0F), mesh.get());
                bystander.step(stick(0.0F, 0.0F), mesh.get());
            }
            INFO("follows " << follows << ", scale " << scale);
            CHECK(std::isfinite(human.position().x));
            CHECK(std::isfinite(human.position().y));
            CHECK(std::isfinite(human.position().z));
            CHECK(human.position().z == Approx(0.0F).margin(0.05F));
            // Another human that stood still is where it was.
            CHECK(bystander.position().x == Approx(bystanderAt.x));
            CHECK(bystander.position().y == Approx(bystanderAt.y));
        }
    }
}
