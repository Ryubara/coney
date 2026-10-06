// SPDX-License-Identifier: GPL-3.0-or-later
// The front-end scene's stand-in camera (docs/research/frontend.md#background): the Wonder Wheel scene's first camera
// pose and lens, turned to the hub and to the left.
#include "platform/front_end_scene.h"

#include <cmath>
#include <numbers>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using Catch::Approx;
using coney::platform::FrontEndWorldScene;

namespace {

// The dot product of two RenderWare vectors.
float dot(coney::world::Vec3 a, coney::world::Vec3 b) { return (a.x * b.x) + (a.y * b.y) + (a.z * b.z); }

} // namespace

TEST_CASE("front-end scene: the camera at the scene's first pose with its lens, level, turned left of the hub",
          "[front_end_scene]") {
    const coney::platform::WorldView view = coney::platform::frontEndSceneView();
    // The game's (x, y, z) is RenderWare's (x, z, -y).
    CHECK(view.pose.position.x == Approx(462.60F));
    CHECK(view.pose.position.y == Approx(-187.93F));
    CHECK(view.pose.position.z == Approx(122.35F));
    CHECK(view.halfWidth == Approx(std::tan(54.43F / 2.0F * std::numbers::pi_v<float> / 180.0F)));
    CHECK(view.halfHeight == Approx(view.halfWidth * 0.75F));
    CHECK(view.nearClip == Approx(0.5F));
    CHECK(view.drawDistance == Approx(150.0F));
    // An orthonormal frame.
    CHECK(dot(view.pose.forward, view.pose.forward) == Approx(1.0F));
    CHECK(dot(view.pose.up, view.pose.up) == Approx(1.0F));
    CHECK(dot(view.pose.forward, view.pose.up) == Approx(0.0F).margin(1e-5));
    CHECK(dot(view.pose.forward, view.pose.right) == Approx(0.0F).margin(1e-5));
    // The hub is 10.7° off the forward axis, on the right of the picture.
    const coney::world::Vec3 hub{515.51F - 462.60F, -188.67F + 187.93F, 68.89F - 122.35F};
    const float length = std::sqrt(dot(hub, hub));
    const float horizontal = std::sqrt((hub.x * hub.x) + (hub.z * hub.z));
    const float forwardHorizontal =
        std::sqrt((view.pose.forward.x * view.pose.forward.x) + (view.pose.forward.z * view.pose.forward.z));
    const float cosTurn =
        ((hub.x * view.pose.forward.x) + (hub.z * view.pose.forward.z)) / (horizontal * forwardHorizontal);
    CHECK(std::acos(cosTurn) * 180.0F / std::numbers::pi_v<float> ==
          Approx(FrontEndWorldScene::kHubYawOffsetDegrees).margin(0.05));
    CHECK(dot(hub, view.pose.right) / length > 0.0F);
}
