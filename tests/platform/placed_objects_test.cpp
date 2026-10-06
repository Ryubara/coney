// SPDX-License-Identifier: GPL-3.0-or-later
// Where a dynamic object's model is drawn (docs/research/objects.md#models): the object's pose in the game's axes
// carried into RenderWare's, after the model's own frame.
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
    const Mat34 m = coney::platform::objectRenderTransform(Quat{}, Vec3{515.51F, -68.88F, -188.65F}, Mat34{});
    // The game's (x, y, z) is RenderWare's (x, z, -y).
    checkSame(coney::anim::transformPoint(m, Vec3{}), Vec3{515.51F, -188.65F, 68.88F});
    // The model's y (up) is the game's z (up): still RenderWare's y.
    checkSame(coney::anim::transformDirection(m, Vec3{0, 1, 0}), Vec3{0, 1, 0});
}

TEST_CASE("an object turned about the game's up axis turns its model about RenderWare's", "[placed_objects]") {
    // A quarter turn about the game's z: the game's x becomes its y.
    const float half = std::numbers::pi_v<float> / 4.0F;
    const Quat turn{0, 0, std::sin(half), std::cos(half)};
    const Mat34 m = coney::platform::objectRenderTransform(turn, Vec3{}, Mat34{});
    // RenderWare's x (the game's x) becomes the game's y, RenderWare's -z; the up axis stays.
    checkSame(coney::anim::transformDirection(m, Vec3{1, 0, 0}), Vec3{0, 0, -1});
    checkSame(coney::anim::transformDirection(m, Vec3{0, 1, 0}), Vec3{0, 1, 0});
}

TEST_CASE("the model's own frame comes before the object's pose", "[placed_objects]") {
    Mat34 frame;
    frame.t = Vec3{0, 2, 0}; // two metres up in the model's (RenderWare's) axes
    const Mat34 m = coney::platform::objectRenderTransform(Quat{}, Vec3{1, 2, 3}, frame);
    checkSame(coney::anim::transformPoint(m, Vec3{}), Vec3{1, 5, -2});
}
