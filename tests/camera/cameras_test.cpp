// SPDX-License-Identifier: GPL-3.0-or-later
// The cameras' manager and the cameras it switches between (docs/research/camera.md#blends,
// docs/research/camera.md#locked-cameras, docs/research/camera.md#scenes, docs/research/camera.md#shake,
// docs/research/camera.md#slow-motion): locked views, cuts and linear blends with a slerped orientation, the scene
// stack, the switches, the target list, the shake and its rumble, and slow motion.
#include "camera/cameras.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <map>
#include <numbers>
#include <optional>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "animation/anim_math.h"
#include "camera/camera_blend.h"
#include "camera/camera_shake.h"
#include "camera/camera_view.h"
#include "camera/follow_camera.h"
#include "camera/locked_camera.h"
#include "camera/slow_motion.h"

using Catch::Approx;
using coney::anim::Vec3;
using coney::camera::CameraBlend;
using coney::camera::CameraKind;
using coney::camera::Cameras;
using coney::camera::CameraShake;
using coney::camera::CameraView;
using coney::camera::FollowCamera;
using coney::camera::FollowTarget;
using coney::camera::LockedCamera;
using coney::camera::SlowMotion;

namespace {

constexpr float kDegree = std::numbers::pi_v<float> / 180.0F;
constexpr float kStep = 1.0F / 30.0F;
constexpr std::uint8_t kRest = 128;
constexpr Vec3 kFeet{40.0F, 40.0F, 0.0F};
constexpr double kFollowHandle = 7.0;
constexpr double kLockedHandle = 8.0;

// A locked camera east of the player looking west, as the tutorial's cut-aways are made.
LockedCamera cutAway() {
    return LockedCamera{.position = Vec3{50.0F, 40.0F, 3.0F},
                        .headingDegrees = 90.0F,
                        .pitchDegrees = 10.0F,
                        .rollDegrees = 0.0F,
                        .fieldOfView = 50.0F,
                        .nearClip = 0.1F,
                        .farClip = 200.0F};
}

// Whether two vectors are the same within `margin` on every axis.
bool near(Vec3 a, Vec3 b, float margin = 1e-4F) {
    return std::abs(a.x - b.x) <= margin && std::abs(a.y - b.y) <= margin && std::abs(a.z - b.z) <= margin;
}

// The manager over a follow camera on a player standing at kFeet, with the follow camera set up and one locked camera.
struct Rig {
    FollowCamera follow{kFeet, 0.0F};
    Cameras cameras;
    FollowTarget target;

    Rig() {
        target.feet = kFeet;
        cameras.attachFollow(&follow);
        CHECK(cameras.setupFollow(kFollowHandle) == kFollowHandle);
        cameras.createLocked(kLockedHandle, cutAway());
    }
    // One update with the stick at rest.
    void step(float seconds = kStep) { cameras.update(target, kRest, kRest, nullptr, seconds); }
};

} // namespace

TEST_CASE("a locked camera looks along its heading and pitch at a point 3 m ahead, its far clip at most 150",
          "[camera]") {
    const CameraView view = cutAway().view();
    const Vec3 forward = coney::camera::viewForward(view);
    CHECK(near(forward, Vec3{-std::cos(10.0F * kDegree), 0.0F, -std::sin(10.0F * kDegree)}));
    CHECK(near(view.lookAt, coney::anim::add(view.position, coney::anim::scale(forward, 3.0F))));
    CHECK(coney::camera::viewUp(view).z > 0.9F);
    CHECK(view.fieldOfView == 50.0F);
    CHECK(view.farClip == 150.0F);
    // A roll turns the top to the right.
    LockedCamera rolled = cutAway();
    rolled.pitchDegrees = 0.0F;
    rolled.rollDegrees = 90.0F;
    CHECK(near(coney::camera::viewUp(rolled.view()), Vec3{0.0F, 1.0F, 0.0F}));
}

