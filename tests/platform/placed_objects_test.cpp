// SPDX-License-Identifier: GPL-3.0-or-later
// Where a dynamic object's model is drawn (docs/research/objects.md#models): the object's pose in the game's axes
// carried into RenderWare's, the model's vertices in the game's axes.
#include "platform/placed_objects.h"

#include <cmath>
#include <numbers>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "animation/anim_math.h"

using Catch::Approx;
using coney::anim::Mat34;
using coney::anim::Quat;
using coney::anim::Vec3;

namespace {

// Checks that `a` and `b` are the same point within a small margin.
void checkSame(Vec3 a, Vec3 b) {
    CHECK(a.x == Approx(b.x).margin(1e-4));
    CHECK(a.y == Approx(b.y).margin(1e-4));
    CHECK(a.z == Approx(b.z).margin(1e-4));
}

} // namespace

TEST_CASE("an unturned object's model is drawn at its position in RenderWare's axes", "[placed_objects]") {
    const Mat34 m = coney::platform::objectRenderTransform(Quat{}, Vec3{515.51F, -68.88F, -188.65F});
    // The game's (x, y, z) is RenderWare's (x, z, -y).
    checkSame(coney::anim::transformPoint(m, Vec3{}), Vec3{515.51F, -188.65F, 68.88F});
    // The model's z (up, the game's axes) is RenderWare's y.
    checkSame(coney::anim::transformDirection(m, Vec3{0, 0, 1}), Vec3{0, 1, 0});
}

TEST_CASE("an object turned about the game's up axis turns its model about RenderWare's", "[placed_objects]") {
    // A quarter turn about the game's z: the game's x becomes its y.
    const float half = std::numbers::pi_v<float> / 4.0F;
    const Quat turn{0, 0, std::sin(half), std::cos(half)};
    const Mat34 m = coney::platform::objectRenderTransform(turn, Vec3{});
    // The model's x becomes the game's y, RenderWare's -z; the up axis stays.
    checkSame(coney::anim::transformDirection(m, Vec3{1, 0, 0}), Vec3{0, 0, -1});
    checkSame(coney::anim::transformDirection(m, Vec3{0, 0, 1}), Vec3{0, 1, 0});
}

TEST_CASE("a model's point is placed in the game's axes, then carried into RenderWare's", "[placed_objects]") {
    // A point 1.4 m up the model (the objective disc's centre) on an object at (1, 2, 3).
    const Mat34 m = coney::platform::objectRenderTransform(Quat{}, Vec3{1, 2, 3});
    checkSame(coney::anim::transformPoint(m, Vec3{0, 0, 1.4F}), Vec3{1, 4.4F, -2});
}
