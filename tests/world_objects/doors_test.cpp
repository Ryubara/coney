// SPDX-License-Identifier: GPL-3.0-or-later
// The doors and barriers (docs/research/objects.md#doors, #door-states, #door-break, #barriers, #lock-pick): the type
// set-up, the spawn's leaves and triangles, the open and close state machine and its timing, the swing away from a
// human, pickable doors, the breakable types' hits and the barriers.
#include "world_objects/doors.h"

#include <cmath>
#include <cstdint>
#include <numbers>
#include <string>
#include <utility>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "animation/anim_math.h"
#include "core/name_hash.h"
#include "raycast/collision_mesh.h"
#include "support/object_fixtures.h"
#include "world/path_map.h"
#include "world_objects/level_objects.h"
#include "world_objects/object_services.h"

using coney::world_objects::Door;
using coney::world_objects::DoorBreaking;
using coney::world_objects::DoorClass;
using coney::world_objects::Doors;
using coney::world_objects::DoorSpawn;
using coney::world_objects::HitKind;
using coney::world_objects::ObjectHit;
using coney::world_objects::ObjectTypeInfo;
namespace door_state = coney::world_objects::door_state;
namespace door_command = coney::world_objects::door_command;

namespace {

// A swinging door type's CfgObj: 2 m wide, leaves 1 m wide, `hitpoints`, material 9, `objectType`.
ObjectTypeInfo swinging(int hitpoints, int objectType = 0) {
    return ObjectTypeInfo{.className = "dyn_door_swinging",
                          .hitpoints = hitpoints,
                          .size = {2.0F, 0.1F, 2.2F},
                          .material = 9,
                          .leafWidth = 1.0F,
                          .objectType = objectType};
}

// A door of `type` at (4, 2, 0) facing along x, on triangles 0 and 1, number 7.
DoorSpawn at(std::string type) {
    return DoorSpawn{.type = std::move(type),
                     .position = {4.0F, 2.0F, 0.0F},
                     .rotation = {},
                     .triangles = {0, 1},
                     .number = coney::test::kTestDoorNumber};
}

// A plain hit of `kind` by human 50 standing at `from`.
ObjectHit hitBy(HitKind kind, coney::anim::Vec3 from = {4.0F, 0.0F, 0.0F}) {
    return ObjectHit{.attacker = 50.0, .kind = kind, .point = {4.0F, 2.0F, 1.0F}, .direction = {}, .attackerAt = from};
}

// Ticks the doors `n` times.
void tick(Doors& doors, coney::test::ObjectWorldFixture& fixture, int n) {
    for (int i = 0; i < n; ++i) {
        doors.tick(fixture.world);
    }
}

} // namespace

TEST_CASE("the swinging door types' set-up", "[world_objects][doors]") {
    const auto gate = coney::world_objects::swingingDoorSetup("dyn_door_big_gate");
    CHECK(gate.leaves == 2);
    CHECK(gate.openSound == 0x43e4b743U);
    CHECK(gate.leafModel == "dyn_dr_big_gate");
    const auto chain = coney::world_objects::swingingDoorSetup("dyn_door_chainlnk_pick");
    CHECK(chain.leaves == 1);
    CHECK(chain.closeSound == 0x53928f1eU);
    CHECK(chain.pickable);
    const auto ornate = coney::world_objects::swingingDoorSetup("dyn_door_ornate_single");
    CHECK(ornate.leafModel == "dyn_dr_ornate");
    CHECK(ornate.openSound == coney::world_objects::kDefaultOpenSound);
    const auto cabin = coney::world_objects::swingingDoorSetup("dyn_door_cabin_b");
    CHECK(cabin.cabin);
    CHECK(cabin.breaking == DoorBreaking::Cabin);
    CHECK(cabin.wreck[0] == "dyn_cabin_bb");
    CHECK(coney::world_objects::swingingDoorSetup("dyn_door_liz").marksLinks);
    CHECK(coney::world_objects::swingingDoorSetup("dyn_door_woodp").leaves == 0);
    CHECK(coney::world_objects::doorClassOf("dyn_door_parapet") == DoorClass::Barrier);
    CHECK(coney::world_objects::doorClassOf("dyn_door_sliding") == DoorClass::Sliding);
}

