// SPDX-License-Identifier: GPL-3.0-or-later
// Player 1's cameras as the pads update reads them to turn the left stick (docs/research/input.md#turned-stick), and
// the follow camera's flag (+0x473) that a stick at rest sets.
#include "camera/stick_turn_cameras.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "animation/anim_math.h"
#include "camera/cameras.h"
#include "camera/follow_camera.h"
#include "camera/locked_camera.h"
#include "core/pad.h"
#include "core/pads.h"

using Catch::Approx;
using coney::Pads;
using coney::PortSamples;
using coney::anim::Vec3;
using coney::camera::Cameras;
using coney::camera::FollowCamera;
using coney::camera::FollowTarget;
using coney::camera::LockedCamera;
using coney::camera::PlayerStickCameras;

namespace {

constexpr Vec3 kFeet{40.0F, 40.0F, 0.0F};
constexpr double kFollowHandle = 7.0;
constexpr double kLockedHandle = 8.0;
constexpr std::uint8_t kRest = 128;

// A locked camera east of the player looking west.
LockedCamera lookingWest() {
    return LockedCamera{.position = Vec3{50.0F, 40.0F, 3.0F},
                        .headingDegrees = 90.0F,
                        .pitchDegrees = -10.0F,
                        .rollDegrees = 0.0F,
                        .fieldOfView = 50.0F,
                        .nearClip = 0.1F,
                        .farClip = 100.0F,
                        .keptInView = {}};
}

// Player 1's follow camera behind him under the manager, updated once so the view is the follow camera's.
struct Rig {
    FollowCamera follow{kFeet, 0.0F};
    Cameras cameras;
    FollowTarget target;

    Rig() {
        target.feet = kFeet;
        cameras.attachFollow(&follow);
        static_cast<void>(cameras.setupFollow(kFollowHandle));
        cameras.createLocked(kLockedHandle, lookingWest());
        cameras.update(target, kRest, kRest, nullptr, 1.0F / 30.0F);
    }
};

// Port 1's left stick at raw bytes (x, y), port 2 at rest.
PortSamples leftStick(std::uint8_t x, std::uint8_t y) {
    PortSamples samples;
    for (coney::PadSample& sample : samples) {
        sample.connected = true;
    }
    samples[0].sticks[2] = x;
    samples[0].sticks[3] = y;
    return samples;
}

} // namespace

TEST_CASE("a follow camera is made taking its orientation afresh for the stick", "[camera]") {
    // Expected: input.md#turned-stick, confirmed (runtime)
    // Cam_Follow_Construct writes 1 to +0x473; run r-20261115-pads-stick-frame read 1 in play.
    const FollowCamera follow{kFeet, 0.0F};
    CHECK(follow.takesStickFrameAfresh());
}

TEST_CASE("player 1's stick cameras are record 0's: his current view and his follow camera", "[camera]") {
    // Expected: input.md#turned-stick, confirmed (code)
    Rig rig;
    PlayerStickCameras sticks(rig.follow, &rig.cameras);
    CHECK(sticks.playerOfRecord(0) == std::optional<std::size_t>(0));
    CHECK_FALSE(sticks.playerOfRecord(4).has_value());
    CHECK_FALSE(sticks.viewsDifferFromPlayers());
    CHECK_FALSE(sticks.playerCamera(1).has_value());

    const auto camera = sticks.playerCamera(0);
    REQUIRE(camera.has_value());
    // NOLINTBEGIN(bugprone-unchecked-optional-access): the REQUIRE above ends the test when empty, which clang-tidy
    // does not know.
    const coney::anim::Quat view = rig.cameras.view().orientation;
    CHECK(camera->orientation == std::array<float, 4>{view.x, view.y, view.z, view.w});
    CHECK(camera->slot == 0);
    // NOLINTEND(bugprone-unchecked-optional-access)

    auto follow = sticks.follow(0);
    REQUIRE(follow.has_value());
    // NOLINTBEGIN(bugprone-unchecked-optional-access): the REQUIRE above ends the test when empty, which clang-tidy
    // does not know.
    CHECK(follow->isPlayerCamera);
    CHECK(follow->takesFrameAfresh);
    CHECK_FALSE(follow->lookingBehind);
    // NOLINTEND(bugprone-unchecked-optional-access)

    // A locked camera made current: the follow camera is no longer his player camera.
    rig.cameras.makeActive(kLockedHandle, 0.0F);
    follow = sticks.follow(0);
    REQUIRE(follow.has_value());
    // NOLINTBEGIN(bugprone-unchecked-optional-access): the REQUIRE above ends the test when empty, which clang-tidy
    // does not know.
    CHECK_FALSE(follow->isPlayerCamera);
    // NOLINTEND(bugprone-unchecked-optional-access)
}

TEST_CASE("a stick at rest sets the follow camera's flag again after it was cleared", "[camera]") {
    // Expected: input.md#turned-stick, confirmed (runtime)
    // Run r-20261115-pads-stick-frame: +0x473 written 0 with the stick held read 1 once the stick was let go.
    Rig rig;
    rig.follow.setStickFrameAfresh(false);
    PlayerStickCameras sticks(rig.follow, &rig.cameras);
    Pads pads;

    pads.update(leftStick(217, 57));
    pads.turnSticks(sticks);
    CHECK_FALSE(rig.follow.takesStickFrameAfresh());

    pads.update(leftStick(kRest, kRest));
    pads.turnSticks(sticks);
    CHECK(rig.follow.takesStickFrameAfresh());
}

TEST_CASE("player 1's stick is turned by his follow camera's view: up walks away from the camera", "[camera]") {
    // Expected: input.md#turned-stick, confirmed (code)
    // The follow camera behind a player facing +y looks along +y, so the stick's up turns to the world's +y.
    Rig rig;
    PlayerStickCameras sticks(rig.follow, &rig.cameras);
    Pads pads;
    pads.update(leftStick(kRest, 57)); // 0.4 up
    pads.turnSticks(sticks);
    CHECK(pads.port(0).turnedX() == Approx(0.0F).margin(1e-3));
    CHECK(pads.port(0).turnedY() == Approx(0.4F).margin(1e-3));
}
