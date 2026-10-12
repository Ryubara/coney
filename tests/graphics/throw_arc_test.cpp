// SPDX-License-Identifier: GPL-3.0-or-later
// The throw-aim arc's ribbon (docs/research/graphics.md#throw-arc), on synthetic arcs.
#include "graphics/throw_arc.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using Catch::Approx;
using coney::anim::Vec3;
using coney::graphics::blendThrowArc;
using coney::graphics::kThrowArcPairs;
using coney::graphics::throwArcIndices;
using coney::graphics::throwArcStrip;
using coney::graphics::ThrowArcVertex;

namespace {

// A throw along +y from the hand: `count` points a metre apart, rising then falling.
std::vector<Vec3> arcOf(std::size_t count) {
    std::vector<Vec3> points;
    for (std::size_t i = 0; i < count; ++i) {
        const auto along = static_cast<float>(i);
        points.push_back(Vec3{0.0F, along, 1.5F + (0.4F * along) - (0.03F * along * along)});
    }
    return points;
}

// The thrower's right axis for a throw along +y.
constexpr Vec3 kRight{1.0F, 0.0F, 0.0F};

// The ribbon's centre at pair `pair`.
Vec3 centreOf(const std::vector<ThrowArcVertex>& strip, std::size_t pair) {
    const Vec3 left = strip[pair * 2].position;
    const Vec3 right = strip[(pair * 2) + 1].position;
    return Vec3{(left.x + right.x) / 2.0F, (left.y + right.y) / 2.0F, (left.z + right.z) / 2.0F};
}

} // namespace

TEST_CASE("an arc that met nothing is 36 vertices, a pair on each of its 18 points", "[throw_arc]") {
    const std::vector<Vec3> arc = arcOf(18);
    const auto strip = throwArcStrip(arc, kRight, false, false);
    REQUIRE(strip.size() == 36);
    for (std::size_t pair = 0; pair < kThrowArcPairs; ++pair) {
        const Vec3 centre = centreOf(strip, pair);
        CHECK(centre.x == Approx(arc[pair].x).margin(1e-6));
        CHECK(centre.y == Approx(arc[pair].y));
        CHECK(centre.z == Approx(arc[pair].z));
    }
}

TEST_CASE("the ribbon is 0.16 m wide along the thrower's right axis, level across the arc", "[throw_arc]") {
    const auto strip = throwArcStrip(arcOf(18), kRight, false, false);
    REQUIRE(strip.size() == 36);
    for (std::size_t pair = 0; pair < kThrowArcPairs; ++pair) {
        const Vec3 left = strip[pair * 2].position;
        const Vec3 right = strip[(pair * 2) + 1].position;
        CHECK(right.x - left.x == Approx(0.16F));
        CHECK(right.y == Approx(left.y));
        CHECK(right.z == Approx(left.z));
    }
    // A thrower facing +x has his right axis along -y: the ribbon follows it.
    const std::vector<Vec3> sideways{Vec3{0.0F, 0.0F, 1.0F}, Vec3{1.0F, 0.0F, 1.2F}, Vec3{2.0F, 0.0F, 1.3F}};
    const auto turned = throwArcStrip(sideways, Vec3{0.0F, -1.0F, 0.0F}, false, false);
    REQUIRE(turned.size() == 36);
    CHECK(turned[1].position.y - turned[0].position.y == Approx(-0.16F));
    CHECK(turned[0].position.x == Approx(turned[1].position.x));
}

TEST_CASE("the texture runs u across and v from 15 at the hand down by one a pair", "[throw_arc]") {
    const auto strip = throwArcStrip(arcOf(18), kRight, false, false);
    REQUIRE(strip.size() == 36);
    for (std::size_t pair = 0; pair < kThrowArcPairs; ++pair) {
        CHECK(strip[pair * 2].u == 0.0F);
        CHECK(strip[(pair * 2) + 1].u == 1.0F);
        const float v = 15.0F - static_cast<float>(pair);
        CHECK(strip[pair * 2].v == v);
        CHECK(strip[(pair * 2) + 1].v == v);
    }
    CHECK(strip.back().v == -2.0F);
}

TEST_CASE("the alpha is 0 at the hand, then 127 rising by pair over 18", "[throw_arc]") {
    const auto strip = throwArcStrip(arcOf(18), kRight, false, false);
    REQUIRE(strip.size() == 36);
    const std::array<int, 18> expected{0,   127, 128, 129, 130, 131, 132, 133, 134,
                                       135, 135, 136, 137, 138, 139, 140, 141, 142};
    for (std::size_t pair = 0; pair < kThrowArcPairs; ++pair) {
        CHECK(strip[pair * 2].rgba[3] == expected[pair]);
        CHECK(strip[(pair * 2) + 1].rgba[3] == expected[pair]);
    }
}