TEST_CASE("a spawned double door: leaves, triangles, hitpoints", "[world_objects][doors]") {
    coney::test::ObjectWorldFixture fixture;
    Doors doors;
    const ObjectTypeInfo info = swinging(100);
    const Door& door = doors.spawn(
        fixture.handle(), at("dyn_door_wood"), &info, [&fixture] { return fixture.handle(); }, fixture.world);
    REQUIRE(door.leaves.size() == 2);
    CHECK(door.leaves[0].position.x == Catch::Approx(4.0F));
    // The second leaf 2w along -x, w half the box's width.
    CHECK(door.leaves[1].position.x == Catch::Approx(2.0F));
    CHECK(door.leaves[1].base.z == Catch::Approx(1.0F)); // turned 180° about z
    CHECK(door.hitpoints == 100);
    CHECK(door.state == door_state::kClosed);
    const auto triangle = fixture.mesh->triangles()[0];
    CHECK((triangle.flags & coney::raycast::kTriangleTwoSided) != 0);
    CHECK((triangle.flags & 0x400) != 0);
    CHECK((triangle.flags & 0x40) != 0);
    CHECK(triangle.material == 9);
    CHECK(doors.findByTriangle(1) == doors.find(door.handle));
    CHECK(doors.findByLeaf(door.leaves[1].handle) == doors.find(door.handle));
}

TEST_CASE("OpenDoor swings, and collision goes 29 ticks later; CloseDoor brings it back", "[world_objects][doors]") {
    coney::test::ObjectWorldFixture fixture;
    Doors doors;
    const ObjectTypeInfo info = swinging(100);
    const double handle =
        doors
            .spawn(
                fixture.handle(), at("dyn_door_wood"), &info, [&fixture] { return fixture.handle(); }, fixture.world)
            .handle;
    doors.command(handle, door_command::kOpen, fixture.world);
    CHECK(doors.find(handle)->state == door_state::kOpening);
    CHECK(doors.find(handle)->angle == Catch::Approx(170.0F));
    CHECK(fixture.services.sounds == std::vector<std::uint32_t>{coney::world_objects::kDefaultOpenSound});
    tick(doors, fixture, 1);
    CHECK(doors.find(handle)->state == door_state::kSwung);
    // The leaves start their swing from the closed pose: the left turns +170°, the right -170° from its base, at a
    // constant rate over 28 ticks.
    CHECK(doors.find(handle)->leaves[0].rotation.z == Catch::Approx(0.0).margin(1e-4));
    tick(doors, fixture, 14);
    CHECK(doors.find(handle)->leaves[0].rotation.z == Catch::Approx(std::sin(42.5 * 3.14159265 / 180.0)).margin(1e-4));
    tick(doors, fixture, 13);
    CHECK(fixture.enabled(0));
    CHECK_FALSE(doors.isOpen(handle));
    tick(doors, fixture, 1);
    CHECK(doors.isOpen(handle));
    CHECK(doors.find(handle)->leaves[0].rotation.z == Catch::Approx(std::sin(85.0 * 3.14159265 / 180.0)).margin(1e-4));
    CHECK_FALSE(fixture.enabled(0));
    CHECK_FALSE(fixture.paths.edges()[0].avoid);
    CHECK((fixture.paths.polygons()[3].flags & coney::world::kPathPolygonExcluded) != 0);

    doors.command(handle, door_command::kClose, fixture.world);
    CHECK(doors.find(handle)->state == door_state::kClosing);
    CHECK(fixture.services.sounds.back() == coney::world_objects::kDefaultCloseSound);
    CHECK(fixture.enabled(0));
    CHECK(fixture.paths.edges()[0].avoid);
    CHECK((fixture.paths.polygons()[3].flags & coney::world::kPathPolygonExcluded) == 0);
    tick(doors, fixture, 2 * coney::world_objects::kDoorInterval);
    CHECK(doors.find(handle)->state == door_state::kClosed);
    CHECK(doors.find(handle)->leaves[0].rotation == doors.find(handle)->leaves[0].base);
}

