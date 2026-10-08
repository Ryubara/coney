// SPDX-License-Identifier: GPL-3.0-or-later
#include "platform/room_smoke_overlay.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "graphics/screen.h"

using Catch::Approx;
using coney::effects::SmokeSprite;
using coney::graphics::LogicalQuad;
using coney::graphics::LogicalRect;
using coney::graphics::logicalToWindow;
using coney::graphics::ScreenRect;
using coney::platform::roomSmokeQuad;

namespace {

// A drift at its narrowest: width scale 1.63 x 1.3, height 1.0, the centre at GUI y 0.2 (overlay Y 0.3).
SmokeSprite narrowSprite() {
    return SmokeSprite{.guiY = 0.2F, .width = 1.63F * 1.3F, .height = 1.0F, .u = 0.25F, .colour = {220, 220, 220, 40}};
}

} // namespace

TEST_CASE("the room smoke keeps the 4:3 overlay camera's projection in 4:3", "[room_smoke]") {
    // The screen is 1.595 x 1.1 overlay units at the GUI depth (docs/research/graphics.md#room-smoke).
    const SmokeSprite sprite = narrowSprite();
    const LogicalQuad quad = roomSmokeQuad(sprite, false);
    CHECK(quad.width / 640.0F == Approx(sprite.width / 1.595).epsilon(1e-3));
    CHECK(quad.height / 448.0F == Approx(1.0 / 1.1).epsilon(1e-3));
    CHECK((quad.x + (quad.width / 2)) == Approx(320.0));
    CHECK((quad.y + (quad.height / 2)) / 448.0F == Approx(0.5 - (0.3 / 1.1)).epsilon(1e-3));
}

TEST_CASE("the room smoke spans the 16:9 picture through the 16:9 overlay camera", "[room_smoke]") {
    // Only the camera changes: x = 0.5 + X / 2.017 and y = 0.5 - Y / 1.21 of the whole picture.
    const SmokeSprite sprite = narrowSprite();
    const LogicalQuad quad = roomSmokeQuad(sprite, true);
    CHECK(quad.width / 640.0F == Approx(sprite.width / 2.017).epsilon(1e-3));
    CHECK(quad.height / 448.0F == Approx(1.0 / 1.21).epsilon(1e-3));
    CHECK((quad.y + (quad.height / 2)) / 448.0F == Approx(0.5 - (0.3 / 1.21)).epsilon(1e-3));
    // The same texture rectangle and tint: one repeat across, shifted a tenth up.
    CHECK(quad.uv.u0 == Approx(0.25));
    CHECK(quad.uv.u1 == Approx(1.25));
    CHECK(quad.uv.v0 == Approx(-0.1));
    CHECK(quad.uv.v1 == Approx(0.9));
    CHECK(quad.colour.a == 40);

    // Over a 1280 x 720 window's whole view, the sprite is 1.05 of its width at the narrowest drift: 2.5 % past each
    // side, so its edges never show.
    const LogicalRect onWindow =
        logicalToWindow(LogicalRect{quad.x, quad.y, quad.width, quad.height}, ScreenRect{0, 0, 1280, 720});
    CHECK(onWindow.width / 1280.0F == Approx(1.0506).epsilon(2e-3));
    CHECK(onWindow.x < 0.0F);
    CHECK(onWindow.x + onWindow.width > 1280.0F);
    // Its top is above the window's top.
    CHECK(onWindow.y < 0.0F);
}
