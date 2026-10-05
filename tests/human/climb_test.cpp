// SPDX-License-Identifier: GPL-3.0-or-later
#include "human/climb.h"

#include <optional>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "support/collision_fixtures.h"

using Catch::Approx;
using coney::anim::Vec3;
using coney::human::ClimbKind;
using coney::test::blockAlongY;
using coney::test::floorAt;
using coney::test::join;
using coney::test::makeMesh;

namespace {

// Facing +y from (40, 38, 0): the obstacles below stand with their face at y = 40, 2 m ahead.
constexpr Vec3 kFeet{40.0F, 38.0F, 0.0F};
constexpr Vec3 kAhead{0.0F, 1.0F, 0.0F};
// The climbable flag for anyone.
constexpr std::uint16_t kClimbable = coney::human::kTriangleClimbable;

// The climb found against an obstacle on flat ground, probing 4.5 m ahead as from a run.
std::optional<coney::human::ClimbProbe> probe(const std::vector<coney::test::Tri>& obstacle) {
    const auto mesh = makeMesh(join(floorAt(0.0F, 0.0F, 80.0F, 0.0F, 80.0F), obstacle));
    return coney::human::probeClimb(*mesh, kFeet, kAhead, 4.5F, true);
}

} // namespace

TEST_CASE("each climb starts at its standing clip, the running form three on", "[human][climb]") {
    CHECK(coney::human::climbFirstClip(ClimbKind::Fence, false) == 437U);
    CHECK(coney::human::climbFirstClip(ClimbKind::Fence, true) == 440U);
    CHECK(coney::human::climbFirstClip(ClimbKind::ShortFence, false) == 443U);
    CHECK(coney::human::climbFirstClip(ClimbKind::ShortFence, true) == 446U);
    CHECK(coney::human::climbFirstClip(ClimbKind::Wall, false) == 449U);
    CHECK(coney::human::climbFirstClip(ClimbKind::Wall, true) == 452U);
    CHECK(coney::human::climbFirstClip(ClimbKind::ShortWall, false) == 455U);
    CHECK(coney::human::climbFirstClip(ClimbKind::ShortWall, true) == 458U);
}

TEST_CASE("material 30, flag 0x80, and flag 0x4 for a player make a triangle climbable", "[human][climb]") {
    coney::raycast::RayHit hit;
    hit.material = 5;
    CHECK_FALSE(coney::human::climbable(hit, true));
    hit.material = 30;
    CHECK(coney::human::climbable(hit, false));
    hit.material = 5;
    hit.flags = 0x80;
    CHECK(coney::human::climbable(hit, false));
    hit.flags = 0x04;
    CHECK(coney::human::climbable(hit, true));
    CHECK_FALSE(coney::human::climbable(hit, false));
}

TEST_CASE("a thin climbable obstacle is a short fence below 1.7 m and a fence up to 2.5 m", "[human][climb]") {
    for (const float height : {0.75F, 1.0F, 1.6F}) {
        const auto found = probe(blockAlongY(30.0F, 50.0F, 40.0F, 40.08F, height, 0, 30));
        REQUIRE(found.has_value());
        if (found) {
            CHECK(found->kind == ClimbKind::ShortFence);
            CHECK(found->distance == Approx(2.0F));
            CHECK(found->normal.y == Approx(-1.0F));
        }
    }
    for (const float height : {1.8F, 2.0F, 2.4F}) {
        const auto found = probe(blockAlongY(30.0F, 50.0F, 40.0F, 40.08F, height, 0, 30));
        REQUIRE(found.has_value());
        if (found) {
            CHECK(found->kind == ClimbKind::Fence);
        }
    }
    // A fence of 2.5 m or more is not climbed, nor one below the low ray's 0.69 m.
    CHECK_FALSE(probe(blockAlongY(30.0F, 50.0F, 40.0F, 40.08F, 2.6F, 0, 30)).has_value());
    CHECK_FALSE(probe(blockAlongY(30.0F, 50.0F, 40.0F, 40.08F, 0.6F, 0, 30)).has_value());
}