TEST_CASE("the ribbon is (127, 127, 127), and (170, 43, 43) when the arc meets a target", "[throw_arc]") {
    const auto plain = throwArcStrip(arcOf(18), kRight, false, false);
    const auto hit = throwArcStrip(arcOf(18), kRight, false, true);
    REQUIRE(plain.size() == 36);
    REQUIRE(hit.size() == 36);
    for (std::size_t i = 0; i < plain.size(); ++i) {
        CHECK(plain[i].rgba[0] == 127);
        CHECK(plain[i].rgba[1] == 127);
        CHECK(plain[i].rgba[2] == 127);
        CHECK(hit[i].rgba[0] == 170);
        CHECK(hit[i].rgba[1] == 43);
        CHECK(hit[i].rgba[2] == 43);
        // The colour changes the ribbon's tint, not its fade.
        CHECK(hit[i].rgba[3] == plain[i].rgba[3]);
    }
}

TEST_CASE("an arc cut at a contact collapses its later pairs on the point before it and ends on the contact",
          "[throw_arc]") {
    // A contact in segment 5: the trace's 6 points, the 6th being the contact.
    const std::vector<Vec3> arc = arcOf(6);
    const auto strip = throwArcStrip(arc, kRight, true, true);
    REQUIRE(strip.size() == 36);
    for (std::size_t pair = 0; pair < 5; ++pair) {
        CHECK(centreOf(strip, pair).y == Approx(arc[pair].y));
    }
    for (std::size_t pair = 5; pair <= 16; ++pair) {
        CHECK(centreOf(strip, pair).y == Approx(arc[4].y));
        CHECK(centreOf(strip, pair).z == Approx(arc[4].z));
    }
    CHECK(centreOf(strip, 17).y == Approx(arc[5].y));
    CHECK(centreOf(strip, 17).z == Approx(arc[5].z));
    // The alpha rises over the 5 points before the contact and is 143 from there on.
    CHECK(strip[0].rgba[3] == 0);
    CHECK(strip[2].rgba[3] == 127 + (16 * 1 / 5));
    CHECK(strip[8].rgba[3] == 127 + (16 * 4 / 5));
    for (std::size_t pair = 5; pair < kThrowArcPairs; ++pair) {
        CHECK(strip[pair * 2].rgba[3] == 143);
    }
}

TEST_CASE("a contact in the last segment of an 18-point arc leaves 17 points before it", "[throw_arc]") {
    const std::vector<Vec3> arc = arcOf(18);
    const auto cut = throwArcStrip(arc, kRight, true, false);
    const auto free = throwArcStrip(arc, kRight, false, false);
    REQUIRE(cut.size() == 36);
    // Pair 16 is on point 16 and the last pair on the contact, as for a free arc; only the alpha's span differs.
    CHECK(centreOf(cut, 16).y == Approx(arc[16].y));
    CHECK(centreOf(cut, 17).y == Approx(arc[17].y));
    CHECK(cut[34].rgba[3] == 143);
    CHECK(free[34].rgba[3] == 142);
}

TEST_CASE("a cut arc of two points collapses every middle pair on the hand", "[throw_arc]") {
    const std::vector<Vec3> arc = arcOf(2);
    const auto strip = throwArcStrip(arc, kRight, true, false);
    REQUIRE(strip.size() == 36);
    for (std::size_t pair = 0; pair <= 16; ++pair) {
        CHECK(centreOf(strip, pair).y == Approx(arc[0].y));
    }
    CHECK(centreOf(strip, 17).y == Approx(arc[1].y));
}

TEST_CASE("an arc of fewer than two points draws nothing", "[throw_arc]") {
    const std::vector<Vec3> arc = arcOf(1);
    CHECK(throwArcStrip(arc, kRight, false, false).empty());
    CHECK(throwArcStrip({}, kRight, false, true).empty());
}

TEST_CASE("the triangle list has 34 triangles, two a pair of pairs", "[throw_arc]") {
    const std::vector<std::uint16_t> indices = throwArcIndices();
    REQUIRE(indices.size() == std::size_t{34} * 3);
    // The first quad joins pairs 0 and 1: vertices 0, 1 (pair 0) and 2, 3 (pair 1).
    CHECK(indices[0] == 0);
    CHECK(indices[1] == 1);
    CHECK(indices[2] == 2);
    CHECK(indices[3] == 1);
    CHECK(indices[4] == 3);
    CHECK(indices[5] == 2);
    for (const std::uint16_t index : indices) {
        CHECK(index < 36);
    }
}

TEST_CASE("the arc is drawn between its last two steps, and exactly the newest at alpha 1", "[throw_arc]") {
    const std::vector<Vec3> before = arcOf(5);
    std::vector<Vec3> moved = before;
    for (Vec3& p : moved) {
        p.x += 1.0F;
    }
    const auto half = blendThrowArc(before, moved, 0.5F);
    REQUIRE(half.size() == 5);
    CHECK(half[2].x == Approx(0.5F));
    const auto newest = blendThrowArc(before, moved, 1.0F);
    CHECK(newest[4].x == moved[4].x);
    // A different number of points (the arc cut by a wall) is not blended.
    const auto cut = blendThrowArc(std::span<const Vec3>(before.data(), 3), moved, 0.5F);
    CHECK(cut[0].x == moved[0].x);
}