TEST_CASE("DoorOpen swings away from the human, only from closed", "[world_objects][doors]") {
    coney::test::ObjectWorldFixture fixture;
    Doors doors;
    const ObjectTypeInfo info = swinging(100);
    const auto make = [&] {
        return doors
            .spawn(
                fixture.handle(), at("dyn_door_wood"), &info, [&fixture] { return fixture.handle(); }, fixture.world)
            .handle;
    };
    const double south = make();
    const double north = make();
    doors.openBy(south, {4.0F, -3.0F, 0.0F}, fixture.world);
    doors.openBy(north, {4.0F, 7.0F, 0.0F}, fixture.world);
    CHECK(doors.find(south)->angle == Catch::Approx(170.0F));
    CHECK(doors.find(north)->angle == Catch::Approx(-170.0F));
    // Not closed any more: a second DoorOpen does nothing.
    doors.openBy(south, {4.0F, 7.0F, 0.0F}, fixture.world);
    CHECK(doors.find(south)->angle == Catch::Approx(170.0F));
}

TEST_CASE("a pickable door refuses to open and keeps a DoorOpenDegree", "[world_objects][doors]") {
    coney::test::ObjectWorldFixture fixture;
    Doors doors;
    const ObjectTypeInfo info = swinging(100);
    const double handle =
        doors
            .spawn(
                fixture.handle(), at("dyn_door_storeb"), &info, [&fixture] { return fixture.handle(); }, fixture.world)
            .handle;
    CHECK(doors.find(handle)->pickable);
    CHECK(doors.find(handle)->glint);
    doors.command(handle, door_command::kOpen, fixture.world);
    doors.openBy(handle, {}, fixture.world);
    doors.openToDegree(handle, 90.0F, fixture.world);
    CHECK(doors.find(handle)->state == door_state::kClosed);
    CHECK(doors.find(handle)->keptAngle == Catch::Approx(90.0F));

    // Unlocked by a pick, it opens to the kept angle.
    doors.lockPickSucceeded(handle, {4.0F, -3.0F, 0.0F}, fixture.world);
    CHECK_FALSE(doors.find(handle)->pickable);
    CHECK(doors.find(handle)->state == door_state::kOpening);
    CHECK(doors.find(handle)->angle == Catch::Approx(90.0F));
}

TEST_CASE("SetDoorPickable(false) stops the pickers; the third abandoned pick counts", "[world_objects][doors]") {
    coney::test::ObjectWorldFixture fixture;
    Doors doors;
    const ObjectTypeInfo info = swinging(100);
    const double handle =
        doors
            .spawn(
                fixture.handle(), at("dyn_door_wood"), &info, [&fixture] { return fixture.handle(); }, fixture.world)
            .handle;
    doors.setPickable(handle, true, fixture.world);
    CHECK(doors.find(handle)->pickable);
    doors.setPickable(handle, false, fixture.world);
    CHECK_FALSE(doors.find(handle)->pickable);
    CHECK(fixture.services.stopped == std::vector<double>{handle});
    CHECK_FALSE(doors.lockPickAbandoned(handle));
    CHECK_FALSE(doors.lockPickAbandoned(handle));
    CHECK(doors.lockPickAbandoned(handle));
    // An open door takes no glint.
    doors.command(handle, door_command::kOpen, fixture.world);
    doors.setPickable(handle, true, fixture.world);
    CHECK_FALSE(doors.find(handle)->pickable);
}

TEST_CASE("the store door: one hit swings it open away from the attacker", "[world_objects][doors]") {
    coney::test::ObjectWorldFixture fixture;
    Doors doors;
    const ObjectTypeInfo info = swinging(1, coney::world_objects::object_type::kBreakAndEnterDoor);
    const double handle =
        doors
            .spawn(
                fixture.handle(), at("dyn_door_store"), &info, [&fixture] { return fixture.handle(); }, fixture.world)
            .handle;
    CHECK(doors.hit(handle, hitBy(HitKind::Plain, {4.0F, 7.0F, 0.0F}), fixture.world));
    CHECK(doors.find(handle)->state == door_state::kOpening);
    CHECK(doors.find(handle)->angle == Catch::Approx(-170.0F));
    CHECK_FALSE(fixture.enabled(0));
    CHECK(fixture.services.sounds.front() == coney::world_objects::kDoorHitSound);
}

