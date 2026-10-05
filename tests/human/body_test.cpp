// SPDX-License-Identifier: GPL-3.0-or-later
#include "human/body.h"

#include <array>
#include <cstdint>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "support/collision_fixtures.h"

using Catch::Approx;
using coney::anim::Vec3;
using coney::human::WallFilter;

TEST_CASE("a wall triangle whose steepest edge rises less than 0.25 m is not a wall", "[human][body]") {
    // The two triangles of a riser 4 m wide: 0.2 m is a step, 0.26 m and more are walls.
    for (const float height : {0.1F, 0.2F, 0.24F}) {
        CHECK(coney::human::wallTooLow(Vec3{0, 0, 0}, Vec3{4, 0, 0}, Vec3{4, 0, height}, 0.25F));
        CHECK(coney::human::wallTooLow(Vec3{0, 0, 0}, Vec3{4, 0, height}, Vec3{0, 0, height}, 0.25F));
    }
    // (A riser exactly 0.25 m tall and 4 m wide is caught by the sliver rule below: its corner lies 0.2495 m off the
    // diagonal.)
    for (const float height : {0.26F, 0.3F, 0.5F, 3.0F}) {
        CHECK_FALSE(coney::human::wallTooLow(Vec3{0, 0, 0}, Vec3{4, 0, 0}, Vec3{4, 0, height}, 0.25F));
        CHECK_FALSE(coney::human::wallTooLow(Vec3{0, 0, 0}, Vec3{4, 0, height}, Vec3{0, 0, height}, 0.25F));
    }
}

TEST_CASE("a thin sliver along a slanted longest edge is not a wall, a narrow upright one is", "[human][body]") {
    // A sliver whose steepest edge is tall enough but whose third corner is 0.1 m off a long slanted edge.
    CHECK(coney::human::wallTooLow(Vec3{0, 0, 0}, Vec3{6, 0, 2}, Vec3{3, 0, 1.1F}, 0.25F));
    // A post's side: 0.1 m wide and 2 m tall, its longest edge (the diagonal) nearly vertical.
    CHECK_FALSE(coney::human::wallTooLow(Vec3{0, 0, 0}, Vec3{0.1F, 0, 0}, Vec3{0.1F, 0, 2}, 0.25F));
}

TEST_CASE("the walking sphere is 0.35 m times the scale, its bottom 0.05 m above the feet", "[human][body]") {
    CHECK(coney::human::walkingRadius(1.0F) == Approx(0.35F));
    CHECK(coney::human::walkingRadius(0.97F) == Approx(0.3395F));
    CHECK(coney::human::walkingCentreHeight(0.97F) == Approx(0.3895F));
}

TEST_CASE("the wall push keeps the walls a move goes into, tall enough, and not passed through", "[human][body]") {
    // A kerb's riser 0.2 m tall and a ledge's 0.5 m tall, both facing -y at y = 40; a fence of material 30 at y = 60.
    const auto kerb = coney::test::makeMesh(coney::test::wallFacingMinusY(40.0F, 0.0F, 80.0F, 0.0F, 0.2F));
    const auto ledge = coney::test::makeMesh(coney::test::wallFacingMinusY(40.0F, 0.0F, 80.0F, 0.0F, 0.5F));
    const auto fence = coney::test::makeMesh(coney::test::wallFacingMinusY(60.0F, 0.0F, 80.0F, 0.0F, 1.5F, 0, 30));
    std::vector<std::uint16_t> scratch;
    const float radius = coney::human::walkingRadius(1.0F);
    // The sphere 0.2 m short of the face, its centre 0.4 m up.
    const Vec3 centre{40.0F, 39.8F, coney::human::walkingCentreHeight(1.0F)};
    const WallFilter walking{.move = Vec3{0.0F, 0.1F, 0.0F}, .skipLow = true, .excludeMaterials = {}};

    CHECK_FALSE(coney::human::nearestWallPush(*kerb, centre, radius, walking, scratch).has_value());
    const auto push = coney::human::nearestWallPush(*ledge, centre, radius, walking, scratch);
    REQUIRE(push.has_value());
    if (push) {
        CHECK(push->y == Approx(-(radius - 0.2F)));
    }
    // Moving away from the face: not a wall for this move.
    const WallFilter away{.move = Vec3{0.0F, -0.1F, 0.0F}, .skipLow = true, .excludeMaterials = {}};
    CHECK_FALSE(coney::human::nearestWallPush(*ledge, centre, radius, away, scratch).has_value());
    // The airborne push-out (no step rule, no move) meets the kerb too.
    const WallFilter air{.move = Vec3{}, .skipLow = false, .excludeMaterials = {}};
    CHECK(coney::human::nearestWallPush(*kerb, Vec3{40.0F, 39.8F, 0.2F}, radius, air, scratch).has_value());
    // A climbing body passes through the fence materials.
    const Vec3 atFence{40.0F, 59.8F, 0.4F};
    CHECK(coney::human::nearestWallPush(*fence, atFence, radius, walking, scratch).has_value());
    const WallFilter climbing{
        .move = Vec3{0.0F, 0.1F, 0.0F}, .skipLow = true, .excludeMaterials = coney::human::kFenceMaterials};
    CHECK_FALSE(coney::human::nearestWallPush(*fence, atFence, radius, climbing, scratch).has_value());
}
