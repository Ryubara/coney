// SPDX-License-Identifier: GPL-3.0-or-later
// Loose objects in flight (world_objects/loose_objects.h): the settle's eased turn and its target, the bodies' reach,
// and a dropped object's fall, bounce, settle and stop over a synthetic flat floor at z = 0.
#include "world_objects/loose_objects.h"

#include <cmath>
#include <numbers>
#include <optional>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "animation/anim_math.h"
#include "world_objects/object_types.h"

using Catch::Approx;
using coney::anim::Quat;
using coney::anim::Vec3;
using coney::world_objects::LooseObject;
using coney::world_objects::LooseObjects;
using coney::world_objects::LooseShape;
using coney::world_objects::ObjectType;
using coney::world_objects::RayContact;
using coney::world_objects::settleAxis;
using coney::world_objects::settleTarget;
using coney::world_objects::SettleTurn;

namespace {

// A turn of `angle` radians about the unit axis `axis`.
Quat turn(Vec3 axis, float angle) {
    const float s = std::sin(angle / 2.0F);
    return Quat{axis.x * s, axis.y * s, axis.z * s, std::cos(angle / 2.0F)};
}

// `v` turned by `q`, through the rotation matrix.
Vec3 rotated(Quat q, Vec3 v) { return coney::anim::transformDirection(coney::anim::matrixFromQuat(q), v); }

// The level: a flat floor at z = 0, met by a ray going down from above it.
std::optional<RayContact> floorRay(Vec3 origin, Vec3 direction, float length) {
    if (direction.z >= 0.0F || origin.z < 0.0F) {
        return std::nullopt;
    }
    const float distance = origin.z / -direction.z;
    if (distance > length) {
        return std::nullopt;
    }
    return RayContact{.distance = distance, .normal = {0, 0, 1}, .body = false};
}

constexpr int kAxisZ = 4;
constexpr int kAxisXZ = 5;
constexpr int kAxisXZRound = 13;

// A synthetic brick type: an OBB 0.07 × 0.21 × 0.10 m, settling on x or z, restitution 0.1.
ObjectType brickType() {
    ObjectType type;
    type.name = "dyn_test_brick";
    type.bodyShape = coney::world_objects::kBodyBox;
    type.bodySize = {0.07F, 0.21F, 0.10F};
    type.axis = kAxisXZ;
    type.restitution = 0.1F;
    return type;
}

} // namespace

TEST_CASE("a settle's progress accelerates from rest and ends on its 11th tick", "[world_objects][physics]") {
    SettleTurn settle{.start = {}, .end = turn({1, 0, 0}, std::numbers::pi_v<float> / 2.0F)};
    for (int n = 1; n <= 10; ++n) {
        REQUIRE_FALSE(settle.tick());
        // From rest, t after n ticks is 0.01 n (n - 1).
        CHECK(settle.t == Approx(0.01F * static_cast<float>(n * (n - 1))).margin(1e-5));
    }
    CHECK(settle.tick());
    CHECK(settle.t == 1.0F);
    CHECK(std::fabs(coney::anim::dot(settle.rotation(), settle.end)) == Approx(1.0F));
}