TEST_CASE("a cabin door: two plain hits break a leaf, three the door", "[world_objects][doors]") {
    coney::test::ObjectWorldFixture fixture;
    Doors doors;
    const ObjectTypeInfo info = swinging(12);
    const double handle =
        doors
            .spawn(
                fixture.handle(), at("dyn_door_cabin_a"), &info, [&fixture] { return fixture.handle(); }, fixture.world)
            .handle;
    CHECK(doors.find(handle)->state == door_state::kCabinRest);
    CHECK((fixture.mesh->triangles()[0].flags & coney::raycast::kTriangleTestableDisabled) != 0);
    doors.hit(handle, hitBy(HitKind::Plain), fixture.world);
    CHECK_FALSE(doors.find(handle)->leavesBroken);
    doors.hit(handle, hitBy(HitKind::Plain), fixture.world);
    CHECK(doors.find(handle)->leavesBroken);
    CHECK(fixture.services.nextModels.size() == 1);
    CHECK(fixture.services.bursts == 1);
    doors.hit(handle, hitBy(HitKind::Plain), fixture.world);
    CHECK(doors.find(handle)->state == door_state::kBroken);
    CHECK(fixture.services.spawned == std::vector<std::string>{"dyn_cabin_aa", "dyn_cabin_aa"});
    CHECK_FALSE(fixture.enabled(0));
    // The next update ends the door; then it takes nothing.
    tick(doors, fixture, coney::world_objects::kCabinInterval);
    CHECK(doors.find(handle)->ended);
    CHECK_FALSE(doors.hit(handle, hitBy(HitKind::Plain), fixture.world));
}

TEST_CASE("dyn_door_liz: model stages, then wreck pieces and the links opened", "[world_objects][doors]") {
    coney::test::ObjectWorldFixture fixture;
    Doors doors;
    const ObjectTypeInfo info = swinging(100);
    const double handle =
        doors.spawn(
                 fixture.handle(), at("dyn_door_liz"), &info, [&fixture] { return fixture.handle(); }, fixture.world)
            .handle;
    CHECK(fixture.paths.edges()[0].flags == 0x40);              // marks its links
    doors.hit(handle, hitBy(HitKind::Airborne), fixture.world); // 22: 78 left, past 80 %
    CHECK(fixture.services.nextModels.size() == 1);
    CHECK(fixture.services.splinterCount == 20);
    CHECK(fixture.services.dusts == 2);
    doors.hit(handle, hitBy(HitKind::Airborne), fixture.world); // 56: past 60 %
    doors.hit(handle, hitBy(HitKind::Airborne), fixture.world); // 34: past 40 %
    doors.hit(handle, hitBy(HitKind::Airborne), fixture.world); // 12: past 20 %
    CHECK(fixture.services.nextModels.size() == 4);
    doors.hit(handle, hitBy(HitKind::Airborne), fixture.world); // broken
    const Door& door = *doors.find(handle);
    CHECK(door.state == door_state::kBroken);
    CHECK((door.tint & 0xffU) == 0);
    CHECK(fixture.services.spawned == std::vector<std::string>{"dyn_dre_liz_e", "dyn_dre_liz_f"});
    CHECK_FALSE(fixture.paths.edges()[0].avoid);
}

TEST_CASE("dyn_door_stall breaks on its first hit", "[world_objects][doors]") {
    coney::test::ObjectWorldFixture fixture;
    Doors doors;
    const ObjectTypeInfo info = swinging(10);
    const double handle =
        doors
            .spawn(
                fixture.handle(), at("dyn_door_stall"), &info, [&fixture] { return fixture.handle(); }, fixture.world)
            .handle;
    doors.hit(handle, hitBy(HitKind::Plain), fixture.world);
    CHECK(doors.find(handle)->state == door_state::kBroken);
}

TEST_CASE("other swinging doors ignore hits; a destroyed one hits itself", "[world_objects][doors]") {
    coney::test::ObjectWorldFixture fixture;
    Doors doors;
    const ObjectTypeInfo info = swinging(12);
    const double plain =
        doors
            .spawn(
                fixture.handle(), at("dyn_door_wood"), &info, [&fixture] { return fixture.handle(); }, fixture.world)
            .handle;
    CHECK_FALSE(doors.hit(plain, hitBy(HitKind::Plain), fixture.world));
    CHECK(doors.hitpoints(plain) == 12);

    const double cabin =
        doors
            .spawn(
                fixture.handle(), at("dyn_door_cabin_a"), &info, [&fixture] { return fixture.handle(); }, fixture.world)
            .handle;
    CHECK(doors.destroyInRadius({4.0F, 2.0F, 0.0F}, 1.0F) == 2);
    tick(doors, fixture, 2);
    CHECK(doors.hitpoints(cabin) == 8);
}