TEST_CASE("a blend lerps the points and slerps the orientation linearly in time; the far clip never grows",
          "[camera]") {
    const CameraView from = coney::camera::viewLookingAt(Vec3{0, 0, 0}, Vec3{0, 10, 0}, 50.0F, 0.1F, 150.0F);
    const CameraView to = coney::camera::viewLookingAt(Vec3{10, 0, 0}, Vec3{0, 0, 0}, 65.0F, 0.1F, 100.0F);
    CameraBlend blend(from, 1.0F);
    const CameraView quarter = blend.step(to, 0.25F);
    CHECK(near(quarter.position, Vec3{2.5F, 0.0F, 0.0F}));
    CHECK(near(quarter.lookAt, Vec3{0.0F, 7.5F, 0.0F}));
    // From facing +y to facing -x is 90° anticlockwise: a quarter of it is 22.5°.
    const Vec3 forward = coney::camera::viewForward(quarter);
    CHECK(std::atan2(-forward.x, forward.y) / kDegree == Approx(22.5F).margin(1e-3));
    CHECK(quarter.fieldOfView == 65.0F);
    CHECK(quarter.farClip == Approx(137.5F));
    CHECK_FALSE(blend.done());
    const CameraView end = blend.step(to, 0.75F);
    CHECK(blend.done());
    CHECK(near(end.position, to.position));
    CHECK(end.farClip == Approx(100.0F));
    // Toward a longer far clip it stays where it started.
    CameraBlend out(to, 1.0F);
    CHECK(out.step(from, 0.5F).farClip == Approx(100.0F));
}

TEST_CASE("CameraMakeActive with 0 s cuts; with 1 s it blends from the view shown to the live follow camera",
          "[camera]") {
    Rig rig;
    CHECK(rig.cameras.current().kind == CameraKind::Follow);
    rig.cameras.makeActive(kLockedHandle, 0.0F);
    CHECK(rig.cameras.current().kind == CameraKind::Locked);
    rig.step();
    const CameraView locked = cutAway().view();
    CHECK(near(rig.cameras.view().position, locked.position));
    // Back to the follow camera, reset behind the player, over 1 s: half way after 15 updates.
    rig.cameras.reset(kFollowHandle);
    rig.cameras.makeActive(kFollowHandle, 1.0F);
    CHECK(rig.cameras.blending());
    for (int i = 0; i < 15; ++i) {
        rig.step();
    }
    const Vec3 halfway = coney::anim::lerp(locked.position, rig.follow.position(), 0.5F);
    CHECK(near(rig.cameras.view().position, halfway, 1e-3F));
    CHECK(rig.cameras.view().fieldOfView == Approx(65.0F));
    // The time is up on the 30th update (or the 31st, as the sum of thirtieths rounds).
    int more = 0;
    while (rig.cameras.blending() && more < 20) {
        rig.step();
        ++more;
    }
    CHECK(more >= 15);
    CHECK(more <= 16);
    CHECK(rig.cameras.current().kind == CameraKind::Follow);
    rig.step();
    CHECK(near(rig.cameras.view().position, rig.follow.position()));
    // A handle that names no camera changes nothing.
    rig.cameras.makeActive(99.0, 0.0F);
    CHECK(rig.cameras.current().kind == CameraKind::Follow);
}

