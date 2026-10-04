// SPDX-License-Identifier: GPL-3.0-or-later
#include "world/debug_camera.h"

#include <cmath>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "core/pad.h"

using Catch::Approx;
using coney::Pad;
using coney::PadSample;
using coney::world::DebugCamera;

namespace {

// A pad record after one connected sample holding `buttons` with the sticks at `sticks`.
Pad padWith(std::uint16_t buttons, std::array<std::uint8_t, 4> sticks = {0x80, 0x80, 0x80, 0x80}) {
    PadSample sample;
    sample.connected = true;
    sample.buttons = buttons;
    sample.sticks = sticks;
    Pad pad;
    pad.update(sample);
    return pad;
}

} // namespace

TEST_CASE("the debug camera's frame is right-handed with the screen's right to -x looking along +z", "[debug_camera]") {
    const coney::world::CameraPose pose = DebugCamera({1.0F, 2.0F, 3.0F}).pose();
    CHECK(pose.position.y == 2.0F);
    CHECK(pose.forward.z == Approx(1.0F));
    CHECK(pose.up.y == Approx(1.0F));
    CHECK(pose.right.x == Approx(-1.0F));
}

TEST_CASE("the left stick flies along the view and Cross makes it faster", "[debug_camera]") {
    // Left stick fully up (raw y 0) for one second.
    DebugCamera camera({0.0F, 0.0F, 0.0F});
    camera.update(padWith(0, {0x80, 0x80, 0x80, 0x00}), 1.0F);
    CHECK(camera.position().z == Approx(DebugCamera::kSpeed));

    DebugCamera fast({0.0F, 0.0F, 0.0F});
    fast.update(padWith(coney::pad::kCross, {0x80, 0x80, 0x80, 0x00}), 1.0F);
    CHECK(fast.position().z == Approx(DebugCamera::kSpeed * DebugCamera::kFastFactor));
}

TEST_CASE("R1 and L1 rise and sink; the d-pad turns and looks, pitch stopping short of straight up", "[debug_camera]") {
    DebugCamera camera({0.0F, 0.0F, 0.0F});
    camera.update(padWith(coney::pad::kR1), 0.5F);
    CHECK(camera.position().y == Approx(DebugCamera::kSpeed * 0.5F));
    camera.update(padWith(coney::pad::kL1), 0.5F);
    CHECK(camera.position().y == Approx(0.0F).margin(1e-5));

    // Right turns towards the pose's right: the yaw shrinks.
    camera.update(padWith(coney::pad::kRight), 0.25F);
    CHECK(camera.yaw() == Approx(-DebugCamera::kTurnRate * 0.25F));
    for (int i = 0; i < 10; ++i) {
        camera.update(padWith(coney::pad::kUp), 1.0F);
    }
    CHECK(camera.pitch() == Approx(DebugCamera::kMaxPitch));
}
