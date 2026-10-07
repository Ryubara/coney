// SPDX-License-Identifier: GPL-3.0-or-later
// The front-end world's view through the Wonder Wheel scene's camera (docs/research/frontend.md#background): a scene
// camera's pose and lens as the world renderer takes them.
#include "platform/front_end_scene.h"

#include <cmath>
#include <numbers>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using Catch::Approx;
using coney::platform::sceneCameraWorldView;

namespace {

// The dot product of two RenderWare vectors.
float dot(coney::world::Vec3 a, coney::world::Vec3 b) { return (a.x * b.x) + (a.y * b.y) + (a.z * b.z); }

} // namespace

TEST_CASE("front-end scene: an unturned scene camera looks along the game's +y with its lens", "[front_end_scene]") {
    const coney::scenes::ScenePose pose{.position = {462.60F, -122.35F, -187.93F},
                                        .rotation = {0.0F, 0.0F, 0.0F, 1.0F}};
    const coney::scenes::SceneLens lens{.fieldOfView = 54.43F, .nearClip = 0.5F, .farClip = 150.0F};
    const coney::platform::WorldView view = sceneCameraWorldView(pose, lens);
    // The game's (x, y, z) is RenderWare's (x, z, -y).
    CHECK(view.pose.position.x == Approx(462.60F));
    CHECK(view.pose.position.y == Approx(-187.93F));
    CHECK(view.pose.position.z == Approx(122.35F));
    CHECK(view.pose.forward.z == Approx(-1.0F));
    CHECK(view.pose.up.y == Approx(1.0F));
    CHECK(view.pose.right.x == Approx(1.0F));
    // The lens's view window on the 4:3 picture.
    CHECK(view.halfWidth == Approx(std::tan(54.43F / 2.0F * std::numbers::pi_v<float> / 180.0F)));
    CHECK(view.halfHeight == Approx(view.halfWidth * 0.75F));
    CHECK(view.nearClip == Approx(0.5F));
    CHECK(view.drawDistance == Approx(150.0F));
}

TEST_CASE("front-end scene: a scene camera turned about the game's up axis turns its frame with it",
          "[front_end_scene]") {
    // 90° about z: the game's +y turns to -x.
    const float half = std::numbers::pi_v<float> / 4.0F;
    const coney::scenes::ScenePose pose{.position = {}, .rotation = {0.0F, 0.0F, std::sin(half), std::cos(half)}};
    const coney::platform::WorldView view = sceneCameraWorldView(pose, coney::scenes::SceneLens{});
    CHECK(view.pose.forward.x == Approx(-1.0F));
    CHECK(view.pose.forward.y == Approx(0.0F).margin(1e-6));
    CHECK(view.pose.up.y == Approx(1.0F));
    // An orthonormal frame.
    CHECK(dot(view.pose.forward, view.pose.up) == Approx(0.0F).margin(1e-6));
    CHECK(dot(view.pose.forward, view.pose.right) == Approx(0.0F).margin(1e-6));
    CHECK(dot(view.pose.right, view.pose.right) == Approx(1.0F));
}
