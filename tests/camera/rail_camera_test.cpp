// SPDX-License-Identifier: GPL-3.0-or-later
// The rail, fixed and third-person cameras (docs/references/bindings/camera.md#camsetuprail,
// docs/references/bindings/camera.md#cameracreatefixed, docs/references/bindings/camera.md#cameracreatethird): placing
// on a rail level with, ahead of or behind the target, eased settings, and the cameras' manager switching to them.
#include "camera/rail_camera.h"

#include <array>
#include <cmath>
#include <numbers>
#include <optional>
#include <utility>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "animation/anim_math.h"
#include "camera/cameras.h"
#include "camera/fixed_camera.h"
#include "camera/third_camera.h"

using Catch::Approx;
using coney::anim::Vec3;
using coney::camera::CameraKind;
using coney::camera::Cameras;
using coney::camera::FixedCamera;
using coney::camera::RailCamera;
using coney::camera::RailMode;
using coney::camera::RailSetup;
using coney::camera::ThirdCamera;
using coney::camera::ThirdCameraSettings;

namespace {

constexpr float kStep = 1.0F / 30.0F;

// A rail along +x from the origin, 20 m long, in two segments, with a look-at offset 1.5 m up.
RailCamera straightRail() {
    RailCamera rail;
    RailSetup setup;
    setup.fieldOfView = 60.0F;
    setup.offset = Vec3{0.0F, 0.0F, 1.5F};
    setup.nearClip = 0.1F;
    setup.farClip = 400.0F;
    rail.setup(setup);
    rail.addPoint(Vec3{0.0F, 0.0F, 2.0F});
    rail.addPoint(Vec3{10.0F, 0.0F, 2.0F});
    rail.addPoint(Vec3{20.0F, 0.0F, 2.0F});
    return rail;
}

} // namespace

TEST_CASE("the rail camera in mode 0 frames a chase as level3's main framing does: shifted, within reach, under a "
          "ceiling",
          "[camera]") {
    RailCamera rail;
    RailSetup setup;
    setup.fieldOfView = 78.0F;
    setup.offset = Vec3{0.0F, 0.0F, 2.0F};
    rail.setup(setup);
    rail.addPoint(Vec3{0.0F, 0.0F, 30.0F});
    rail.addPoint(Vec3{100.0F, 0.0F, 30.0F});
    rail.modify(RailCamera::kShift, -8.0F, 0.0F);
    rail.modify(RailCamera::kLookShift, 6.0F, 0.0F);
    rail.modify(RailCamera::kDistance, 5.5F, 0.0F);
    rail.modify(RailCamera::kHeight, 4.0F, 0.0F);
    rail.update(Vec3{50.0F, 15.0F, 20.0F}, false, kStep);
    // P: the feet raised by the offset's 2 m and shifted 8 m back along the rail; the look-at point 6 m on from it.
    CHECK(rail.targetPoint().x == Approx(42.0F));
    CHECK(rail.targetPoint().z == Approx(22.0F));
    CHECK(rail.lookAt().x == Approx(48.0F));
    // The camera: P's foot on the rail, pulled out to 5.5 m from P in plan, lowered to 4 m above the feet.
    CHECK(rail.position().x == Approx(42.0F));
    CHECK(rail.position().y == Approx(9.5F));
    CHECK(rail.position().z == Approx(24.0F));
    // A step of 3 m in plan is damped to a quarter.
    rail.update(Vec3{53.0F, 15.0F, 20.0F}, false, kStep);
    CHECK(rail.targetPoint().x == Approx(42.75F));
    CHECK(rail.position().x == Approx(42.75F));
}

TEST_CASE("the rail camera leads its target ahead or behind along the rail, clamped to the rail's ends", "[camera]") {
    RailCamera rail = straightRail();
    rail.setLead(3.0F, 0.0F, true);
    rail.update(Vec3{12.0F, 4.0F, 0.0F}, true, kStep);
    CHECK(rail.mode() == RailMode::Ahead);
    CHECK(rail.position().x == Approx(15.0F));
    CHECK(rail.lookAt().x == Approx(15.0F));
    rail.setLead(3.0F, 0.0F, false);
    rail.update(Vec3{1.0F, 4.0F, 0.0F}, false, kStep);
    CHECK(rail.position().x == Approx(0.0F));
    rail.update(Vec3{25.0F, 4.0F, 0.0F}, false, kStep);
    CHECK(rail.position().x == Approx(17.0F));
}