TEST_CASE("a scene pushes the current camera; CameraMakeActive during it changes what it returns to", "[camera]") {
    Rig rig;
    const CameraView scene = coney::camera::viewLookingAt(Vec3{0, 0, 5}, Vec3{0, 10, 0}, 40.0F, 0.1F, 100.0F);
    rig.cameras.beginScene(scene);
    CHECK(rig.cameras.current().kind == CameraKind::Scene);
    REQUIRE(rig.cameras.stack().size() == 1);
    CHECK(rig.cameras.stack().back().kind == CameraKind::Follow);
    rig.step();
    CHECK(near(rig.cameras.view().position, scene.position));
    rig.cameras.makeActive(kLockedHandle, 1.0F);
    CHECK(rig.cameras.current().kind == CameraKind::Scene);
    CHECK(rig.cameras.stack().back().kind == CameraKind::Locked);
    // The end pops it and cuts (BlendCam 0), then settles the cameras once.
    rig.cameras.endScene(0.0F);
    CHECK(rig.cameras.stack().empty());
    CHECK(rig.cameras.current().kind == CameraKind::Locked);
    CHECK_FALSE(rig.cameras.blending());
    // A scene returning to the follow camera with a 1 s blend: the settling update counts 0.17 s of it.
    rig.cameras.makeActive(kFollowHandle, 0.0F);
    rig.cameras.beginScene(scene);
    rig.cameras.endScene(1.0F);
    CHECK(rig.cameras.current().kind == CameraKind::Follow);
    CHECK(rig.cameras.blending());
}

TEST_CASE("the switches start as a level's reset leaves them and reach the follow camera", "[camera]") {
    Rig rig;
    for (std::size_t i = 0; i < Cameras::kSwitches; ++i) {
        CHECK(rig.cameras.enabled(i) == (i != 1 && i != 13));
    }
    rig.cameras.enable(Cameras::kSwitchStick, false);
    CHECK_FALSE(rig.cameras.enabled(0));
    rig.cameras.update(rig.target, 255, kRest, nullptr, kStep);
    CHECK(rig.follow.inputHold() == 0.0F);
    rig.cameras.enable(99, false); // out of range: nothing
    CHECK_FALSE(rig.cameras.enabled(99));
}

TEST_CASE("CamTarget keeps a shared list of at most four humans", "[camera]") {
    Cameras cameras;
    for (int human = 1; human <= 4; ++human) {
        CHECK(cameras.target(0, static_cast<double>(human)));
    }
    CHECK_FALSE(cameras.target(0, 5.0));
    CHECK_FALSE(cameras.target(0, 0.0));
    CHECK(cameras.target(1, 2.0));
    CHECK(cameras.targets().size() == 3);
    CHECK(cameras.target(2, 0.0));
    CHECK(cameras.targets().empty());
    CHECK_FALSE(cameras.target(3, 1.0));
}

TEST_CASE("CamSetSecondary keeps a human in view through the locator; NilHandle ends it", "[camera]") {
    Rig rig;
    rig.cameras.setLocator([](double handle) -> std::optional<Vec3> {
        return handle == 3.0 ? std::optional<Vec3>(Vec3{20.0F, 34.0F, 0.0F}) : std::nullopt;
    });
    rig.cameras.setSecondary(3.0, 0.0F);
    REQUIRE(rig.cameras.secondaryPoint().has_value());
    rig.step();
    CHECK(rig.follow.lastFrameTurn() != 0.0F);
    rig.cameras.setSecondary(0.0, 5.0F);
    CHECK_FALSE(rig.cameras.secondaryPoint().has_value());
    CHECK(rig.cameras.secondaryRange() == 0.0F);
}

TEST_CASE("CfgFollowCamera through the manager configures the follow camera and slow motion's factor", "[camera]") {
    Rig rig;
    coney::camera::FollowSettings settings;
    rig.cameras.configureFollow(settings, 0.25F);
    CHECK(rig.follow.bandNear() == Approx(3.0F));
    CHECK(rig.cameras.slowMotion().factor() == 0.25F);
    rig.cameras.setFollowZoom(coney::camera::FollowZoom::Default);
    CHECK(rig.follow.bandNear() == Approx(4.8F));
    rig.cameras.setFollowAngle(20.0F);
    CHECK(rig.follow.targetPitch() == Approx(20.0F * kDegree));
}

