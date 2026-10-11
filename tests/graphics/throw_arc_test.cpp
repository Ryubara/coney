// SPDX-License-Identifier: GPL-3.0-or-later
// The throw-aim arc's strip (docs/research/graphics.md#code-textures), on synthetic arcs.
#include "graphics/throw_arc.h"

#include <array>
#include <cmath>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using Catch::Approx;
using coney::anim::Vec3;
using coney::graphics::blendThrowArc;
using coney::graphics::throwArcStrip;

namespace {

// An arc thrown along +y: five points rising then falling.
const std::array<Vec3, 5> kArc{Vec3{0.0F, 0.0F, 1.5F}, Vec3{0.0F, 1.0F, 1.8F}, Vec3{0.0F, 2.0F, 1.9F},
                               Vec3{0.0F, 3.0F, 1.8F}, Vec3{0.0F, 4.0F, 1.5F}};

} // namespace

TEST_CASE("the arc's strip is two vertices a point, 0.16 m apart, lying flat across the throw", "[throw_arc]") {
    const auto strip = throwArcStrip(kArc, false);
    REQUIRE(strip.size() == kArc.size() * 2);
    for (std::size_t i = 0; i < kArc.size(); ++i) {
        const Vec3 left = strip[i * 2].position;
        const Vec3 right = strip[(i * 2) + 1].position;
        // Across the throw (along x for a throw along +y), level, centred on the point.
        CHECK(right.x - left.x == Approx(0.16F));
        CHECK(left.y == Approx(kArc[i].y));
        CHECK(right.y == Approx(kArc[i].y));
        CHECK(left.z == Approx(kArc[i].z));
        CHECK(right.z == Approx(kArc[i].z));
        CHECK((left.x + right.x) / 2.0F == Approx(kArc[i].x).margin(1e-6));
    }
    CHECK(strip.front().v == 0.0F);
    CHECK(strip.back().v == 1.0F);
    CHECK(strip.front().u == 0.0F);
    CHECK(strip[1].u == 1.0F);
}

TEST_CASE("the arc fades in from the hand", "[throw_arc]") {
    const auto strip = throwArcStrip(kArc, false);
    REQUIRE(strip.size() == 10);
    CHECK(strip[0].rgba[3] == 0);
    CHECK(strip[2].rgba[3] > 0);
    CHECK(strip[2].rgba[3] < coney::graphics::kThrowArcColour[3]);
    CHECK(strip[6].rgba[3] == coney::graphics::kThrowArcColour[3]);
    CHECK(strip[9].rgba[3] == coney::graphics::kThrowArcColour[3]);
}

TEST_CASE("the arc is grey, and red when it meets a target", "[throw_arc]") {
    const auto plain = throwArcStrip(kArc, false);
    const auto hit = throwArcStrip(kArc, true);
    REQUIRE(plain.size() == hit.size());
    CHECK(plain.back().rgba == coney::graphics::kThrowArcColour);
    CHECK(hit.back().rgba == coney::graphics::kThrowArcHitColour);
    // Grey has its three channels equal; red is mostly red.
    CHECK(plain.back().rgba[0] == plain.back().rgba[1]);
    CHECK(plain.back().rgba[1] == plain.back().rgba[2]);
    CHECK(hit.back().rgba[0] > 2 * hit.back().rgba[1]);
}

TEST_CASE("an arc of fewer than two points draws nothing", "[throw_arc]") {
    CHECK(throwArcStrip(std::span<const Vec3>(kArc.data(), 1), false).empty());
    CHECK(throwArcStrip({}, true).empty());
}

TEST_CASE("the arc is drawn between its last two steps, and exactly the newest at alpha 1", "[throw_arc]") {
    std::array<Vec3, 5> moved = kArc;
    for (Vec3& p : moved) {
        p.x += 1.0F;
    }
    const auto half = blendThrowArc(kArc, moved, 0.5F);
    REQUIRE(half.size() == 5);
    CHECK(half[2].x == Approx(0.5F));
    const auto newest = blendThrowArc(kArc, moved, 1.0F);
    CHECK(newest[4].x == moved[4].x);
    // A different number of points (the arc cut by a wall) is not blended.
    const auto cut = blendThrowArc(std::span<const Vec3>(kArc.data(), 3), moved, 0.5F);
    CHECK(cut[0].x == moved[0].x);
}