TEST_CASE("a deep climbable block is a short wall with a top from 0.7 to 1.7 m and a wall from 1.7 to 2.91 m",
          "[human][climb]") {
    for (const float height : {0.72F, 1.2F, 1.65F}) {
        const auto found = probe(blockAlongY(30.0F, 50.0F, 40.0F, 43.0F, height, kClimbable));
        REQUIRE(found.has_value());
        if (found) {
            CHECK(found->kind == ClimbKind::ShortWall);
            CHECK(found->top == Approx(height).margin(1e-4));
        }
    }
    for (const float height : {1.8F, 2.5F, 2.9F}) {
        const auto found = probe(blockAlongY(30.0F, 50.0F, 40.0F, 43.0F, height, kClimbable));
        REQUIRE(found.has_value());
        if (found) {
            CHECK(found->kind == ClimbKind::Wall);
            CHECK(found->top == Approx(height).margin(1e-4));
        }
    }
    // Too tall for the wall window, too low for the low ray, or not climbable: no climb.
    CHECK_FALSE(probe(blockAlongY(30.0F, 50.0F, 40.0F, 43.0F, 3.2F, kClimbable)).has_value());
    CHECK_FALSE(probe(blockAlongY(30.0F, 50.0F, 40.0F, 43.0F, 0.5F, kClimbable)).has_value());
    CHECK_FALSE(probe(blockAlongY(30.0F, 50.0F, 40.0F, 43.0F, 1.2F)).has_value());
}

TEST_CASE("a climb needs the face within the probe's length and looking at the climber", "[human][climb]") {
    const auto mesh = makeMesh(
        join(floorAt(0.0F, 0.0F, 80.0F, 0.0F, 80.0F), blockAlongY(30.0F, 50.0F, 40.0F, 43.0F, 1.2F, kClimbable)));
    // 2 m away: within the running 4.5 m, beyond the standing 1.5 m.
    CHECK(coney::human::probeClimb(*mesh, kFeet, kAhead, 4.5F, true).has_value());
    CHECK_FALSE(coney::human::probeClimb(*mesh, kFeet, kAhead, 1.5F, true).has_value());
    // Approaching at 50° from the face's normal (n · d = -0.64): not facing it enough.
    const Vec3 slanted{0.766F, 0.643F, 0.0F};
    CHECK_FALSE(coney::human::probeClimb(*mesh, Vec3{38.0F, 38.5F, 0.0F}, slanted, 4.5F, true).has_value());
    // The jump's check from 1.7 m: the 1.2 m block is under it; a 2 m one is not.
    CHECK_FALSE(coney::human::climbableAhead(*mesh, kFeet, kAhead, 5.5F, true));
    const auto tall = makeMesh(
        join(floorAt(0.0F, 0.0F, 80.0F, 0.0F, 80.0F), blockAlongY(30.0F, 50.0F, 40.0F, 43.0F, 2.0F, kClimbable)));
    CHECK(coney::human::climbableAhead(*tall, kFeet, kAhead, 5.5F, true));
    CHECK_FALSE(coney::human::climbableAhead(*tall, Vec3{40.0F, 34.0F, 0.0F}, kAhead, 5.5F, true));
}

TEST_CASE("a standing climb reaches (r1 + r2) x 0.4, a running one from there to r2 x 2.2", "[human][climb]") {
    // The disc's fence reaches: 0.765 standing, 2.312 running.
    CHECK(coney::human::withinClimbReach(1.0F, 0.765F, 2.312F, false));
    CHECK_FALSE(coney::human::withinClimbReach(1.3F, 0.765F, 2.312F, false));
    CHECK_FALSE(coney::human::withinClimbReach(1.0F, 0.765F, 2.312F, true));
    CHECK(coney::human::withinClimbReach(1.3F, 0.765F, 2.312F, true));
    CHECK(coney::human::withinClimbReach(4.4F, 0.765F, 2.312F, true));
    CHECK_FALSE(coney::human::withinClimbReach(5.2F, 0.765F, 2.312F, true));
}

TEST_CASE("a climb clip's reach is its type-8 event's vector, else its displacement", "[human][climb]") {
    coney::anim::AnimClip clip;
    clip.displacement = Vec3{0.0F, 1.5F, 0.0F};
    CHECK(coney::human::clipReach(clip) == Approx(1.5F));
    coney::anim::ClipEvent other;
    other.type = 11;
    other.position = Vec3{0.0F, 0.0F, 13.0F};
    coney::anim::ClipEvent reach;
    reach.type = 8;
    reach.position = Vec3{0.0F, 2.3F, 0.0F};
    clip.events.push_back(other);
    clip.events.push_back(reach);
    CHECK(coney::human::clipReach(clip) == Approx(2.3F));
}
