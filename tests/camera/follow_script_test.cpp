// SPDX-License-Identifier: GPL-3.0-or-later
// The follow camera's script calls and its combat camera (docs/research/camera.md#script-calls,
// docs/research/camera.md#combat-camera): CfgFollowCamera's band at the minimum, CamSetFollowZoom's presets,
// CamSetFollowAngle's clamp, CameraReset behind the player, the activation, the combat camera's band, pitch and
// framing, keep-in-view and the stick switch.
#include <cmath>
#include <cstdint>
#include <numbers>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "animation/anim_math.h"
#include "camera/follow_camera.h"

using Catch::Approx;
using coney::anim::Vec3;
using coney::camera::FollowCamera;
using coney::camera::FollowSettings;
using coney::camera::FollowTarget;
using coney::camera::FollowZoom;

namespace {

constexpr float kDegree = std::numbers::pi_v<float> / 180.0F;
constexpr float kStep = 1.0F / 30.0F;
constexpr std::uint8_t kRest = 128;
constexpr Vec3 kFeet{40.0F, 40.0F, 0.0F};

// The camera's elevation above its look-at point, radians.
float pitchOf(const FollowCamera& camera) {
    const Vec3 offset = coney::anim::subtract(camera.position(), camera.lookAt());
    return std::atan2(offset.z, std::hypot(offset.x, offset.y));
}

// The heading (0 facing +y, anticlockwise) of a direction across the ground.
float headingOf(Vec3 direction) { return std::atan2(-direction.x, direction.y); }

// The angle at the look-at point from the camera's view across the ground to `point`, radians, anticlockwise.
float angleAtLookAt(const FollowCamera& camera, Vec3 point) {
    const float view = headingOf(coney::anim::subtract(camera.lookAt(), camera.position()));
    const float to = headingOf(coney::anim::subtract(point, camera.lookAt()));
    return std::remainder(to - view, 2.0F * std::numbers::pi_v<float>);
}

// CfgFollowCamera's arguments in level99 (global.lua's CameraCreateFollow).
FollowSettings level99() {
    FollowSettings settings;
    settings.minDistance = 3.0F;
    settings.maxDistance = 6.6F;
    settings.defaultDistance = 4.8F;
    settings.pitchDegrees = 13.0F;
    settings.fieldOfView = 65.0F;
    settings.nearClip = 0.1F;
    settings.lookAtHeight = 1.4F;
    return settings;
}

// A target standing at kFeet facing +y.
FollowTarget standing() {
    FollowTarget target;
    target.feet = kFeet;
    return target;
}

// A standing target holding L1 at an enemy at `enemy`.
FollowTarget lockedOn(Vec3 enemy) {
    FollowTarget target = standing();
    target.lockOn = true;
    target.lockHeld = true;
    target.enemy = enemy;
    return target;
}

} // namespace

TEST_CASE("CfgFollowCamera leaves the band at the minimum, 3.0-3.5 m, with the zoom step at the default", "[camera]") {
    FollowCamera camera(kFeet, 0.0F);
    camera.configure(level99());
    CHECK(camera.bandNear() == Approx(3.0F));
    CHECK(camera.bandFar() == Approx(3.5F));
    CHECK(camera.zoomDistance() == Approx(4.8F));
    CHECK(camera.upperPitch() == Approx(30.0F * kDegree));
    CHECK(camera.targetPitch() == Approx(13.0F * kDegree));
    CHECK(camera.lowerPitch() == Approx(std::atan(-0.4F / 3.5F)));
    // The view is turned to the pitch at once.
    CHECK(pitchOf(camera) == Approx(13.0F * kDegree).margin(1e-4));
    CHECK(camera.fieldOfView() == Approx(65.0F));
    CHECK(camera.nearClip() == Approx(0.1F));
}

TEST_CASE("CamSetFollowZoom moves the band to its preset without moving the camera; the leash then drags it",
          "[camera]") {
    FollowCamera camera(kFeet, 0.0F);
    camera.configure(level99());
    const Vec3 before = camera.position();
    camera.setZoom(FollowZoom::Default);
    CHECK(camera.bandNear() == Approx(4.8F));
    CHECK(camera.bandFar() == Approx(5.3F));
    CHECK(camera.zoomDistance() == Approx(6.6F));
    CHECK(camera.upperPitch() == Approx(40.0F * kDegree));
    CHECK(camera.lowerPitch() == Approx(std::atan(-0.4F / 5.3F)));
    CHECK(camera.position() == before);
    camera.setZoom(FollowZoom::Far);
    CHECK(camera.bandNear() == Approx(6.1F));
    CHECK(camera.bandFar() == Approx(6.6F));
    CHECK(camera.zoomDistance() == Approx(3.0F));
    CHECK(camera.upperPitch() == Approx(50.0F * kDegree));
    camera.setZoom(FollowZoom::Close);
    CHECK(camera.bandNear() == Approx(3.0F));
    // The camera stood 4.8 m away: the leash brings the wanted position to the new band's far edge.
    camera.update(standing(), kRest, kRest, nullptr, kStep);
    CHECK(coney::anim::distance(camera.wanted(), camera.lookAt()) == Approx(3.5F).margin(1e-3));
}