TEST_CASE("a barrier: links charged through, boards, hidden, then removed with its body at its next update",
          "[world_objects][doors]") {
    coney::test::ObjectWorldFixture fixture;
    Doors doors;
    const ObjectTypeInfo info{
        .className = "dyn_door_fence", .hitpoints = 10, .size = {2.5F, 0.2F, 2.6F}, .material = 3};
    const double handle = doors.spawn(fixture.handle(), at("dyn_door_fence"), &info, {}, fixture.world).handle;
    CHECK(doors.find(handle)->doorClass == DoorClass::Barrier);
    CHECK(fixture.paths.edges()[0].flags == 0x40);
    CHECK((fixture.mesh->triangles()[0].flags & 0x40) != 0);
    // Commands do nothing to a barrier.
    doors.command(handle, door_command::kOpen, fixture.world);
    CHECK(doors.find(handle)->state == door_state::kClosed);

    // A hit that leaves hitpoints changes no model.
    doors.hit(handle, hitBy(HitKind::Plain), fixture.world);
    CHECK(fixture.services.models.empty());
    CHECK_FALSE(doors.find(handle)->hidden);
    // A charge (16) breaks the 6 left.
    doors.hit(handle, hitBy(HitKind::Charge), fixture.world);
    CHECK(doors.find(handle)->hidden);
    // Its triangles stand for the body the break leaves, until the removal.
    CHECK(fixture.enabled(0));
    CHECK_FALSE(fixture.paths.edges()[0].avoid);
    CHECK(fixture.services.spawned.size() == 3);
    // The three boards go on their own first update, the next tick (the hit's own update in the original), and never
    // stand for the barrier's removal 60 ticks on (docs/research/objects.md#barriers, issue #233).
    CHECK(fixture.services.destroyed.empty());
    tick(doors, fixture, 1);
    CHECK(fixture.services.destroyed == std::vector<double>{1001.0, 1002.0, 1003.0});
    tick(doors, fixture, 1);
    CHECK(fixture.services.destroyed.size() == 3);
    CHECK_FALSE(doors.hit(handle, hitBy(HitKind::Plain), fixture.world));
    // Broken, it is removed at its next update (every 60 ticks), once.
    CHECK(doors.find(handle)->broken);
    CHECK(doors.takeRemoved().empty());
    tick(doors, fixture, coney::world_objects::kBarrierInterval);
    CHECK(doors.takeRemoved() == std::vector<double>{handle});
    CHECK(doors.find(handle)->removed);
    CHECK_FALSE(fixture.enabled(0));
    tick(doors, fixture, coney::world_objects::kBarrierInterval);
    CHECK(doors.takeRemoved().empty());
}

TEST_CASE("walls throw no boards; message 10 stops a barrier being hit", "[world_objects][doors]") {
    coney::test::ObjectWorldFixture fixture;
    Doors doors;
    const ObjectTypeInfo info{.className = "dyn_door_fence", .hitpoints = 3};
    const double wall = doors.spawn(fixture.handle(), at("dyn_door_wall_a"), &info, {}, fixture.world).handle;
    doors.setHittable(wall, false);
    CHECK_FALSE(doors.hit(wall, hitBy(HitKind::Plain), fixture.world));
    doors.setHittable(wall, true);
    CHECK(doors.hit(wall, hitBy(HitKind::Plain), fixture.world));
    CHECK(doors.find(wall)->hidden);
    CHECK(fixture.services.spawned.empty());
    // dyn_door_wall_a takes its damaged model and loses its body when broken.
    REQUIRE(fixture.services.models.size() == 1);
    CHECK(fixture.services.models[0].second == coney::world_objects::kBarrierDamagedModel);

    const ObjectTypeInfo chain{.className = "dyn_door_chain_s", .hitpoints = 10};
    coney::test::ObjectWorldFixture other;
    doors.spawn(other.handle(), at("dyn_door_chain_s"), &chain, {}, other.world);
    CHECK(other.paths.edges()[0].flags == 0x10); // dyn_door_chain_s leaves its links alone
}

