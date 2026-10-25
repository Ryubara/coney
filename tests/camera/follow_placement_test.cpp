// SPDX-License-Identifier: GPL-3.0-or-later
// The follow camera when its target is moved by a placement (a teleport): it is not cut, its look-at point follows the
// new place and it resets itself behind the target only when lost (update step 7); a reset reads where the target
// stands now, and a scene's end hands the camera back behind the place the scene left the player
// (docs/research/camera.md#update, docs/research/camera.md#setting-up, docs/research/camera.md#scenes).
#include <cmath>
#include <cstdint>
#include <numbers>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "animation/anim_math.h"
#include "camera/camera_view.h"
#include "camera/cameras.h"
#include "camera/follow_camera.h"

using Catch::Approx;
using coney::anim::Vec3;
using coney::camera::Cameras;
using coney::camera::FollowCamera;
using coney::camera::FollowPlace;
using coney::camera::FollowTarget;

namespace {

constexpr float kDegree = std::numbers::pi_v<float> / 180.0F;
constexpr float kStep = 1.0F / 30.0F;
constexpr std::uint8_t kRest = 128;

// A target standing at `feet` facing `heading`: nothing the automatic rules follow.
FollowTarget standing(Vec3 feet, float heading = 0.0F) {
    FollowTarget target;
    target.feet = feet;
    target.heading = heading;
    return target;
}

// A camera on a target at the origin facing +y, settled by a few updates standing.
FollowCamera settled() {
    FollowCamera camera(Vec3{0.0F, 0.0F, 0.0F}, 0.0F);
    for (int i = 0; i < 10; ++i) {
        camera.update(standing(Vec3{0.0F, 0.0F, 0.0F}), kRest, kRest, nullptr, kStep);
    }
    return camera;
}

// The camera's distance from its look-at point across the ground.
float acrossGround(const FollowCamera& camera) {
    return std::hypot(camera.position().x - camera.lookAt().x, camera.position().y - camera.lookAt().y);
}

// Restores the follow camera's tunables when a test that changed them ends, however it ends.
struct TuningGuard {
    coney::camera::FollowTuning saved = coney::camera::followTuning();
    TuningGuard() = default;
    TuningGuard(const TuningGuard&) = delete;
    TuningGuard& operator=(const TuningGuard&) = delete;
    TuningGuard(TuningGuard&&) = delete;
    TuningGuard& operator=(TuningGuard&&) = delete;
    ~TuningGuard() { coney::camera::followTuning() = saved; }
};

} // namespace

TEST_CASE("a camera more than 30 m above or below the look-at point is put behind the target", "[camera]") {
    // Expected: camera.md#update, confirmed (code) (step 7's height test, 0x0012b988-0x0012b9bc)
    FollowCamera camera = settled();
    // Put 35 m up in the air (followed directly, no length limit): the camera, 1 m up, is 35 m below.
    FollowTarget up = standing(Vec3{0.0F, 0.0F, 35.0F});
    up.airborne = true;
    camera.update(up, kRest, kRest, nullptr, kStep);
    CHECK(camera.position().z > 34.0F);
    CHECK(coney::anim::distance(camera.position(), camera.lookAt()) < 6.0F);
}

