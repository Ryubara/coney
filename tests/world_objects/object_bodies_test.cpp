// SPDX-License-Identifier: GPL-3.0-or-later
// World-object bodies (world_objects/object_bodies.h): a type's box or sphere at its object's pose, the push that
// moves a walking sphere out of one, and the layers that say who meets it. Synthetic types and records only.
#include "world_objects/object_bodies.h"

#include <cmath>
#include <cstdint>
#include <numbers>
#include <optional>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "animation/anim_math.h"
#include "world_objects/object_types.h"
#include "world_objects/spawn_records.h"

using Catch::Approx;
using coney::anim::Vec3;
using namespace coney::world_objects;

namespace {

// A trash-can-like type: a 0.84 × 0.74 × 0.97 box centred 0.485 m up, blocking humans and taking strikes.
ObjectType can() {
    ObjectType type;
    type.name = "test_can";
    type.bodyShape = kBodyBox;
    type.bodySize = {0.84F, 0.74F, 0.97F};
    type.bodyCentre = {0.0F, 0.0F, 0.485F};
    type.bodyWord = static_cast<std::uint16_t>(kPhyBlockHumans | kPhyMeleeTarget);
    return type;
}

// A record of handle 7 at `at`, turned `degrees` about z.
SpawnRecord at(Vec3 position, float degrees = 0.0F) {
    SpawnRecord record;
    record.handle = 7;
    record.position = {position.x, position.y, position.z};
    const float half = degrees * std::numbers::pi_v<float> / 360.0F;
    record.rotation = {0.0F, 0.0F, std::sin(half), std::cos(half)};
    return record;
}

} // namespace

TEST_CASE("an OBB type's body is its box offset by the centre in the object's frame; other shapes have none",
          "[objects]") {
    const std::optional<ObjectBody> body = bodyOf(at({10.0F, 0.0F, 0.0F}), can());
    REQUIRE(body.has_value());
    if (!body) {
        return;
    }
    CHECK(body->pose.t.x == Approx(10.0F));
    CHECK(body->pose.t.z == Approx(0.485F));
    CHECK(body->half.x == Approx(0.42F));
    ObjectType none = can();
    none.bodyShape = 0;
    CHECK_FALSE(bodyOf(at({}), none).has_value());
    ObjectType ball = can();
    ball.bodyShape = kBodySphere;
    const std::optional<ObjectBody> sphere = bodyOf(at({}), ball);
    REQUIRE(sphere.has_value());
    if (!sphere) {
        return;
    }
    CHECK(sphere->radius == Approx(0.42F));
}

TEST_CASE("a sphere overlapping a box is pushed out to touch it, through the nearest face when inside", "[objects]") {
    const std::optional<ObjectBody> box = bodyOf(at({0.0F, 0.0F, 0.0F}), can());
    REQUIRE(box.has_value());
    if (!box) {
        return;
    }
    const ObjectBody& body = *box;
    // 0.6 m east of the centre at the box's mid height: 0.18 from the face, a 0.485 sphere goes 0.305 further east.
    const std::optional<Vec3> push = spherePush(body, {0.6F, 0.0F, 0.485F}, 0.485F);
    REQUIRE(push.has_value());
    if (!push) {
        return;
    }
    CHECK(push->x == Approx(0.305F));
    CHECK(push->y == Approx(0.0F).margin(1e-5));
    CHECK_FALSE(spherePush(body, {1.0F, 0.0F, 0.485F}, 0.485F).has_value());
    // Inside, nearer the north face (half y 0.37): out along +y.
    const std::optional<Vec3> inside = spherePush(body, {0.0F, 0.3F, 0.485F}, 0.1F);
    REQUIRE(inside.has_value());
    if (!inside) {
        return;
    }
    CHECK(inside->y == Approx(0.17F));
    // The box turned 90 degrees: its long side now runs along y.
    const std::optional<ObjectBody> turnedBox = bodyOf(at({0.0F, 0.0F, 0.0F}, 90.0F), can());
    REQUIRE(turnedBox.has_value());
    if (!turnedBox) {
        return;
    }
    const ObjectBody& turned = *turnedBox;
    CHECK_FALSE(spherePush(turned, {0.6F, 0.0F, 0.485F}, 0.2F).has_value());
    CHECK(spherePush(turned, {0.0F, 0.6F, 0.485F}, 0.2F).has_value());
}

TEST_CASE("a walker meets only the bodies of its layer it moves into, pushed in the ground plane", "[objects]") {
    ObjectBodies bodies;
    const std::optional<ObjectBody> box = bodyOf(at({0.0F, 0.0F, 0.0F}), can());
    REQUIRE(box.has_value());
    if (!box) {
        return;
    }
    bodies.add(*box);
    const Vec3 centre{0.6F, 0.0F, 0.6F};
    // Walking west into it: pushed back east, with no z.
    const std::optional<Vec3> push = bodies.pushOut(centre, 0.485F, kPhyBlockHumans, {-0.1F, 0.0F, 0.0F});
    REQUIRE(push.has_value());
    if (!push) {
        return;
    }
    CHECK(push->x > 0.0F);
    CHECK(push->z == 0.0F);
    // Walking east, away from it: not met; nor by another layer.
    CHECK_FALSE(bodies.pushOut(centre, 0.485F, kPhyBlockHumans, {0.1F, 0.0F, 0.0F}).has_value());
    CHECK_FALSE(bodies.pushOut(centre, 0.485F, kPhyBlockObjects, {-0.1F, 0.0F, 0.0F}).has_value());
    // The strikes' query.
    CHECK(bodies.touching(centre, 0.485F, kPhyMeleeTarget).size() == 1);
    CHECK(bodies.touching(centre, 0.485F, kPhyThrownWeaponTarget).empty());
}
