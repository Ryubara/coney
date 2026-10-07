// SPDX-License-Identifier: GPL-3.0-or-later
// The effects' near test (docs/research/objects.md#shatter): a camera within range, the point in its view by a margin.
#include "effects/effect_culling.h"

#include <catch2/catch_test_macros.hpp>

#include "camera/camera_view.h"

using coney::anim::Vec3;
using coney::effects::effectNearView;
using coney::effects::pointInView;

namespace {

// A camera at the origin looking along +y.
coney::camera::CameraView camera() {
    return coney::camera::viewLookingAt(Vec3{0.0F, 0.0F, 0.0F}, Vec3{0.0F, 10.0F, 0.0F}, 65.0F, 0.1F, 115.0F);
}

} // namespace

TEST_CASE("a point ahead is in the view; behind it only within the margin", "[effects][culling]") {
    CHECK(pointInView(camera(), Vec3{0.0F, 5.0F, 0.0F}, 0.0F));
    CHECK_FALSE(pointInView(camera(), Vec3{0.0F, -5.0F, 0.0F}, 0.0F));
    CHECK(pointInView(camera(), Vec3{0.0F, -5.0F, 0.0F}, 10.0F));
    CHECK_FALSE(pointInView(camera(), Vec3{0.0F, -12.0F, 0.0F}, 10.0F));
    // Off to the side of a point 5 m ahead: outside the view window, but within a 10 m margin.
    CHECK_FALSE(pointInView(camera(), Vec3{12.0F, 5.0F, 0.0F}, 0.0F));
    CHECK(pointInView(camera(), Vec3{12.0F, 5.0F, 0.0F}, 10.0F));
}

TEST_CASE("the shatter's test: within 15 m of the camera and in view by 10 m", "[effects][culling]") {
    CHECK(effectNearView(camera(), Vec3{0.0F, 14.0F, 0.0F}, 15.0F, 10.0F));
    CHECK_FALSE(effectNearView(camera(), Vec3{0.0F, 16.0F, 0.0F}, 15.0F, 10.0F));
    CHECK(effectNearView(camera(), Vec3{0.0F, -8.0F, 0.0F}, 15.0F, 10.0F));
    CHECK_FALSE(effectNearView(camera(), Vec3{0.0F, -11.0F, 0.0F}, 15.0F, 10.0F));
}
