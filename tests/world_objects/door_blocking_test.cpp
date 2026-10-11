// SPDX-License-Identifier: GPL-3.0-or-later
// How a closed door or barrier stops the player (docs/research/objects.md#door-blocking): its two triangles of the
// level mesh are one-sided on the disc and made two-sided by the door's spawn. The level script spawns the doors
// before the level's mesh is bound to the objects, so the spawn's writes are kept and applied when it is
// (ObjectWorld::bindCollision). A synthetic doorway walked into from both sides with the stick at a partial deflection,
// closed and opened by OpenDoor. No game data.

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "characters/anim_set.h"
#include "human/human.h"
#include "raycast/collision_mesh.h"
#include "support/collision_fixtures.h"
#include "support/human_fixtures.h"
#include "world_objects/doors.h"
#include "world_objects/glass.h"
#include "world_objects/level_objects.h"
#include "world_objects/object_services.h"

using coney::anim::Vec3;
using coney::human::Human;
using coney::human::HumanInput;
using coney::raycast::CollisionMesh;
using coney::world_objects::Doors;
using coney::world_objects::DoorSpawn;
using coney::world_objects::ObjectTypeInfo;
using coney::world_objects::ObjectWorld;
namespace door_command = coney::world_objects::door_command;

namespace {

// The doorway: the plane y = 40, its two triangles (indices 2 and 3, after the floor's two) facing -y, so a walker
// coming from y < 40 meets its front and one coming from y > 40 its back, as at the hangout's door 10.
constexpr float kDoorY = 40.0F;
constexpr std::array<std::uint32_t, 2> kDoorTriangles{2, 3};
// The stick's deflection the walkers use: a run, short of full, so the speed is the run's and not a sprint's.
constexpr float kStick = 0.6F;
// Updates a walk lasts: 4 s at 30 Hz, enough to cover the 3 m to the doorway and well past it.
constexpr int kWalkUpdates = 120;

// The floor and the doorway, one-sided as on the disc.
std::unique_ptr<CollisionMesh> doorwayMesh() {
    return coney::test::makeMesh(coney::test::join(coney::test::floorAt(0.0F, 0.0F, 80.0F, 0.0F, 80.0F),
                                                   coney::test::wallFacingMinusY(kDoorY, 0.0F, 80.0F, 0.0F, 2.2F)));
}

// The synthetic character and its anim set, held together for a test.
struct TestCharacter {
    coney::characters::CharacterData data = coney::test::locomotionData();
    coney::characters::AnimSet anims{data, nullptr};
};

// The pad for one update: the left stick at (0, `y`), the camera looking along +y.
HumanInput stick(float y) {
    return HumanInput{.stickX = 0.0F,
                      .stickY = y,
                      .cameraForward = Vec3{0.0F, 1.0F, 0.0F},
                      .sprintHeld = false,
                      .actionPressed = false,
                      .command = coney::combat::command::kNone,
                      .buttons = 0,
                      .targets = {}};
}

// Where a human ends who starts 3 m from the doorway on side `fromFront` (y < 40) or behind it and walks toward it.
float walkThrough(const TestCharacter& character, const CollisionMesh& mesh, bool fromFront) {
    Human human(character.anims, coney::human::AnimSlots::player(), coney::test::identityBind());
    human.spawn(&mesh, Vec3{40.0F, fromFront ? kDoorY - 3.0F : kDoorY + 3.0F, 0.0F}, 0.0F);
    const HumanInput input = stick(fromFront ? kStick : -kStick);
    for (int i = 0; i < kWalkUpdates; ++i) {
        human.step(input, &mesh);
    }
    return human.position().y;
}

// A swinging door type 2 m wide, 100 hitpoints, material 9.
ObjectTypeInfo swingingDoor() {
    return ObjectTypeInfo{.className = "dyn_door_swinging",
                          .hitpoints = 100,
                          .size = {2.0F, 0.1F, 2.2F},
                          .material = 9,
                          .leafWidth = 1.0F};
}

// The doorway's door (or barrier) of `type`, number 4, at its middle.
DoorSpawn doorwaySpawn(std::string type) {
    return DoorSpawn{.type = std::move(type),
                     .position = {40.0F, kDoorY, 0.0F},
                     .rotation = {},
                     .triangles = kDoorTriangles,
                     .number = 4};
}

// Spawns `info`'s door in `world` with handle 100 (its leaves 101, 102), as the level script's SpawnDoor does.
double spawnDoor(Doors& doors, ObjectWorld& world, const ObjectTypeInfo& info, std::string type) {
    double next = 100.0;
    return doors.spawn(next, doorwaySpawn(std::move(type)), &info, [&next] { return ++next; }, world).handle;
}

} // namespace