TEST_CASE("the doors draw their leaves at their poses and a barrier as its model until it breaks",
          "[world_objects][doors]") {
    coney::test::ObjectWorldFixture fixture;
    Doors doors;
    const ObjectTypeInfo store = swinging(1, coney::world_objects::object_type::kBreakAndEnterDoor);
    const double door =
        doors
            .spawn(
                fixture.handle(), at("dyn_door_store"), &store, [&fixture] { return fixture.handle(); }, fixture.world)
            .handle;
    const ObjectTypeInfo fence{.className = "dyn_door_fence", .hitpoints = 10};
    const double barrier = doors.spawn(fixture.handle(), at("dyn_door_fence"), &fence, {}, fixture.world).handle;

    std::vector<coney::world_objects::DoorDraw> draws = coney::world_objects::doorDraws(doors);
    // The store door's two leaves (no model for its frame), then the fence.
    REQUIRE(draws.size() == 3);
    const Door& made = *doors.find(door);
    CHECK(draws[0].handle == made.leaves[0].handle);
    CHECK(draws[0].modelHash == coney::crc32(made.leaves[0].model));
    CHECK(draws[1].position.x == Catch::Approx(made.leaves[1].position.x));
    CHECK(draws[2].handle == barrier);
    CHECK(draws[2].modelHash == coney::crc32("dyn_door_fence"));

    // Hit: the store door's leaves swing to their new rotation; the fence keeps its model, then hides.
    doors.hit(door, hitBy(HitKind::Plain, {4.0F, 7.0F, 0.0F}), fixture.world);
    doors.hit(barrier, hitBy(HitKind::Plain), fixture.world);
    tick(doors, fixture, 2);
    draws = coney::world_objects::doorDraws(doors);
    REQUIRE(draws.size() == 3);
    CHECK(draws[0].rotation.w == Catch::Approx(doors.find(door)->leaves[0].rotation.w));
    CHECK(draws[0].rotation.w != Catch::Approx(1.0F));
    CHECK(draws[2].modelHash == coney::crc32("dyn_door_fence"));
    doors.hit(barrier, hitBy(HitKind::Charge), fixture.world);
    CHECK(coney::world_objects::doorDraws(doors).size() == 2);
}

TEST_CASE("a door whose model is not listed falls back to dyn_dr_ and its name, less a leading dbl",
          "[world_objects][doors]") {
    coney::test::ObjectWorldFixture fixture;
    Doors doors;
    const ObjectTypeInfo fence{.className = "dyn_door_fence", .hitpoints = 10};
    doors.spawn(fixture.handle(), at("dyn_door_fence"), &fence, {}, fixture.world);
    const ObjectTypeInfo wood = swinging(100);
    doors.spawn(
        fixture.handle(), at("dyn_door_dblwoodfnce_xl"), &wood, [&fixture] { return fixture.handle(); }, fixture.world);
    const std::vector<coney::world_objects::DoorDraw> draws = coney::world_objects::doorDraws(doors);
    REQUIRE(draws.size() == 3);
    CHECK(draws[0].fallbackHash == coney::crc32("dyn_dr_fence"));
    CHECK(draws[1].fallbackHash == coney::crc32("dyn_dr_woodfnce_xl"));
}

TEST_CASE("CamGhostDoor lets the camera's rays through a door's triangles, and only a door's",
          "[world_objects][doors]") {
    coney::test::ObjectWorldFixture fixture;
    coney::world_objects::LevelObjects objects;
    objects.world = fixture.world;
    const ObjectTypeInfo door = swinging(100, 15); // TYPE_DOOR
    const double handle =
        objects.doors
            .spawn(
                fixture.handle(), at("dyn_door_wood"), &door, [&fixture] { return fixture.handle(); }, objects.world)
            .handle;
    REQUIRE((fixture.mesh->triangles()[0].flags & coney::world_objects::kCameraGhostBit) == 0);
    CHECK(objects.ghostForCamera(handle));
    CHECK((fixture.mesh->triangles()[0].flags & coney::world_objects::kCameraGhostBit) != 0);
    CHECK((fixture.mesh->triangles()[1].flags & coney::world_objects::kCameraGhostBit) != 0);
    // Not a door kind, or not a door at all: nothing.
    const ObjectTypeInfo plain = swinging(100, 0);
    coney::world_objects::DoorSpawn other = at("dyn_door_wood");
    other.triangles = {2, 3};
    const double plainHandle =
        objects.doors.spawn(
                         fixture.handle(), other, &plain, [&fixture] { return fixture.handle(); }, objects.world)
            .handle;
    CHECK_FALSE(objects.ghostForCamera(plainHandle));
    CHECK((fixture.mesh->triangles()[2].flags & coney::world_objects::kCameraGhostBit) == 0);
    CHECK_FALSE(objects.ghostForCamera(12345.0));
    CHECK(coney::world_objects::isDoorKind(25));
    CHECK(coney::world_objects::isDoorKind(30));
    CHECK(coney::world_objects::isDoorKind(33));
    CHECK_FALSE(coney::world_objects::isDoorKind(0));
}

