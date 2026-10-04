// SPDX-License-Identifier: GPL-3.0-or-later
#include "camera/camera_lens.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using Catch::Approx;

TEST_CASE("the player camera sees 65 degrees across a 4:3 picture, from 0.1 to 115", "[camera_lens]") {
    const coney::camera::CameraLens& lens = coney::camera::kPlayerCameraLens;
    CHECK(lens.fieldOfView == 65.0F);
    CHECK(lens.nearClip == 0.1F);
    CHECK(lens.farClip == 115.0F);
    // (0.637, 0.478), docs/research/world.md#player-camera.
    const coney::camera::ViewWindow window = coney::camera::viewWindow(lens);
    CHECK(window.halfWidth == Approx(0.637).margin(5e-4));
    CHECK(window.halfHeight == Approx(0.478).margin(5e-4));
}

TEST_CASE("the base camera's view window follows its 60 degrees", "[camera_lens]") {
    const coney::camera::ViewWindow window = coney::camera::viewWindow(coney::camera::kBaseCameraLens);
    CHECK(window.halfWidth == Approx(0.5774).margin(1e-4));
    CHECK(window.halfHeight == Approx(0.4330).margin(1e-4));
}
