// SPDX-License-Identifier: GPL-3.0-or-later
// The death camera (docs/research/camera.md#death-camera): its top-down placement under whatever is overhead, its turn
// and sink, and the manager's one-time cut to it.
#include "camera/failed_camera.h"

#include <cmath>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "animation/anim_math.h"
#include "camera/cameras.h"
#include "camera/follow_camera.h"
#include "support/collision_fixtures.h"

using Catch::Approx;
using coney::anim::Vec3;
using coney::camera::CameraKind;
using coney::camera::Cameras;
using coney::camera::FailedCamera;
using coney::camera::FollowCamera;
using coney::camera::FollowTarget;
using coney::test::Tri;

namespace {

constexpr Vec3 kFeet{40.0F, 40.0F, 0.0F};

// A ceiling square at height z round kFeet, facing down, with `flags`.
std::vector<Tri> ceilingAt(float z, std::uint16_t flags = 0) {
    const float x0 = kFeet.x - 2.0F;
    const float x1 = kFeet.x + 2.0F;
    const float y0 = kFeet.y - 2.0F;
    const float y1 = kFeet.y + 2.0F;
    return {Tri{{x0, y0, z}, {x1, y1, z}, {x1, y0, z}, flags, 5, 1},
            Tri{{x0, y0, z}, {x0, y1, z}, {x1, y1, z}, flags, 5, 1}};
}

} // namespace

TEST_CASE("the death camera looks almost straight down from 5 m over the feet at a random step's turn", "[camera]") {
    const FailedCamera open(kFeet, nullptr, 0);
    CHECK(open.distance() == Approx(5.0F));
    CHECK(open.rollDegrees() == Approx(-180.0F));
    const auto view = open.view();
    CHECK(view.position.z == Approx(5.0F));
    CHECK(view.position.x == Approx(kFeet.x));
    CHECK(view.position.y == Approx(kFeet.y));
    // Pitched 88.28° down: the forward's drop is sin 88.28°.
    const Vec3 forward = coney::camera::viewForward(view);
    CHECK(forward.z == Approx(-std::sin(88.28F * 3.14159265F / 180.0F)).margin(1e-4F));
    CHECK(view.fieldOfView == Approx(60.0F));
    CHECK(view.nearClip == Approx(0.3F));

    // Step 9 of 36 is −90°; steps wrap.
    CHECK(FailedCamera(kFeet, nullptr, 9).rollDegrees() == Approx(-90.0F));
    CHECK(FailedCamera(kFeet, nullptr, 36 + 18).rollDegrees() == Approx(0.0F));
}

TEST_CASE("the death camera sits under a ceiling less the near clip, and the 0x200 mask passes some", "[camera]") {
    const auto low = coney::test::makeMesh(ceilingAt(3.0F));
    CHECK(FailedCamera(kFeet, low.get(), 0).distance() == Approx(3.0F - 0.3F).margin(1e-3F));
    // Higher than near + 5 m: out of the ray's reach.
    const auto high = coney::test::makeMesh(ceilingAt(6.0F));
    CHECK(FailedCamera(kFeet, high.get(), 0).distance() == Approx(5.0F));
    // A face with type bit 0x200 does not stop the ray.
    const auto masked = coney::test::makeMesh(ceilingAt(3.0F, 0x200));
    CHECK(FailedCamera(kFeet, masked.get(), 0).distance() == Approx(5.0F));
}

TEST_CASE("the death camera turns 15 degrees a second and sinks 0.18 m a second to 2.5 m", "[camera]") {
    FailedCamera shot(kFeet, nullptr, 18);
    for (int i = 0; i < 180; ++i) {
        shot.update(1.0F / 30.0F);
    }
    // Six seconds: a 90° turn, 5 − 1.08 m.
    CHECK(shot.rollDegrees() == Approx(90.0F).margin(1e-3F));
    CHECK(shot.distance() == Approx(3.92F).margin(1e-3F));
    for (int i = 0; i < 900; ++i) {
        shot.update(1.0F / 30.0F);
    }
    CHECK(shot.distance() == Approx(2.5F));
    shot.setRotating(false);
    const float roll = shot.rollDegrees();
    shot.update(1.0F);
    CHECK(shot.rollDegrees() == roll);
}

TEST_CASE("the game-over shot cuts from the follow camera once and keeps the camera it replaced", "[camera]") {
    FollowCamera follow{kFeet, 0.0F};
    Cameras cameras;
    cameras.attachFollow(&follow);
    FollowTarget target;
    target.feet = kFeet;
    cameras.update(target, 128, 128, nullptr, 1.0F / 30.0F);
    REQUIRE(cameras.current().kind == CameraKind::Follow);

    const auto cuts = cameras.cuts();
    CHECK(cameras.startFailed(kFeet, nullptr, 0));
    CHECK(cameras.current().kind == CameraKind::Failed);
    CHECK(cameras.beforeFailed().kind == CameraKind::Follow);
    CHECK_FALSE(cameras.blending());
    CHECK(cameras.cuts() == cuts + 1);
    CHECK(cameras.view().position.z == Approx(5.0F));
    CHECK_FALSE(cameras.activeHandle().has_value());

    // Later updates of the same failure find the shot and do not start it again.
    CHECK_FALSE(cameras.startFailed(Vec3{0.0F, 0.0F, 0.0F}, nullptr, 5));
    cameras.update(target, 128, 128, nullptr, 1.0F);
    REQUIRE(cameras.failed() != nullptr);
    CHECK(cameras.failed()->rollDegrees() == Approx(-165.0F));
    CHECK(cameras.view().position.z == Approx(5.0F - 0.18F));
}

TEST_CASE("while the mission has failed CameraMakeActive cannot leave the death camera", "[camera]") {
    FollowCamera follow{kFeet, 0.0F};
    Cameras cameras;
    cameras.attachFollow(&follow);
    REQUIRE(cameras.setupFollow(7.0) == 7.0);
    FollowTarget target;
    target.feet = kFeet;
    cameras.update(target, 128, 128, nullptr, 1.0F / 30.0F);
    REQUIRE(cameras.startFailed(kFeet, nullptr, 1));
    cameras.setMissionFailed(true);
    cameras.makeActive(7.0, 0.0F);
    CHECK(cameras.current().kind == CameraKind::Failed);
    // A retry clears the failure; then the scripts can switch again.
    cameras.setMissionFailed(false);
    cameras.makeActive(7.0, 0.0F);
    CHECK(cameras.current().kind == CameraKind::Follow);
}