TEST_CASE("a roll-up door: closed at its set-up, OpenDoor slides its leaf up and opens the way, CloseDoor shuts it",
          "[world_objects][doors]") {
    coney::test::ObjectWorldFixture fixture;
    Doors doors;
    // dyn_door_sliding: the step twice property 6 (0.1 m), no property 7, so property 8's 3 m up the leaf's z axis.
    const ObjectTypeInfo info{.className = "dyn_door_sliding",
                              .hitpoints = 0,
                              .size = {3.0F, 0.2F, 3.0F},
                              .material = 9,
                              .leafWidth = 1.5F,
                              .property6 = 0.05F,
                              .property7 = 0.0F,
                              .property8 = 3.0F,
                              .objectType = 0};
    const double handle =
        doors
            .spawn(
                fixture.handle(), at("dyn_door_rollup"), &info, [&fixture] { return fixture.handle(); }, fixture.world)
            .handle;
    const Door& door = *doors.find(handle);
    REQUIRE(door.doorClass == DoorClass::Sliding);
    REQUIRE(door.slidingLeaves.size() == 1);
    CHECK(door.slidingLeaves[0].model == "dyn_dr_rollup");
    CHECK(door.slideAxis == 0);
    CHECK(door.slideLength == Catch::Approx(3.0F));
    CHECK(door.slideStep == Catch::Approx(0.1F));
    CHECK(door.slidingLeaves[0].axis.z == Catch::Approx(1.0F));
    // Set up closed: triangles on, its number's links avoided, the doorway's hole in place; the close sound.
    CHECK(fixture.enabled(0));
    CHECK(fixture.paths.edges()[0].avoid);
    CHECK((fixture.paths.polygons()[3].flags & coney::world::kPathPolygonExcluded) == 0);
    CHECK_FALSE(doors.isOpen(handle));
    CHECK(fixture.services.sounds == std::vector<std::uint32_t>{coney::world_objects::kRollupDoorSound});

    // OpenDoor: the way opens at once, and the leaf climbs 0.1 m every second tick until past 3 m, then settles there.
    doors.command(handle, door_command::kOpen, fixture.world);
    CHECK(doors.isOpen(handle));
    CHECK_FALSE(fixture.enabled(0));
    CHECK_FALSE(fixture.paths.edges()[0].avoid);
    CHECK((fixture.paths.polygons()[3].flags & coney::world::kPathPolygonExcluded) != 0);
    tick(doors, fixture, 2);
    CHECK(door.slidingLeaves[0].offset == Catch::Approx(0.1F));
    CHECK(door.slidingLeaves[0].position().z == Catch::Approx(0.1F));
    tick(doors, fixture, 2 * 40);
    CHECK(door.slidingLeaves[0].offset == Catch::Approx(3.0F));
    CHECK(door.slidingLeaves[0].motion == coney::world_objects::slide_motion::kRest);

    // CloseDoor: the way shuts at once and the leaf comes down.
    doors.command(handle, door_command::kClose, fixture.world);
    CHECK_FALSE(doors.isOpen(handle));
    CHECK(fixture.enabled(0));
    CHECK(fixture.paths.edges()[0].avoid);
    tick(doors, fixture, 2 * 40);
    CHECK(door.slidingLeaves[0].offset == Catch::Approx(0.0F));
}