TEST_CASE("the rail camera holds at the rail's end; its settings ease linearly; the pitch fixes the look-at point",
          "[camera]") {
    RailCamera rail = straightRail();
    rail.update(Vec3{15.0F, 4.0F, 0.0F}, false, kStep);
    CHECK(rail.position().x == Approx(15.0F));
    CHECK_FALSE(rail.held());
    for (int i = 0; i < 30; ++i) {
        rail.update(Vec3{30.0F, 4.0F, 0.0F}, false, kStep);
    }
    CHECK(rail.held());
    CHECK(rail.position().x <= 20.0F);

    rail.modify(RailCamera::kFieldOfView, 30.0F, 1.0F);
    rail.update(Vec3{15.0F, 4.0F, 0.0F}, false, 0.5F);
    CHECK(rail.fieldOfView() == Approx(45.0F));
    rail.update(Vec3{15.0F, 4.0F, 0.0F}, false, 0.5F);
    CHECK(rail.fieldOfView() == Approx(30.0F));
    for (int i = 0; i < 30; ++i) {
        rail.update(Vec3{15.0F, 4.0F, 0.0F}, false, kStep);
    }
    CHECK_FALSE(rail.held());
    // A pitch of 0 is taken as -0.05 degrees: the look-at point 6 m toward the target in plan, barely below.
    rail.modify(RailCamera::kPitch, 0.0F, 0.0F);
    rail.update(Vec3{15.0F, 4.0F, 0.0F}, false, kStep);
    const Vec3 toward = coney::anim::subtract(rail.lookAt(), rail.position());
    CHECK(std::hypot(toward.x, toward.y) == Approx(6.0F));
    CHECK(toward.z < 0.0F);
    CHECK(toward.z > -0.01F);

    for (int i = 0; i < 20; ++i) {
        rail.addPoint(Vec3{static_cast<float>(i), 1.0F, 0.0F});
    }
    CHECK(rail.pointCount() == RailCamera::kMaxPoints);
    CHECK(rail.point(RailCamera::kMaxPoints - 1).x == Approx(19.0F));
    // CamSetupRail starts the rail over but keeps the lead.
    rail.setLead(2.0F, 0.0F, true);
    rail.setup(RailSetup{});
    CHECK(rail.pointCount() == 0);
    CHECK(rail.mode() == RailMode::Level);
    CHECK(rail.lead() == Approx(2.0F));
}

TEST_CASE(
    "the rail camera in mode 3 trails the Armies fighter forward only, at a fixed pitch, 1 m/s faster than he goes",
    "[camera]") {
    // level60's Rail1: along -x at y = 39.5, 3.7 m up; the look-at offset (0, -0.25, 1.25); CamModifyRail(5, -7, 0).
    RailCamera rail;
    RailSetup setup;
    setup.fieldOfView = 22.0F;
    setup.offset = Vec3{0.0F, -0.25F, 1.25F};
    rail.setup(setup);
    rail.addPoint(Vec3{100.0F, 39.5F, 3.7F});
    rail.addPoint(Vec3{-752.3F, 39.5F, 3.7F});
    rail.addPoint(Vec3{-850.0F, 39.5F, 3.7F});
    rail.modify(RailCamera::kMode3Pitch, -7.0F, 0.0F);
    rail.modify(RailCamera::kMode3Margin, 0.5F, 0.0F);
    CHECK(rail.mode() == RailMode::Trailing);
    rail.activate();
    const float drop = 2.0F * std::tan(-7.0F * std::numbers::pi_v<float> / 180.0F);

    // The start: on the rail level with the target point, looking 2 m toward it in plan at the pitch.
    rail.update(Vec3{-94.9F, 24.0F, 0.0F}, false, kStep, Vec3{});
    CHECK(rail.position().x == Approx(-94.9F));
    CHECK(rail.position().y == Approx(39.5F));
    CHECK(rail.position().z == Approx(3.7F));
    CHECK(rail.lookAt().y == Approx(37.5F));
    CHECK(rail.lookAt().z == Approx(3.7F + drop));

    // He goes 2 m on along the rail at 3 m/s: the look-at point moves (3 + 1) m/s x 1/30 s toward the goal, under
    // 40% of the 2 m gap, and the camera is its foot on the rail.
    rail.update(Vec3{-96.9F, 24.0F, 0.0F}, false, kStep, Vec3{-3.0F, 0.0F, 0.0F});
    const float moved = 4.0F * kStep;
    CHECK(rail.position().x == Approx(-94.9F - moved));
    CHECK(rail.position().y == Approx(39.5F));
    CHECK(rail.lookAt().x == Approx(-94.9F - moved));
    CHECK(rail.lookAt().y == Approx(37.5F));
    CHECK(rail.lookAt().z == Approx(3.7F + drop));

    // Back the way he came: the goal lies behind, so the camera keeps its place.
    rail.update(Vec3{-90.0F, 24.0F, 0.0F}, false, kStep, Vec3{3.0F, 0.0F, 0.0F});
    CHECK(rail.position().x == Approx(-94.9F - moved));
    CHECK_FALSE(rail.held());

    // Below -360 the setting switches mode 3 off.
    rail.modify(RailCamera::kMode3Pitch, -400.0F, 0.0F);
    CHECK(rail.mode() == RailMode::Level);
}