TEST_CASE("CamSetFollowAngle clamps to the pitch limits and turns the view at once", "[camera]") {
    FollowCamera camera(kFeet, 0.0F);
    camera.configure(level99());
    camera.setZoom(FollowZoom::Default);
    camera.setPitch(-10.0F);
    // -10° is below the lower limit atan(-0.4 / 5.3) = -4.3°: the camera looks up from there.
    CHECK(camera.targetPitch() == Approx(std::atan(-0.4F / 5.3F)));
    CHECK(camera.targetPitch() / kDegree == Approx(-4.32F).margin(0.01));
    CHECK(pitchOf(camera) == Approx(camera.targetPitch()).margin(1e-4));
    CHECK(camera.position() == camera.wanted());
    camera.setPitch(70.0F);
    CHECK(camera.targetPitch() == Approx(40.0F * kDegree));
    // The pitch stays until the next reset, which puts the configured 13° back.
    for (int i = 0; i < 10; ++i) {
        camera.update(standing(), kRest, kRest, nullptr, kStep);
    }
    CHECK(camera.targetPitch() == Approx(40.0F * kDegree));
    camera.reset();
    CHECK(camera.targetPitch() == Approx(13.0F * kDegree));
}

TEST_CASE("CameraReset puts the camera behind the player at the nearest preset, the band left as it is", "[camera]") {
    FollowCamera camera(kFeet, 0.0F);
    camera.configure(level99());
    // The player turned to face +x (heading -90°) and walked off: the reset swings the camera behind him.
    FollowTarget target = standing();
    target.feet = Vec3{45.0F, 40.0F, 0.0F};
    target.heading = -90.0F * kDegree;
    camera.observe(target);
    camera.reset();
    CHECK(camera.bandNear() == Approx(3.0F));
    CHECK(camera.lookAt() == Vec3{45.0F, 40.0F, 1.4F});
    CHECK(coney::anim::distance(camera.position(), camera.lookAt()) == Approx(3.0F));
    CHECK(camera.position().x < 45.0F);
    CHECK(camera.position().y == Approx(40.0F).margin(1e-4));
    CHECK(pitchOf(camera) == Approx(13.0F * kDegree).margin(1e-4));
    CHECK(camera.zoomDistance() == Approx(4.8F));
    // In the 4.8-5.3 m band the preset is the default distance.
    camera.setZoom(FollowZoom::Default);
    camera.reset();
    CHECK(coney::anim::distance(camera.position(), camera.lookAt()) == Approx(4.8F));
    CHECK(camera.zoomDistance() == Approx(6.6F));
}

TEST_CASE("the follow camera made current keeps its direction at its distance clamped to the band", "[camera]") {
    FollowCamera camera(kFeet, 0.0F);
    camera.configure(level99());
    // The camera stood 4.8 m south of the player; the band is now 3.0-3.5 m.
    camera.activate();
    const Vec3 offset = coney::anim::subtract(camera.position(), camera.lookAt());
    CHECK(coney::anim::length(offset) == Approx(3.5F));
    CHECK(offset.x == Approx(0.0F).margin(1e-4));
    CHECK(offset.y < 0.0F);
    CHECK(camera.position() == camera.wanted());
    CHECK_FALSE(camera.wantedNear().has_value());
}