TEST_CASE("a one-sided doorway stops only a walker coming at its front", "[world_objects][doors][door_blocking]") {
    // The disc's doorway before any door marks it: the one-way wall of checklist item 30.
    const TestCharacter character;
    const auto mesh = doorwayMesh();
    CHECK(walkThrough(character, *mesh, true) < kDoorY);
    CHECK(walkThrough(character, *mesh, false) < kDoorY - 1.0F);
}

TEST_CASE("a door spawned before the level's mesh is bound blocks from both sides once it is",
          "[world_objects][doors][door_blocking]") {
    const TestCharacter character;
    const auto mesh = doorwayMesh();
    // The level script runs first: SpawnDoor with no mesh yet, as GameplayMode does.
    ObjectWorld world;
    Doors doors;
    const ObjectTypeInfo info = swingingDoor();
    const double door = spawnDoor(doors, world, info, "dyn_door_stlslot");
    world.bindCollision(mesh.get());

    for (const std::uint32_t index : kDoorTriangles) {
        const auto flags = mesh->triangles()[index].flags;
        CHECK((flags & coney::raycast::kTriangleTwoSided) != 0);
        CHECK((flags & 0x400) != 0);
        CHECK(mesh->triangles()[index].material == 9);
    }
    // Closed: stopped from inside and from outside, short of the plane by about the sphere's radius.
    const float fromFront = walkThrough(character, *mesh, true);
    const float fromBack = walkThrough(character, *mesh, false);
    CHECK(fromFront < kDoorY - 0.3F);
    CHECK(fromFront > kDoorY - 0.6F);
    CHECK(fromBack > kDoorY + 0.3F);
    CHECK(fromBack < kDoorY + 0.6F);

    // A mission opens it (OpenDoor, the hangout's exit handler): its collision goes at the end of the swing and the
    // player walks through either way.
    doors.command(door, door_command::kOpen, world);
    for (int i = 0; i <= coney::world_objects::kDoorInterval; ++i) {
        doors.tick(world);
    }
    REQUIRE(doors.isOpen(door));
    CHECK(walkThrough(character, *mesh, true) > kDoorY + 1.0F);
    CHECK(walkThrough(character, *mesh, false) < kDoorY - 1.0F);

    // Closed again (CloseDoor): it blocks both ways again.
    doors.command(door, door_command::kClose, world);
    CHECK(walkThrough(character, *mesh, true) < kDoorY);
    CHECK(walkThrough(character, *mesh, false) > kDoorY);
}

TEST_CASE("a barrier spawned before the mesh is bound blocks from both sides until it breaks",
          "[world_objects][doors][door_blocking]") {
    const TestCharacter character;
    const auto mesh = doorwayMesh();
    ObjectWorld world;
    Doors doors;
    const ObjectTypeInfo info{.className = "dyn_door_fence", .hitpoints = 10, .size = {2.0F, 0.1F, 2.2F}};
    const double fence = spawnDoor(doors, world, info, "dyn_door_fence");
    world.bindCollision(mesh.get());
    CHECK(walkThrough(character, *mesh, true) < kDoorY);
    CHECK(walkThrough(character, *mesh, false) > kDoorY);

    // Only breaking it opens the way (level3's rooftop chase, whose roof a row of these closes): a charge's 16 breaks
    // its 10, and its triangles go at its next update, when it is removed (docs/research/objects.md#barriers).
    REQUIRE(doors.hit(fence,
                      coney::world_objects::ObjectHit{.attacker = 50.0,
                                                      .kind = coney::world_objects::HitKind::Charge,
                                                      .point = {40.0F, kDoorY, 1.0F},
                                                      .direction = {0.0F, 1.0F, 0.0F},
                                                      .attackerAt = {40.0F, kDoorY - 1.0F, 0.0F}},
                      world));
    for (int i = 0; i <= coney::world_objects::kBarrierInterval; ++i) {
        doors.tick(world);
    }
    CHECK(walkThrough(character, *mesh, true) > kDoorY + 1.0F);
    CHECK(walkThrough(character, *mesh, false) < kDoorY - 1.0F);
}