TEST_CASE("the rail camera's mode 3 pitch switched on with a time eases from the camera's present pitch", "[camera]") {
    // Expected: camera.md#rail-mode3, confirmed (code): CamRail_Update starts setting 5's pitch, while off, at the
    // camera's present pitch (CamRail_PitchToPoint) and eases it to the target.
    // A fixed look-down of 30 degrees first (setting 8), so the present pitch is far from mode 3's.
    RailCamera rail = straightRail();
    rail.modify(RailCamera::kPitch, -30.0F, 0.0F);
    rail.update(Vec3{5.0F, 4.0F, 0.0F}, false, kStep, Vec3{});
    const Vec3 toward{rail.lookAt().x - rail.position().x, rail.lookAt().y - rail.position().y,
                      rail.lookAt().z - rail.position().z};
    const float present = std::atan2(toward.z, std::hypot(toward.x, toward.y)) * 180.0F / std::numbers::pi_v<float>;
    REQUIRE(std::abs(present - -7.0F) > 1.0F);
    rail.modify(RailCamera::kMode3Pitch, -7.0F, 2.0F);
    rail.update(Vec3{5.0F, 4.0F, 0.0F}, false, 1.0F, Vec3{});
    CHECK(rail.mode3Pitch() == Approx((present + -7.0F) / 2.0F).margin(0.01F));
}

TEST_CASE("an activation snaps the rail camera's eased settings to their targets", "[camera]") {
    // Expected: camera.md#rail, confirmed (code): CamRail_Activate sets the field of view and every eased setting to
    // its target at once and zeroes their times.
    RailCamera rail = straightRail();
    rail.modify(RailCamera::kFieldOfView, 40.0F, 5.0F);
    rail.modify(RailCamera::kMode3Pitch, -7.0F, 5.0F);
    rail.activate();
    CHECK(rail.fieldOfView() == Approx(40.0F));
    CHECK(rail.mode3Pitch() == Approx(-7.0F));
}

TEST_CASE("the rail camera in mode 3 holds once it reaches the rail's first point", "[camera]") {
    RailCamera rail = straightRail();
    rail.modify(RailCamera::kMode3Pitch, -7.0F, 0.0F);
    rail.activate();
    // Started before the rail's first point: clamped there, and held from the first later update.
    rail.update(Vec3{-5.0F, 4.0F, 0.0F}, false, kStep, Vec3{});
    CHECK(rail.position().x == Approx(0.0F));
    rail.update(Vec3{5.0F, 4.0F, 0.0F}, false, kStep, Vec3{3.0F, 0.0F, 0.0F});
    CHECK(rail.held());
    CHECK(rail.position().x == Approx(0.0F));
}

TEST_CASE("the rail camera in mode 3 keeps under its height setting over the target's feet while it stands still",
          "[camera]") {
    // Expected: camera.md#rail-mode3, confirmed (code): CamRail_PlaceMode3 (0x0013dcc8) ends both paths, moving or
    // not, at the height ceiling: the camera at most setting 1 above the target's feet.
    RailCamera rail = straightRail();
    rail.modify(RailCamera::kMode3Pitch, -7.0F, 0.0F);
    rail.activate();
    rail.update(Vec3{5.0F, 4.0F, 0.0F}, false, kStep, Vec3{});
    REQUIRE(rail.position().z == Approx(2.0F));
    rail.modify(RailCamera::kHeight, 1.0F, 0.0F);
    // He steps back the way he came: the goal lies behind, so the camera keeps its place along the rail, but no more
    // than 1 m over his feet.
    rail.update(Vec3{4.0F, 4.0F, 0.0F}, false, kStep, Vec3{-3.0F, 0.0F, 0.0F});
    CHECK(rail.position().x == Approx(5.0F));
    CHECK_FALSE(rail.held());
    CHECK(rail.position().z == Approx(1.0F));
}