TEST_CASE("the combat camera eases the band to 2.4 m at 4.5/s, pitches to 15 degrees and frames the enemy at 27",
          "[camera]") {
    FollowCamera camera(kFeet, 0.0F);
    const Vec3 enemy{40.0F, 43.0F, 0.0F};
    // The entry update saves the band and sets the goal; the ease starts on the next (4.8, 4.44, 4.134, ...).
    camera.update(lockedOn(enemy), kRest, kRest, nullptr, kStep);
    CHECK(camera.combatOn());
    CHECK(camera.bandNear() == Approx(4.8F));
    CHECK(camera.targetPitch() == Approx(15.0F * kDegree));
    camera.update(lockedOn(enemy), kRest, kRest, nullptr, kStep);
    CHECK(camera.bandNear() == Approx(4.44F).margin(1e-3));
    camera.update(lockedOn(enemy), kRest, kRest, nullptr, kStep);
    CHECK(camera.bandNear() == Approx(4.134F).margin(1e-3));
    for (int i = 0; i < 60; ++i) {
        camera.update(lockedOn(enemy), kRest, kRest, nullptr, kStep);
    }
    CHECK(camera.bandNear() == Approx(2.4F).margin(1e-3));
    CHECK(camera.zoomDistance() == Approx(4.8F)); // below 3.72 m: the default, 30°
    // The enemy straight ahead was swung out to between 25° and 29° off the view, and held there.
    const float angle = std::abs(angleAtLookAt(camera, enemy)) / kDegree;
    CHECK(angle >= 25.0F);
    CHECK(angle <= 29.0F);
    // Released: the saved band comes back the same way; the pitch stays 15°.
    camera.update(standing(), kRest, kRest, nullptr, kStep);
    CHECK_FALSE(camera.combatOn());
    CHECK(camera.wantedNear().value_or(0.0F) == Approx(4.8F));
    camera.update(standing(), kRest, kRest, nullptr, kStep);
    CHECK(camera.bandNear() == Approx(2.76F).margin(1e-3));
    for (int i = 0; i < 150; ++i) {
        camera.update(standing(), kRest, kRest, nullptr, kStep);
    }
    CHECK(camera.bandNear() == Approx(4.8F));
    CHECK(camera.targetPitch() == Approx(15.0F * kDegree));
}

TEST_CASE("the combat camera's framing turns 0.455 of the way to 27 degrees, at most 640 deg/s", "[camera]") {
    FollowCamera camera(kFeet, 0.0F);
    // An enemy 60° to the left of the view at the look-at point: (60 - 27) × 0.455 = 15°, under the 21.3° an update
    // that 640°/s allows, so the whole share.
    const float a = 60.0F * kDegree;
    const Vec3 enemy{40.0F - 3.0F * std::sin(a), 40.0F + 3.0F * std::cos(a), 0.0F};
    camera.update(lockedOn(enemy), kRest, kRest, nullptr, kStep);
    CHECK(camera.lastFrameTurn() / kDegree == Approx(15.015F).margin(0.05));
    CHECK(camera.lastAutoTurn() == 0.0F);
    // Behind the player (170°): (170 - 27) × 0.455 = 65°, capped at 640°/s × 1/30 s = 21.3°.
    FollowCamera behind(kFeet, 0.0F);
    const float b = 170.0F * kDegree;
    const Vec3 back{40.0F - 3.0F * std::sin(b), 40.0F + 3.0F * std::cos(b), 0.0F};
    behind.update(lockedOn(back), kRest, kRest, nullptr, kStep);
    CHECK(behind.lastFrameTurn() == Approx(FollowCamera::kCombatFrameRate * kStep));
}

TEST_CASE("a watched human is kept within a quarter of the field of view, 35% of the excess an update", "[camera]") {
    FollowCamera camera(kFeet, 0.0F);
    FollowTarget target = standing();
    // 90° to the left of the camera's view: (90 - 16.25) × 0.35 = 25.8°, capped at 270°/s × 1/30 s = 9°.
    target.secondary = coney::anim::add(camera.position(), Vec3{-10.0F, 0.0F, 0.0F});
    camera.update(target, kRest, kRest, nullptr, kStep);
    CHECK(camera.lastFrameTurn() == Approx(FollowCamera::kKeepInViewRate * kStep));
    // 20° to the right: (20 - 16.25) × 0.35 = 1.31° the other way.
    FollowCamera right(kFeet, 0.0F);
    const float a = -20.0F * kDegree;
    target.secondary = coney::anim::add(right.position(), Vec3{-10.0F * std::sin(a), 10.0F * std::cos(a), 0.0F});
    right.update(target, kRest, kRest, nullptr, kStep);
    CHECK(right.lastFrameTurn() / kDegree == Approx(-1.3125F).margin(0.01));
    // Within the quarter, nothing; and it replaces auto-follow, so a running player is not followed.
    FollowCamera running(kFeet, 0.0F);
    target.gait = coney::camera::kGaitRun;
    target.heading = 90.0F * kDegree;
    target.secondary = coney::anim::add(running.position(), Vec3{0.0F, 10.0F, 0.0F});
    running.update(target, kRest, kRest, nullptr, kStep);
    CHECK(running.lastFrameTurn() == 0.0F);
    CHECK(running.lastAutoTurn() == 0.0F);
}

TEST_CASE("CamEnable(0, off) stops the right stick turning the camera", "[camera]") {
    FollowCamera camera(kFeet, 0.0F);
    const Vec3 before = camera.wanted();
    camera.enableStick(false);
    camera.update(standing(), 255, kRest, nullptr, kStep);
    CHECK(camera.wanted().x == Approx(before.x).margin(1e-4));
    CHECK(camera.inputHold() == 0.0F);
    camera.enableStick(true);
    camera.update(standing(), 255, kRest, nullptr, kStep);
    CHECK(camera.inputHold() > 0.0F);
}