TEST_CASE("a settle turns onto the nearest face its axis byte allows", "[world_objects][physics]") {
    const Vec3 up{0, 0, 1};
    SECTION("no allowed axis: no settle") {
        CHECK_FALSE(settleAxis({}, up, 0).has_value());
        CHECK(settleTarget(turn({1, 0, 0}, 0.3F), up, 0) == turn({1, 0, 0}, 0.3F));
    }
    SECTION("Z: the local z axis comes up") {
        const Vec3 z = rotated(settleTarget(turn({1, 0, 0}, 0.5F), up, kAxisZ), {0, 0, 1});
        CHECK(z.z == Approx(1.0F).margin(1e-5));
    }
    SECTION("an axis pointing away lines up its negative direction (the turn wrapped to 90 degrees)") {
        const Vec3 z = rotated(settleTarget(turn({1, 0, 0}, 2.8F), up, kAxisZ), {0, 0, 1});
        CHECK(z.z == Approx(-1.0F).margin(1e-5));
    }
    SECTION("the axis nearest by its positive direction wins, not the most nearly vertical") {
        // Upside down but for a 0.2 rad tilt about y: z points almost straight down and x lies nearly flat, a little
        // up. x has the smaller angle to the normal, so it is turned up through nearly 90 degrees.
        const Quat downish = turn({0, 1, 0}, std::numbers::pi_v<float> + 0.2F);
        CHECK(rotated(downish, {1, 0, 0}).z > 0.0F);
        CHECK(rotated(downish, {0, 0, 1}).z < -0.9F);
        CHECK(settleAxis(downish, up, kAxisXZ) == 0);
        CHECK(rotated(settleTarget(downish, up, kAxisXZ), {1, 0, 0}).z == Approx(1.0F).margin(1e-5));
    }
    SECTION("a _ROUND type settles like its plain axes") {
        const Quat tilted = turn(coney::anim::normalise(Vec3{1, 1, 0}), 1.1F);
        CHECK(settleTarget(tilted, up, kAxisXZRound) == settleTarget(tilted, up, kAxisXZ));
    }
}

TEST_CASE("a body's reach follows its shape and turn", "[world_objects][physics]") {
    const LooseShape box = LooseShape::of(brickType());
    CHECK(box.reach({}, {0, 0, 1}) == Approx(0.05F));
    CHECK(box.reach({}, {0, -1, 0}) == Approx(0.105F));
    const float s = std::numbers::sqrt2_v<float> / 2.0F;
    CHECK(box.reach(turn({1, 0, 0}, std::numbers::pi_v<float> / 4.0F), {0, 0, -1}) ==
          Approx((0.105F * s) + (0.05F * s)));
    ObjectType ball;
    ball.bodyShape = coney::world_objects::kBodySphere;
    ball.bodySize = {0.3F, 0.3F, 0.3F};
    CHECK(LooseShape::of(ball).reach(turn({0, 0, 1}, 1.0F), {0, 0, -1}) == Approx(0.15F));
    CHECK(LooseShape::of(ObjectType{}).reach({}, {0, 0, -1}) == 0.0F);
}

TEST_CASE("a dropped brick falls, bounces once, settles and stops on its next floor contact",
          "[world_objects][physics]") {
    LooseObjects objects;
    // Dropped tilted 15 degrees about x, with no velocity.
    objects.start(7, Vec3{1, 2, 1.18F}, turn({1, 0, 0}, std::numbers::pi_v<float> / 12.0F),
                  LooseObjects::Kind::of(brickType()));
    const LooseObject* brick = objects.find(7);
    REQUIRE(brick != nullptr);

    // The first update lasts 1/60 s; later ones 1/30 s, each taking 15.68 / 30 m/s off the vertical velocity.
    objects.step(floorRay);
    CHECK(brick->velocity.z == Approx(-15.68F / 60.0F));
    objects.step(floorRay);
    CHECK(brick->velocity.z == Approx(-15.68F / 60.0F - 15.68F / 30.0F));
    CHECK(brick->interval() == 2);

    // Falls until the floor: the first contact starts a settle and bounces it up with e = 0.1, scaled by 0.98.
    float before = brick->velocity.z;
    int steps = 2;
    while (!brick->settling && steps < 100) {
        before = brick->velocity.z;
        objects.step(floorRay);
        ++steps;
    }
    REQUIRE(brick->settling);
    CHECK(brick->airborne);
    const float impact = before - (15.68F / 30.0F);
    CHECK(brick->velocity.z == Approx(-impact * 0.1F * 0.98F));
    CHECK(objects.settlesInUse() == 1);

    // While it settles each update lasts 1/60 s, and nothing else touches the floor.
    for (int i = 0; i < 5; ++i) {
        const float vz = brick->velocity.z;
        objects.step(floorRay);
        REQUIRE(brick != nullptr);
        CHECK(brick->velocity.z == Approx(vz - (15.68F / 60.0F)));
        CHECK(brick->airborne);
    }
    // The 11th tick ended the settle; later updates last 1/30 s again, and the next floor contact stops it.
    CHECK(brick->settled);
    CHECK_FALSE(brick->settling);
    std::optional<LooseObject> rested;
    for (int i = 0; i < 10 && !rested; ++i) {
        const float vz = brick->velocity.z;
        objects.step(floorRay, [&rested](double handle, const LooseObject& object) {
            CHECK(handle == 7);
            rested = object;
        });
        if (!rested) {
            CHECK(brick->velocity.z == Approx(vz - (15.68F / 30.0F)));
        }
    }
    REQUIRE(rested.has_value());
    CHECK(objects.find(7) == nullptr);
    CHECK(rested->grounded);
    CHECK_FALSE(rested->airborne);
    CHECK_FALSE(rested->settled);
    CHECK(rested->velocity == Vec3{});
    CHECK(rested->interval() == 20);
    // Flat on its face, its origin at the floor + its half-height + the 0.01 m back-off.
    CHECK(rotated(rested->rotation, {0, 0, 1}).z == Approx(1.0F).margin(1e-4));
    CHECK(rested->position.z == Approx(0.05F + LooseObjects::kBackOff).margin(1e-4));
    CHECK(rested->position.x == Approx(1.0F));
    CHECK(rested->position.y == Approx(2.0F));
}

