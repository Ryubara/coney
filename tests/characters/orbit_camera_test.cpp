// SPDX-License-Identifier: GPL-3.0-or-later
#include "characters/orbit_camera.h"

#include <cmath>
#include <cstdint>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "core/pad.h"

using Catch::Approx;
using coney::Pad;
using coney::PadSample;
using coney::characters::OrbitCamera;

namespace {

// A pad that has taken one sample: `buttons` held and the sticks at raw bytes (128 is the centre, 0 left or up).
Pad padWith(std::uint16_t buttons, std::uint8_t rightX, std::uint8_t rightY, std::uint8_t leftX, std::uint8_t leftY) {
    PadSample sample;
    sample.connected = true;
    sample.buttons = buttons;
    sample.sticks = {rightX, rightY, leftX, leftY};
    Pad pad;
    pad.update(sample);
    return pad;
}

constexpr std::uint8_t kCentre = coney::pad::kStickCentre;

} // namespace

TEST_CASE("the orbit camera turns in proportion to the right stick and the d-pad turns it at full rate",
          "[orbit_camera]") {
    // A partial push right (raw 205 of 255) for one second turns by that share of the full rate.
    OrbitCamera camera({0, 0, 1}, 3.0F, 0.0F, 0.0F);
    const Pad partial = padWith(0, 205, kCentre, kCentre, kCentre);
    for (int i = 0; i < 30; ++i) {
        camera.update(partial, 1.0F / 30.0F);
    }
    CHECK(camera.yaw() == Approx(partial.rightX() * OrbitCamera::kTurnRate).margin(1e-4));
    CHECK(partial.rightX() > 0.3F);
    CHECK(partial.rightX() < 0.8F);

    // The d-pad's up raises the camera at the full rate, and the pitch stops short of straight up.
    OrbitCamera raised({0, 0, 1}, 3.0F, 0.0F, 0.0F);
    raised.update(padWith(coney::pad::kUp, kCentre, kCentre, kCentre, kCentre), 0.25F);
    CHECK(raised.pitch() == Approx(0.25F * OrbitCamera::kTurnRate));
    raised.update(padWith(coney::pad::kUp, kCentre, kCentre, kCentre, kCentre), 10.0F);
    CHECK(raised.pitch() == OrbitCamera::kMaxPitch);
}

TEST_CASE("the orbit camera moves in and out by the left stick or R1 and L1, within its limits", "[orbit_camera]") {
    OrbitCamera camera({0, 0, 1}, 4.0F, 0.0F, 0.0F);
    // Left stick half up (raw 64) for one second: the distance shrinks by e^(0.5 × rate).
    const Pad half = padWith(0, kCentre, kCentre, kCentre, 64);
    camera.update(half, 1.0F);
    CHECK(camera.distance() == Approx(4.0F * std::exp(-half.leftY() * OrbitCamera::kZoomRate)));
    camera.update(padWith(coney::pad::kL1, kCentre, kCentre, kCentre, kCentre), 100.0F);
    CHECK(camera.distance() == OrbitCamera::kMaxDistance);
    camera.update(padWith(coney::pad::kR1, kCentre, kCentre, kCentre, kCentre), 100.0F);
    CHECK(camera.distance() == OrbitCamera::kMinDistance);
}

TEST_CASE("the orbit camera looks at its target from its distance, with z up", "[orbit_camera]") {
    const OrbitCamera camera({1, 2, 1}, 2.0F, 1.5707963F, 0.3F);
    const coney::characters::OrbitPose pose = camera.pose();
    const coney::anim::Vec3 toTarget = coney::anim::subtract(camera.target(), pose.position);
    CHECK(coney::anim::length(toTarget) == Approx(2.0F));
    CHECK(coney::anim::dot(coney::anim::normalise(toTarget), pose.forward) == Approx(1.0F));
    CHECK(coney::anim::dot(pose.forward, pose.up) == Approx(0.0F).margin(1e-6));
    CHECK(pose.up.z > 0.9F);
    CHECK(pose.position.y > camera.target().y); // yaw 90 degrees puts it on the +y side
    // `right` is forward × up: looking along -y with z up, the screen's right is -x.
    CHECK(pose.right.x == Approx(-1.0F).margin(1e-5));
}
