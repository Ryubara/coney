// SPDX-License-Identifier: GPL-3.0-or-later
// The Rumble win camera (docs/research/rumble.md#win-camera) and the camera bindings a match's end uses through the
// manager: CameraCreateWin's start and orbit, CamDelete and CamSetFollowHeading.
#include "camera/win_camera.h"

#include <cmath>
#include <optional>
#include <utility>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "animation/anim_math.h"
#include "camera/cameras.h"
#include "camera/follow_camera.h"
#include "camera/locked_camera.h"

using Catch::Approx;
using coney::anim::Vec3;
using coney::camera::CameraKind;
using coney::camera::Cameras;
using coney::camera::FollowCamera;
using coney::camera::FollowTarget;
using coney::camera::LockedCamera;
using coney::camera::WinCamera;
using coney::camera::WinCameraSettings;

namespace {

constexpr Vec3 kFeet{40.0F, 40.0F, 0.0F};
constexpr double kFollowHandle = 7.0;
constexpr double kLockedHandle = 8.0;
constexpr double kWinHandle = 9.0;
constexpr double kWinner = 5.0;

// Whether two vectors are the same within `margin` on every axis.
bool near(Vec3 a, Vec3 b, float margin = 1e-3F) {
    return std::abs(a.x - b.x) <= margin && std::abs(a.y - b.y) <= margin && std::abs(a.z - b.z) <= margin;
}

// Settings 3 m out and 1 m up, not turned, a quarter turn a second.
WinCameraSettings quarterTurn() {
    return WinCameraSettings{.fieldOfView = 58.0F,
                             .distance = 3.0F,
                             .angleDegrees = 0.0F,
                             .speedDegrees = 90.0F,
                             .height = 1.0F,
                             .farClip = 200.0F,
                             .direction = 1.0F};
}

// The manager over a follow camera on a player at kFeet, with a locked camera and a placer that finds the winner at
// kFeet facing +y.
struct Rig {
    FollowCamera follow{kFeet, 0.0F};
    Cameras cameras;
    FollowTarget target;

    Rig() {
        target.feet = kFeet;
        cameras.attachFollow(&follow);
        CHECK(cameras.setupFollow(kFollowHandle) == kFollowHandle);
        cameras.createLocked(kLockedHandle, LockedCamera{.position = Vec3{50.0F, 40.0F, 3.0F},
                                                         .headingDegrees = 90.0F,
                                                         .pitchDegrees = 10.0F,
                                                         .rollDegrees = 0.0F,
                                                         .fieldOfView = 50.0F,
                                                         .nearClip = 0.1F,
                                                         .farClip = 200.0F});
        cameras.setPlacer([](double handle) -> std::optional<std::pair<Vec3, float>> {
            if (handle != kWinner) {
                return std::nullopt;
            }
            return std::pair{kFeet, 0.0F};
        });
    }
    // One update of `seconds` with the stick at rest.
    void step(float seconds = 1.0F / 30.0F) { cameras.update(target, 128, 128, nullptr, seconds); }
};

} // namespace

TEST_CASE("the win camera starts along the target's facing turned by its angle, above his feet", "[camera][rumble]") {
    const WinCamera ahead(Vec3{0.0F, 0.0F, 0.0F}, 0.0F, quarterTurn());
    CHECK(near(ahead.lookAt(), Vec3{0.0F, 0.0F, 1.0F}));
    CHECK(near(ahead.position(), Vec3{0.0F, 3.0F, 1.0F}));

    WinCameraSettings turned = quarterTurn();
    turned.angleDegrees = 90.0F;
    const WinCamera side(Vec3{0.0F, 0.0F, 0.0F}, 0.0F, turned);
    CHECK(near(side.position(), Vec3{-3.0F, 0.0F, 1.0F}));
    CHECK(side.view().farClip == Approx(WinCamera::kMaxFarClip));
    CHECK(side.view().nearClip == Approx(WinCamera::kNearClip));
}

TEST_CASE("the win camera orbits its look-at point by direction x speed x time until stopped", "[camera][rumble]") {
    WinCamera camera(Vec3{0.0F, 0.0F, 0.0F}, 0.0F, quarterTurn());
    camera.update(1.0F);
    CHECK(near(camera.position(), Vec3{-3.0F, 0.0F, 1.0F}));

    WinCameraSettings backwards = quarterTurn();
    backwards.direction = -1.0F;
    WinCamera other(Vec3{0.0F, 0.0F, 0.0F}, 0.0F, backwards);
    other.update(1.0F);
    CHECK(near(other.position(), Vec3{3.0F, 0.0F, 1.0F}));

    other.setOrbiting(false);
    other.update(1.0F);
    CHECK(near(other.position(), Vec3{3.0F, 0.0F, 1.0F}));
}

TEST_CASE("CameraCreateWin keeps one handle; made active it shows the orbit around the winner", "[camera][rumble]") {
    Rig rig;
    CHECK(rig.cameras.createWin(kWinHandle, kWinner, quarterTurn()) == kWinHandle);
    CHECK(rig.cameras.createWin(kWinHandle + 1, kWinner, quarterTurn()) == kWinHandle);
    REQUIRE(rig.cameras.win() != nullptr);
    CHECK(near(rig.cameras.win()->lookAt(), Vec3{kFeet.x, kFeet.y, 1.0F}));

    rig.cameras.makeActive(kWinHandle, 0.0F);
    CHECK(rig.cameras.current().kind == CameraKind::Win);
    const Vec3 before = rig.cameras.win()->position();
    rig.step(1.0F);
    CHECK_FALSE(near(rig.cameras.win()->position(), before));
    CHECK(near(rig.cameras.view().position, rig.cameras.win()->position()));
}

TEST_CASE("CamDelete forgets a locked camera, cutting to the follow camera, and keeps the shared ones",
          "[camera][rumble]") {
    Rig rig;
    rig.cameras.makeActive(kLockedHandle, 0.0F);
    REQUIRE(rig.cameras.current().kind == CameraKind::Locked);
    rig.cameras.deleteCamera(kLockedHandle);
    CHECK(rig.cameras.locked(kLockedHandle) == nullptr);
    CHECK(rig.cameras.current().kind == CameraKind::Follow);

    (void)rig.cameras.createWin(kWinHandle, kWinner, quarterTurn());
    rig.cameras.deleteCamera(kWinHandle);
    CHECK(rig.cameras.win() != nullptr);
}

TEST_CASE("CamSetFollowHeading swings the follow camera round its target once it has one", "[camera][rumble]") {
    Rig rig;
    const Vec3 start = rig.follow.position();
    rig.cameras.setFollowHeading(90.0F);
    CHECK(near(rig.follow.position(), start));

    rig.step();
    rig.cameras.setFollowHeading(90.0F);
    const Vec3 east = rig.follow.position();
    rig.cameras.setFollowHeading(-90.0F);
    const Vec3 west = rig.follow.position();
    CHECK_FALSE(near(east, west));
    const float eastOut = std::hypot(east.x - kFeet.x, east.y - kFeet.y);
    const float westOut = std::hypot(west.x - kFeet.x, west.y - kFeet.y);
    CHECK(eastOut == Approx(westOut).margin(1e-3));
}
