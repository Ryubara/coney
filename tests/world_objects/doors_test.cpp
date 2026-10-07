// SPDX-License-Identifier: GPL-3.0-or-later
// The doors and barriers (docs/research/objects.md#doors, #door-states, #door-break, #barriers, #lock-pick): the type
// set-up, the spawn's leaves and triangles, the open and close state machine and its timing, the swing away from a
// human, pickable doors, the breakable types' hits and the barriers.
#include "world_objects/doors.h"

#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "core/name_hash.h"
#include "raycast/collision_mesh.h"
#include "support/object_fixtures.h"
#include "world/path_map.h"

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
    CHECK(coney::world_objects::doorClassOf("dyn_door_sliding") == DoorClass::Other);
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

TEST_CASE("a barrier: links charged through, damaged model, boards and hidden", "[world_objects][doors]") {
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

    doors.hit(handle, hitBy(HitKind::Plain), fixture.world);
    CHECK(fixture.services.models.size() == 1);
    CHECK(fixture.services.models[0].second == coney::world_objects::kBarrierDamagedModel);
    // A charge (16) breaks the 6 left.
    doors.hit(handle, hitBy(HitKind::Charge), fixture.world);
    CHECK(doors.find(handle)->hidden);
    CHECK_FALSE(fixture.enabled(0));
    CHECK_FALSE(fixture.paths.edges()[0].avoid);
    CHECK(fixture.services.spawned.size() == 3);
    CHECK_FALSE(doors.hit(handle, hitBy(HitKind::Plain), fixture.world));
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

    const ObjectTypeInfo chain{.className = "dyn_door_chain_s", .hitpoints = 10};
    coney::test::ObjectWorldFixture other;
    doors.spawn(other.handle(), at("dyn_door_chain_s"), &chain, {}, other.world);
    CHECK(other.paths.edges()[0].flags == 0x10); // dyn_door_chain_s leaves its links alone
}

TEST_CASE("the doors draw their leaves at their poses and a barrier as its model, damaged once hit",
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

    // Hit: the store door's leaves swing to their new rotation; the fence takes its damaged model, then hides.
    doors.hit(door, hitBy(HitKind::Plain, {4.0F, 7.0F, 0.0F}), fixture.world);
    doors.hit(barrier, hitBy(HitKind::Plain), fixture.world);
    tick(doors, fixture, 2);
    draws = coney::world_objects::doorDraws(doors);
    REQUIRE(draws.size() == 3);
    CHECK(draws[0].rotation.w == Catch::Approx(doors.find(door)->leaves[0].rotation.w));
    CHECK(draws[0].rotation.w != Catch::Approx(1.0F));
    CHECK(draws[2].modelHash == coney::world_objects::kBarrierDamagedModel);
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
