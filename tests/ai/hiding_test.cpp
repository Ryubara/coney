// SPDX-License-Identifier: GPL-3.0-or-later
#include "ai/hiding.h"

#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "ai/perception.h"
#include "raycast/collision_mesh.h"
#include "support/ai_fixtures.h"
#include "support/collision_fixtures.h"

// The ground rule of hiding and what it changes for sight (docs/research/stealth.md#shadow-ground,
// docs/research/stealth.md#seen). Synthetic floors: shadow ground below x = 40, normal ground above.

using coney::ai::HideMemory;
using coney::ai::updateHiding;
using coney::anim::Vec3;

namespace {

// The scene on a floor whose half below x = 40 is shadow ground, with `walls` added.
void useHalfShadow(coney::test::AiScene& scene, const std::vector<coney::test::Tri>& walls = {}) {
    scene.mesh = coney::test::makeMesh(coney::test::join(
        coney::test::join(coney::test::floorAt(0.0F, 0.0F, 40.0F, 0.0F, 80.0F, 1, coney::raycast::kTriangleShadow),
                          coney::test::floorAt(0.0F, 40.0F, 80.0F, 0.0F, 80.0F)),
        walls));
    scene.brains.setCollision(scene.mesh.get());
}

} // namespace

TEST_CASE("on shadow ground and hunted by nobody the player hides; off it he does not", "[ai][hiding]") {
    coney::test::AiScene scene;
    useHalfShadow(scene);
    coney::ai::Brain& player = scene.player();
    player.human().place(Vec3{20.0F, 40.0F, 0.0F}, 0.0F);
    scene.run(1);
    HideMemory memory;
    CHECK(updateHiding(scene.brains, player, memory, false).mayHide);
    CHECK(player.human().hidden());
    CHECK(memory.onShadow);
    // A molotov in hand keeps him out of it.
    CHECK_FALSE(updateHiding(scene.brains, player, memory, true).mayHide);
    CHECK_FALSE(player.human().hidden());
    // Off the shadow.
    CHECK(updateHiding(scene.brains, player, memory, false).mayHide);
    player.human().place(Vec3{60.0F, 40.0F, 0.0F}, 0.0F);
    scene.run(1);
    CHECK_FALSE(updateHiding(scene.brains, player, memory, false).mayHide);
    CHECK_FALSE(player.human().hidden());
    CHECK_FALSE(memory.onShadow);
}

TEST_CASE("a near hunter keeps the player from hiding; a far one out of sight is shaken off", "[ai][hiding]") {
    coney::test::AiScene scene;
    // A wall at y = 50 between the player and the far hunter.
    useHalfShadow(scene, coney::test::wallFacingPlusY(50.0F, 0.0F, 40.0F, 0.0F, 4.0F));
    coney::ai::Brain& player = scene.player();
    coney::ai::Brain& near = scene.add(Vec3{22.0F, 40.0F, 0.0F}, 0.0F);
    near.setTarget(&player);
    player.human().place(Vec3{20.0F, 40.0F, 0.0F}, 0.0F);
    scene.run(1);
    HideMemory memory;
    CHECK_FALSE(updateHiding(scene.brains, player, memory, false).mayHide);
    CHECK_FALSE(player.human().hidden());

    coney::test::AiScene far;
    useHalfShadow(far, coney::test::wallFacingPlusY(50.0F, 0.0F, 40.0F, 0.0F, 4.0F));
    coney::ai::Brain& hider = far.player();
    coney::ai::Brain& hunter = far.add(Vec3{20.0F, 70.0F, 0.0F}, 180.0F);
    hunter.setTarget(&hider);
    hider.human().place(Vec3{20.0F, 40.0F, 0.0F}, 0.0F);
    far.run(1);
    HideMemory shaken;
    CHECK(coney::ai::huntersOf(far.brains, hider, shaken) == 1);
    CHECK(updateHiding(far.brains, hider, shaken, false).mayHide);
    CHECK(hider.human().hidden());
    CHECK(shaken.shakenOff.size() == 1);
}

TEST_CASE("a hidden human is seen only within 2 m and only by a viewer on shadow ground", "[ai][hiding][sight]") {
    coney::test::AiScene scene;
    useHalfShadow(scene);
    coney::ai::Brain& player = scene.player();
    coney::ai::Brain& onShadow = scene.add(Vec3{38.5F, 41.0F, 0.0F}, 180.0F);
    coney::ai::Brain& farOnShadow = scene.add(Vec3{36.5F, 40.0F, 0.0F}, 270.0F);
    coney::ai::Brain& onNormal = scene.add(Vec3{41.0F, 40.0F, 0.0F}, 90.0F);
    player.human().place(Vec3{39.5F, 40.0F, 0.0F}, 0.0F);
    scene.run(1);
    const coney::raycast::CollisionMesh* mesh = scene.mesh.get();
    // Not hidden: the guard on normal ground sees him.
    CHECK(coney::ai::canSeeHuman(mesh, onNormal.human(), player.human(), 30.0F));
    HideMemory memory;
    REQUIRE(updateHiding(scene.brains, player, memory, false).mayHide);
    // Hidden: the guard on normal ground 1.5 m away does not; the one on shadow beyond 2 m does not either.
    CHECK_FALSE(coney::ai::canSeeHuman(mesh, onNormal.human(), player.human(), 30.0F));
    CHECK_FALSE(coney::ai::canSeeHuman(mesh, farOnShadow.human(), player.human(), 30.0F));
    // The guard on shadow ground 1.4 m away sees him.
    CHECK(coney::ai::canSeeHuman(mesh, onShadow.human(), player.human(), 30.0F));
}