TEST_CASE("a fixed camera looks at its targets' average plus its offset; a third-person camera sits behind its target",
          "[camera]") {
    FixedCamera fixed(Vec3{0.0F, 0.0F, 5.0F}, Vec3{0.0F, 0.0F, 1.0F}, 50.0F, 0.1F, 300.0F);
    CHECK(fixed.view().farClip == Approx(FixedCamera::kMaxFarClip));
    const std::array<Vec3, 2> targets{Vec3{10.0F, 0.0F, 0.0F}, Vec3{20.0F, 10.0F, 0.0F}};
    fixed.update(targets);
    CHECK(fixed.lookAt().x == Approx(15.0F));
    CHECK(fixed.lookAt().y == Approx(5.0F));
    CHECK(fixed.lookAt().z == Approx(1.0F));
    fixed.update({});
    CHECK(fixed.lookAt().x == Approx(15.0F));

    ThirdCamera third(ThirdCameraSettings{});
    third.update(Vec3{0.0F, 0.0F, 0.0F}, 0.0F);
    CHECK(third.lookAt().z == Approx(1.5F));
    CHECK(third.position().y == Approx(-5.1F));
    CHECK(third.position().z == Approx(3.3F));
    // Its facing turns a tenth of the way to the target's each update.
    third.update(Vec3{0.0F, 0.0F, 0.0F}, 90.0F);
    CHECK(third.headingDegrees() == Approx(9.0F));
}

TEST_CASE("the cameras' manager keeps one rail camera, fixed cameras on the target list and third-person cameras",
          "[camera]") {
    Cameras cameras;
    cameras.setPlacer([](double handle) -> std::optional<std::pair<Vec3, float>> {
        if (handle == 3.0) {
            return std::pair<Vec3, float>{Vec3{5.0F, 4.0F, 0.0F}, 0.0F};
        }
        return std::nullopt;
    });
    cameras.setLocator([](double handle) -> std::optional<Vec3> {
        if (handle == 3.0) {
            return Vec3{5.0F, 4.0F, 0.0F};
        }
        return std::nullopt;
    });
    CHECK_FALSE(cameras.addRailPoint(Vec3{}));
    RailSetup setup;
    setup.fieldOfView = 60.0F;
    CHECK(cameras.setupRail(20.0, 3.0, setup) == 20.0);
    CHECK(cameras.setupRail(21.0, 3.0, setup) == 20.0);
    CHECK(cameras.addRailPoint(Vec3{0.0F, 0.0F, 2.0F}));
    CHECK(cameras.addRailPoint(Vec3{10.0F, 0.0F, 2.0F}));
    cameras.makeActive(20.0, 0.0F);
    CHECK(cameras.current().kind == CameraKind::Rail);
    CHECK(cameras.activeHandle().value_or(0.0) == 20.0);
    CHECK(cameras.view().position.x == Approx(5.0F));

    cameras.createFixed(30.0, 3.0, FixedCamera(Vec3{0.0F, 0.0F, 5.0F}, Vec3{}, 50.0F, 0.1F, 100.0F));
    REQUIRE(cameras.targets().size() == 1);
    cameras.createFixed(31.0, 3.0, FixedCamera(Vec3{0.0F, 0.0F, 5.0F}, Vec3{}, 50.0F, 0.1F, 100.0F));
    CHECK(cameras.targets().size() == 1);
    cameras.makeActive(30.0, 0.0F);
    CHECK(cameras.view().lookAt.x == Approx(5.0F));
    cameras.deleteCamera(30.0);
    CHECK(cameras.fixed(30.0) == nullptr);
    CHECK(cameras.current().kind != CameraKind::Fixed);

    ThirdCameraSettings settings;
    cameras.createThird(40.0, 3.0, settings);
    cameras.makeActive(40.0, 0.0F);
    CHECK(cameras.current().kind == CameraKind::Third);
    CHECK(cameras.view().lookAt.x == Approx(5.0F));
    // CamDelete keeps third-person cameras, as the original's does.
    cameras.deleteCamera(40.0);
    CHECK(cameras.third(40.0) != nullptr);
}