TEST_CASE("an object whose type has no settle axes never settles", "[world_objects][physics]") {
    LooseObjects objects;
    ObjectType chair = brickType();
    chair.axis = 0;
    objects.start(3, Vec3{0, 0, 1.0F}, {}, LooseObjects::Kind::of(chair));
    for (int i = 0; i < 60; ++i) {
        objects.step(floorRay);
    }
    const LooseObject* object = objects.find(3);
    REQUIRE(object != nullptr);
    CHECK(object->airborne);
    CHECK_FALSE(object->settling);
    CHECK_FALSE(object->settled);
    CHECK(object->position.z >= 0.05F);
}

TEST_CASE("a wall bounces a flying object with no friction and no settle", "[world_objects][physics]") {
    LooseObjects objects;
    objects.start(4, Vec3{0, 0, 5.0F}, {}, LooseObjects::Kind{.axisMask = kAxisZ, .restitution = 0.1F},
                  Vec3{6.0F, 1.0F, 0});
    const auto wall = [](Vec3 origin, Vec3 direction, float length) -> std::optional<RayContact> {
        if (origin.x > 1.0F || direction.x <= 0.0F) {
            return std::nullopt;
        }
        const float distance = (1.0F - origin.x) / direction.x;
        if (distance > length) {
            return std::nullopt;
        }
        return RayContact{.distance = distance, .normal = {-1, 0, 0}};
    };
    for (int i = 0; i < 6; ++i) {
        objects.step(wall);
    }
    const LooseObject* object = objects.find(4);
    REQUIRE(object != nullptr);
    CHECK(object->velocity.x == Approx(-6.0F * 0.1F));
    CHECK(object->velocity.y == Approx(1.0F));
    CHECK(object->position.x < 1.0F);
    CHECK_FALSE(object->settling);
}

TEST_CASE("with every settle slot busy a landing starts no settle; removing an object frees its slot",
          "[world_objects][physics]") {
    LooseObjects objects;
    for (int i = 0; i < 65; ++i) {
        objects.start(100 + i, Vec3{static_cast<float>(i), 0, 0.005F}, {}, LooseObjects::Kind{.axisMask = kAxisZ});
    }
    objects.step(floorRay);
    objects.step(floorRay);
    int settling = 0;
    for (const auto& [handle, object] : objects.all()) {
        settling += object.settling ? 1 : 0;
    }
    CHECK(settling == static_cast<int>(LooseObjects::kSettleSlots));
    CHECK(objects.settlesInUse() == LooseObjects::kSettleSlots);
    objects.remove(100);
    CHECK(objects.find(100) == nullptr);
    CHECK(objects.settlesInUse() == LooseObjects::kSettleSlots - 1);
}