TEST_CASE("a lift's two-leaf door (dyn_door_ele_c) opens its leaves apart, the first up and the second down",
          "[world_objects][doors]") {
    // Expected: script-types.md#sliding-doors, confirmed (code): SlidingDoor_SpawnLeaves turns the second leaf 180
    // degrees about its y axis and places it 2w along -z; SubSlidingDoor_PlaceLeaf turns a vertical door's second leaf
    // about y, so it slides down.
    coney::test::ObjectWorldFixture fixture;
    Doors doors;
    const ObjectTypeInfo info{.className = "dyn_door_sliding",
                              .hitpoints = 0,
                              .size = {2.0F, 0.2F, 3.0F},
                              .material = 9,
                              .leafWidth = 0.0F,
                              .property6 = 0.007F,
                              .property7 = 0.0F,
                              .property8 = 1.6F,
                              .objectType = 0};
    const double handle =
        doors
            .spawn(
                fixture.handle(), at("dyn_door_ele_c"), &info, [&fixture] { return fixture.handle(); }, fixture.world)
            .handle;
    const Door& door = *doors.find(handle);
    REQUIRE(door.slidingLeaves.size() == 2);
    CHECK(door.slidingLeaves[0].axis.z == Catch::Approx(1.0F));
    CHECK(door.slidingLeaves[1].axis.z == Catch::Approx(-1.0F));
    doors.command(handle, door_command::kOpen, fixture.world);
    tick(doors, fixture, 2 * 130);
    const auto rise = [](const coney::world_objects::SlidingLeaf& leaf) { return leaf.position().z - leaf.closed.z; };
    CHECK(rise(door.slidingLeaves[0]) == Catch::Approx(1.6F));
    CHECK(rise(door.slidingLeaves[1]) == Catch::Approx(-1.6F));
}

TEST_CASE("a two-leaf sliding door's command puts its leaves back to the door's pose, the second turned about z; a "
          "lift's pair keeps its own",
          "[world_objects][doors]") {
    // Expected: armies.md#garage-door, confirmed (code): SlidingDoor_StateCommand (0x003f5608) sets, for every type
    // but dyn_door_ele_c, the first leaf's rotation to the door's and the second's to the door's turned 180 degrees
    // about z, on every command; the slide direction SubSlidingDoor_PlaceLeaf (0x003f4950) worked out from the
    // set-up's pose stays.
    coney::test::ObjectWorldFixture fixture;
    Doors doors;
    // An up-and-down door (no property 7): its second leaf is set up turned about y.
    const ObjectTypeInfo info{.className = "dyn_door_sliding",
                              .hitpoints = 0,
                              .size = {3.0F, 0.2F, 3.0F},
                              .material = 9,
                              .leafWidth = 1.5F,
                              .property6 = 0.05F,
                              .property7 = 0.0F,
                              .property8 = 3.0F,
                              .objectType = 0};
    // The door turned a quarter round, so the turn's frame shows.
    const float half = std::numbers::pi_v<float> / 4.0F;
    const auto spawnTurned = [&](std::string type) -> const Door& {
        DoorSpawn spawn = at(std::move(type));
        spawn.rotation = coney::anim::Quat{0.0F, 0.0F, std::sin(half), std::cos(half)};
        const double handle =
            doors.spawn(fixture.handle(), spawn, &info, [&fixture] { return fixture.handle(); }, fixture.world).handle;
        return *doors.find(handle);
    };
    const coney::anim::Vec3 up{0.0F, 0.0F, 1.0F};
    const coney::anim::Vec3 across{1.0F, 0.0F, 0.0F};
    const Door& portcullis = spawnTurned("dyn_door_portcullis");
    REQUIRE(portcullis.slidingLeaves.size() == 2);
    // Set up: the second leaf upside down (about y) and sliding down.
    const coney::world_objects::SlidingLeaf& second = portcullis.slidingLeaves[1];
    CHECK(coney::world_objects::rotate(second.rotation, up).z == Catch::Approx(-1.0F));
    CHECK(second.axis.z == Catch::Approx(-1.0F));
    doors.command(portcullis.handle, door_command::kOpen, fixture.world);
    // After the command: upright again, its x the door's -x (the door's x is +y here), still sliding down.
    CHECK(coney::world_objects::rotate(second.rotation, up).z == Catch::Approx(1.0F));
    CHECK(coney::world_objects::rotate(second.rotation, across).y == Catch::Approx(-1.0F));
    CHECK(second.axis.z == Catch::Approx(-1.0F));
    CHECK(portcullis.slidingLeaves[0].rotation == portcullis.rotation);

    // The lift's pair keeps the set-up's poses.
    const Door& lift = spawnTurned("dyn_door_ele_c");
    REQUIRE(lift.slidingLeaves.size() == 2);
    const coney::anim::Quat liftSecond = lift.slidingLeaves[1].rotation;
    doors.command(lift.handle, door_command::kOpen, fixture.world);
    CHECK(lift.slidingLeaves[1].rotation == liftSecond);
}