TEST_CASE("a shake eases in 65% an update, drives the rumble above its threshold and counts down", "[camera]") {
    CameraShake shake;
    shake.start(2, false);
    shake.update(kStep, kStep);
    CHECK(shake.amplitude() == Approx(0.75F * 0.65F));
    // 255 × 0.65 = 166 is above the threshold 0x30 / 4 + 0x28 = 52 and capped at 0x30 + 0x60.
    CHECK(shake.rumble() == 0x90);
    // 0.15 s: five updates, then it eases back to nothing and the rumble stops.
    for (int i = 0; i < 4; ++i) {
        shake.update(kStep, kStep);
    }
    CHECK(shake.amplitude() > 0.7F);
    for (int i = 0; i < 10; ++i) {
        shake.update(kStep, kStep);
    }
    CHECK(shake.amplitude() < 0.01F);
    CHECK(shake.rumble() == 0);
    // In combat it is 0.66 as strong; level 0 stops it.
    shake.start(3, true);
    shake.update(kStep, kStep);
    CHECK(shake.amplitude() == Approx(0.66F * 0.65F).margin(1e-4));
    shake.start(0, false);
    CHECK(shake.amplitude() == 0.0F);
    // In slow motion the time counts down at 54 × step of the frame's time: 0.36 of it at 1/150 s.
    CameraShake slow;
    slow.start(1, false);
    for (int i = 0; i < 8; ++i) {
        slow.update(kStep, kStep / 5.0F);
    }
    CHECK(slow.amplitude() == Approx(0.5F).margin(1e-3));
}

TEST_CASE("slow motion's events set the characters' step to the factor of 1/30 s until no player has it", "[camera]") {
    SlowMotion slow;
    CHECK(slow.stepSeconds() == Approx(1.0F / 30.0F));
    slow.setFactor(0.2F);
    slow.event(SlowMotion::kEventOn, 0);
    CHECK(slow.active());
    CHECK(slow.stepSeconds() == Approx(1.0F / 150.0F));
    slow.event(SlowMotion::kEventOn, 1);
    slow.event(SlowMotion::kEventOff, 0);
    CHECK(slow.stepSeconds() == Approx(1.0F / 150.0F));
    slow.event(SlowMotion::kEventOff, 1);
    CHECK_FALSE(slow.active());
    CHECK(slow.stepSeconds() == Approx(1.0F / 30.0F));
    slow.event(0x30, 0);                 // not a slow-motion event
    slow.event(SlowMotion::kEventOn, 5); // no such player
    CHECK_FALSE(slow.active());
    // A script's HuSetSlowMo: a fraction of 1/30 s strictly between 0 and 1, else the normal step.
    slow.setScripted(0.5F);
    CHECK(slow.stepSeconds() == Approx(1.0F / 60.0F));
    slow.setScripted(-1.0F);
    CHECK(slow.stepSeconds() == Approx(1.0F / 30.0F));
}

TEST_CASE("CamSetupFollow and CfgFollowCamera made before the player exists apply when his camera is attached",
          "[camera]") {
    Cameras cameras;
    CHECK(cameras.setupFollow(kFollowHandle) == kFollowHandle);
    cameras.configureFollow(coney::camera::FollowSettings{}, 0.2F);
    cameras.makeActive(kFollowHandle, 0.0F);
    FollowCamera follow{kFeet, 0.0F};
    CHECK(follow.bandNear() == Approx(4.8F));
    cameras.attachFollow(&follow);
    CHECK(follow.bandNear() == Approx(3.0F));
    CHECK(cameras.current().kind == CameraKind::Follow);
    CHECK(near(cameras.view().position, follow.position()));
}

TEST_CASE("CameraSetClipping sets a locked camera's clips and the follow camera's far clip, at most 150", "[camera]") {
    Rig rig;
    rig.cameras.setClipping(kFollowHandle, 5.0F, 80.0F);
    CHECK(rig.cameras.followFarClip() == 80.0F);
    rig.step();
    CHECK(rig.cameras.view().farClip == 80.0F);
    CHECK(rig.cameras.view().nearClip == Approx(0.1F));
    rig.cameras.setClipping(kLockedHandle, 0.5F, 400.0F);
    REQUIRE(rig.cameras.locked(kLockedHandle) != nullptr);
    CHECK(rig.cameras.locked(kLockedHandle)->nearClip == 0.5F);
    CHECK(rig.cameras.locked(kLockedHandle)->farClip == 150.0F);
    rig.cameras.setClipping(99.0, 1.0F, 1.0F); // no camera: nothing
}