TEST_CASE("a follow camera far behind its target is put behind it at once but not after a short move or with two "
          "player cameras",
          "[camera]") {
    // Expected: camera.md#update, confirmed (code) (step 7 at 0x0012b918-0x0012b9c8, Cam_Follow_Reset 0x00124d00; it
    // runs only while 0x0050b19c is 1). The reset is the half of the case that fails before the fix; the glide after a
    // short move and the no-reset with two player cameras are its other sides, which the unchanged camera already
    // had, so they sit in this case (the same step 7 decides all three) instead of in cases that could not fail first.
    {
        // A target moved beyond 1.3 times the maximum distance: put 30 m ahead facing -x, the look-at point closes
        // 20% (6 m), which leaves the held position 10.7 m from it across the ground, more than 1.3 x 6.6 = 8.58 m:
        // the camera is reset behind the target where it stands.
        FollowCamera camera = settled();
        camera.update(standing(Vec3{0.0F, 30.0F, 0.0F}, 90.0F * kDegree), kRest, kRest, nullptr, kStep);
        CHECK(camera.lookAt().y == Approx(30.0F).margin(1e-3));
        CHECK(camera.position().x > 3.0F); // behind a target facing -x
        CHECK(std::abs(camera.position().y - 30.0F) < 0.5F);
        CHECK(acrossGround(camera) < 6.0F);
    }
    {
        // A short move: the target put 2 m ahead (a Teleport) is more than 0.8 m, so the look-at point moves 20% of
        // the way, and the camera, not lost, is not cut.
        FollowCamera camera = settled();
        const Vec3 before = camera.position();
        camera.update(standing(Vec3{0.0F, 2.0F, 0.0F}), kRest, kRest, nullptr, kStep);
        CHECK(camera.lookAt().y == Approx(0.4F).margin(1e-4));
        CHECK(coney::anim::distance(camera.position(), before) < 0.5F);
        CHECK(camera.position().y < 0.0F);
    }
    {
        // Two player cameras: the same 30 m move is not reset. The look-at point closes 20% of the 30 m and the
        // camera is only dragged after it (the leash, the hard band).
        const TuningGuard guard;
        coney::camera::followTuning().onePlayerCamera = false;
        FollowCamera camera = settled();
        camera.update(standing(Vec3{0.0F, 30.0F, 0.0F}), kRest, kRest, nullptr, kStep);
        CHECK(camera.lookAt().y == Approx(6.0F).margin(1e-3));
        CHECK(camera.position().y < camera.lookAt().y);
    }
}

TEST_CASE("CameraReset right after a teleport puts the camera behind the new place, read from the target", "[camera]") {
    // Expected: camera.md#setting-up, confirmed (code) (Cam_Follow_PlaceAtHeading 0x001250a8 gathers the targets and
    // snaps the look-at point first, Cam_Follow_RefreshLookAt 0x00127d48)
    FollowPlace where{.feet = Vec3{0.0F, 0.0F, 0.0F}, .heading = 0.0F};
    FollowCamera camera = settled();
    camera.setTargetSource([&where] { return where; });
    // Teleported 40 m east facing +y, with no camera update between: the reset reads the new place.
    where = FollowPlace{.feet = Vec3{40.0F, 0.0F, 0.0F}, .heading = 0.0F};
    CHECK(camera.targetPlace().feet.x == Approx(40.0F));
    camera.reset();
    CHECK(camera.lookAt().x == Approx(40.0F));
    CHECK(camera.position().x == Approx(40.0F).margin(1e-3));
    CHECK(camera.position().y < 0.0F);
    CHECK(acrossGround(camera) < 6.0F);
}

TEST_CASE("a scene's end hands the follow camera back behind the place the scene left the player", "[camera]") {
    // Expected: camera.md#scenes, confirmed (code) (SceneTask_End 0x0039f450: pop, reset, make current, the cameras'
    // update with 0.17 s)
    FollowPlace where{.feet = Vec3{0.0F, 0.0F, 0.0F}, .heading = 0.0F};
    FollowCamera follow(where.feet, 0.0F);
    follow.setTargetSource([&where] { return where; });
    Cameras cameras;
    cameras.attachFollow(&follow);
    const coney::camera::CameraView scene =
        coney::camera::viewLookingAt(Vec3{0, 0, 5}, Vec3{0, 10, 0}, 40.0F, 0.1F, 100.0F);
    cameras.beginScene(scene);
    cameras.update(standing(where.feet), kRest, kRest, nullptr, kStep);
    // The scene lets the player go 25 m north facing -y, before its camera ends (no update between).
    where = FollowPlace{.feet = Vec3{0.0F, 25.0F, 0.0F}, .heading = 180.0F * kDegree};
    cameras.endScene(0.0F);
    CHECK(follow.lookAt().y == Approx(25.0F).margin(1e-3));
    CHECK(follow.position().y > 25.0F); // behind a player facing -y
    CHECK(acrossGround(follow) < 6.0F);
}