TEST_CASE("triangle writes made before the bind land on it, later ones at once, and all of them on a new mesh",
          "[world_objects][doors][door_blocking]") {
    ObjectWorld world;
    // Marked, switched off, then marked again with another bit and a material: all of it lands on the bind.
    coney::world_objects::markTriangles(world, kDoorTriangles, coney::raycast::kTriangleTwoSided);
    coney::world_objects::setTrianglesEnabled(world, kDoorTriangles, false);
    coney::world_objects::markTriangles(world, std::array<std::uint32_t, 1>{3}, 0x200, 12);
    const auto first = doorwayMesh();
    world.bindCollision(first.get());
    const auto check = [](const CollisionMesh& mesh) {
        const auto& tris = mesh.triangles();
        CHECK(tris[2].flags == coney::raycast::kTriangleTwoSided);
        CHECK(tris[3].flags == (coney::raycast::kTriangleTwoSided | 0x200));
        CHECK(tris[3].material == 12);
        CHECK((tris[0].flags & coney::raycast::kTriangleEnabled) != 0); // the floor is untouched
    };
    check(*first);

    // Bound: a write lands at once, and is kept too.
    coney::world_objects::setTrianglesEnabled(world, std::array<std::uint32_t, 1>{2}, true);
    CHECK((first->triangles()[2].flags & coney::raycast::kTriangleEnabled) != 0);
    coney::world_objects::setTrianglesEnabled(world, std::array<std::uint32_t, 1>{2}, false);

    // The play mode ends and a new one binds a freshly loaded mesh: the objects' state is put back on it.
    world.bindCollision(nullptr);
    const auto second = doorwayMesh();
    world.bindCollision(second.get());
    check(*second);

    // Indices past the mesh are skipped.
    coney::world_objects::markTriangles(world, std::array<std::uint32_t, 1>{999}, 0x2);
    world.bindCollision(second.get());
    CHECK(second->triangles().size() == 4);
}

TEST_CASE("ChangeCollision's box switches are kept and replayed in order with the single triangles'",
          "[world_objects][door_blocking]") {
    ObjectWorld world;
    // Around the doorway's two triangles only: the floor reaches beyond the box.
    constexpr std::array<float, 3> kLow{0.0F, kDoorY - 1.0F, -0.5F};
    constexpr std::array<float, 3> kHigh{80.0F, kDoorY + 1.0F, 3.0F};
    // Triangle 2 off, then the box on: on. The box off, then triangle 3 on: 2 off, 3 on.
    coney::world_objects::setTrianglesEnabled(world, std::array<std::uint32_t, 1>{2}, false);
    coney::world_objects::setTrianglesEnabledInBox(world, kLow, kHigh, true);
    coney::world_objects::setTrianglesEnabledInBox(world, kLow, kHigh, false);
    coney::world_objects::setTrianglesEnabled(world, std::array<std::uint32_t, 1>{3}, true);
    const auto mesh = doorwayMesh();
    world.bindCollision(mesh.get());
    const auto enabled = [&mesh](std::size_t index) {
        return (mesh->triangles()[index].flags & coney::raycast::kTriangleEnabled) != 0;
    };
    CHECK_FALSE(enabled(2));
    CHECK(enabled(3));
    CHECK(enabled(0)); // the floor is outside the box
    CHECK(enabled(1));

    // Bound: a box switch lands at once.
    coney::world_objects::setTrianglesEnabledInBox(world, kLow, kHigh, true);
    CHECK(enabled(2));
    CHECK(world.boxWrites.size() == 3);
}

TEST_CASE("a glass pane spawned before the bind gives its triangles two sides and GLASS once bound",
          "[world_objects][glass][door_blocking]") {
    ObjectWorld world;
    coney::world_objects::GlassPanes panes;
    panes.setType(1, coney::world_objects::GlassType{});
    static_cast<void>(panes.spawn(100.0,
                                  coney::world_objects::GlassSpawn{.type = 1,
                                                                   .corner = {39.0F, kDoorY, 0.0F},
                                                                   .cornerU = {41.0F, kDoorY, 0.0F},
                                                                   .cornerV = {39.0F, kDoorY, 2.0F},
                                                                   .triangles = kDoorTriangles},
                                  world));
    const auto mesh = doorwayMesh();
    world.bindCollision(mesh.get());
    for (const std::uint32_t index : kDoorTriangles) {
        CHECK((mesh->triangles()[index].flags & coney::raycast::kTriangleTwoSided) != 0);
        CHECK(mesh->triangles()[index].material == coney::world_objects::material::kGlass);
    }
}

TEST_CASE("the level's unload forgets its objects' triangle writes", "[world_objects][doors][door_blocking]") {
    coney::world_objects::LevelObjects objects;
    const ObjectTypeInfo info = swingingDoor();
    spawnDoor(objects.doors, objects.world, info, "dyn_door_swinging");
    coney::world_objects::setTrianglesEnabledInBox(objects.world, {0.0F, 0.0F, 0.0F}, {1.0F, 1.0F, 1.0F}, false);
    REQUIRE_FALSE(objects.world.triangleWrites.empty());
    objects.clear();
    CHECK(objects.world.triangleWrites.empty());
    CHECK(objects.world.boxWrites.empty());
    // A mesh bound afterwards keeps the disc's one-sided doorway.
    const auto mesh = doorwayMesh();
    objects.world.bindCollision(mesh.get());
    CHECK((mesh->triangles()[2].flags & coney::raycast::kTriangleTwoSided) == 0);
}