TEST_CASE("the active camera's handle and a camera's place are found by handle", "[camera]") {
    Rig rig;
    rig.cameras.makeActive(kLockedHandle, 0.0F);
    CHECK(rig.cameras.activeHandle() == kLockedHandle);
    REQUIRE(rig.cameras.positionOf(kLockedHandle).has_value());
    CHECK(near(*rig.cameras.positionOf(kLockedHandle), cutAway().position));
    CHECK_FALSE(rig.cameras.positionOf(99.0).has_value());
    rig.cameras.makeActive(kFollowHandle, 0.0F);
    CHECK(rig.cameras.activeHandle() == kFollowHandle);
    // The follow camera put at a point stays there.
    const Vec3 spot{kFeet.x + 2.0F, kFeet.y - 3.0F, kFeet.z + 2.0F};
    rig.cameras.setFollowPosition(spot);
    CHECK(near(rig.follow.position(), spot));
}

TEST_CASE("CamLockLocked lists a human once; the current locked camera pushes him back inside its sides", "[camera]") {
    Rig rig;
    // Two humans 10 m in front of the camera (which faces -x, so its right is +y): one inside, one beyond the right
    // side (half the 50 degree view is 4.7 m across there).
    std::map<double, Vec3> humans{{1.0, Vec3{40.0F, 40.0F, 0.0F}}, {2.0, Vec3{40.0F, 46.0F, 0.0F}}};
    std::vector<double> moved;
    rig.cameras.setLocator([&humans](double handle) -> std::optional<Vec3> {
        const auto found = humans.find(handle);
        return found == humans.end() ? std::nullopt : std::optional<Vec3>(found->second);
    });
    rig.cameras.setMover([&humans, &moved](double handle, Vec3 feet) {
        humans[handle] = feet;
        moved.push_back(handle);
    });
    rig.cameras.lockLocked(kLockedHandle, 2.0, true);
    rig.cameras.lockLocked(kLockedHandle, 2.0, true);
    rig.cameras.lockLocked(kLockedHandle, 1.0, true);
    rig.cameras.lockLocked(99.0, 1.0, true); // not a camera: nothing
    REQUIRE(rig.cameras.locked(kLockedHandle) != nullptr);
    CHECK(rig.cameras.locked(kLockedHandle)->keptInView == std::vector<double>{2.0, 1.0});
    // Not current: nobody moves.
    rig.step();
    CHECK(moved.empty());
    rig.cameras.makeActive(kLockedHandle, 0.0F);
    rig.step();
    // Human 2 is pushed to 0.3 m inside the right side, at head height; human 1, after him, is placed again too (the
    // pushed flag carries over) but where he was.
    REQUIRE(moved == std::vector<double>{2.0, 1.0});
    const std::array<coney::camera::ViewSide, 2> sides = coney::camera::viewSides(cutAway().view());
    const Vec3 head = coney::anim::add(humans[2.0], Vec3{0.0F, 0.0F, coney::camera::KeepInViewRules::kHeadHeight});
    CHECK(coney::anim::dot(head, sides[1].normal) - sides[1].w == Approx(0.3F).margin(1e-3));
    CHECK(humans[2.0].y < 46.0F);
    CHECK(near(humans[1.0], Vec3{40.0F, 40.0F, 0.0F}));
    // Both inside now: the next update moves no one.
    moved.clear();
    rig.step();
    CHECK(moved.empty());
    // Off removes him.
    rig.cameras.lockLocked(kLockedHandle, 2.0, false);
    CHECK(rig.cameras.locked(kLockedHandle)->keptInView == std::vector<double>{1.0});
}
