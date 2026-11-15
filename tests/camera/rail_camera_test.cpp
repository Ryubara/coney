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
    // Expected: camera.md#rail, confirmed (code)
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

namespace {

// An L-shaped rail: along +x to (10, 0), then along +y to (10, 10), 2 m up, with a look-at offset 1.5 m up.
RailCamera cornerRail() {
    RailCamera rail;
    RailSetup setup;
    setup.fieldOfView = 60.0F;
    setup.offset = Vec3{0.0F, 0.0F, 1.5F};
    rail.setup(setup);
    rail.addPoint(Vec3{0.0F, 0.0F, 2.0F});
    rail.addPoint(Vec3{10.0F, 0.0F, 2.0F});
    rail.addPoint(Vec3{10.0F, 10.0F, 2.0F});
    return rail;
}

} // namespace

TEST_CASE("the rail camera's leading modes stand the lead from the target's foot on the rail, mirrored before the "
          "first point and at the last point near the end",
          "[camera]") {
    // Expected: camera.md#rail-leading, confirmed (code): CamRail_PlaceLeading (0x0013e708) steps 1, 2 and 4 with
    // switch 7 off; the rail point's share is a plan length over the segment's, so it is never negative.
    struct Case {
        bool ahead;
        Vec3 feet;
        float x; // where the camera stands along the straight rail (y 0, z 2)
    };
    const std::array<Case, 4> cases{{
        // Ahead 3 m of P's foot (12, 0) on the last segment, which is 8 m from the end: 15.
        {true, Vec3{12.0F, 4.0F, 0.0F}, 15.0F},
        // Behind 3 m of the foot at 1 m: the lead point 2 m before the start gives the share 0.2, so 2 m past it.
        {false, Vec3{1.0F, 4.0F, 0.0F}, 2.0F},
        // P 1 m behind the start, within the lead: 3 + 1 m past it.
        {false, Vec3{-1.0F, 4.0F, 0.0F}, 4.0F},
        // P 4 m behind the start, more than the lead: the first point.
        {false, Vec3{-4.0F, 4.0F, 0.0F}, 0.0F},
    }};
    for (const Case& c : cases) {
        RailCamera rail = straightRail();
        rail.setLead(3.0F, 0.0F, c.ahead);
        rail.update(c.feet, false, kStep);
        CHECK(rail.mode() == (c.ahead ? RailMode::Ahead : RailMode::Behind));
        CHECK(rail.position().x == Approx(c.x).margin(1e-5F));
        CHECK(rail.position().y == Approx(0.0F).margin(1e-5F));
        CHECK(rail.position().z == Approx(2.0F));
        // Switch 7 off: the look-at point is P, the feet raised by the offset.
        CHECK(rail.lookAt().x == Approx(c.feet.x));
        CHECK(rail.lookAt().z == Approx(1.5F));
    }
    // On the last segment past its end point the camera takes the last point.
    RailCamera rail = straightRail();
    rail.setLead(3.0F, 0.0F, false);
    rail.update(Vec3{25.0F, 4.0F, 0.0F}, false, kStep);
    CHECK(rail.position().x == Approx(20.0F));
}

TEST_CASE("the rail camera's lead point that crosses a joint is the mean of the old and new lead points; switch 7 "
          "stands the camera the lead from the target toward the rail point and looks at the target",
          "[camera]") {
    // Expected: camera.md#rail-leading, confirmed (code): CamRail_PlaceLeading (0x0013e708) steps 3 and 5.
    // P (8, -1) on the first segment: its foot F (8, 0), the lead point (11, 0) past the corner, so the segment steps
    // on; there F' is (10, -1) and the lead point (10, 2); their mean (10.5, 1) lies sqrt(1.25) m from the corner: the
    // share 0.1118 of the second segment.
    const float share = std::sqrt(1.25F) / 10.0F;
    RailCamera off = cornerRail();
    off.setLead(3.0F, 0.0F, true);
    off.update(Vec3{8.0F, -1.0F, 0.0F}, false, kStep);
    CHECK(off.segment() == 1);
    CHECK(off.position().x == Approx(10.0F));
    CHECK(off.position().y == Approx(10.0F * share));
    CHECK(off.position().z == Approx(2.0F));

    // Switch 7 on: the camera leaves the rail, 3 m from P in plan along the direction from the first foot F (8, 0) to
    // that rail point, at the rail point's height, and looks at P.
    RailCamera on = cornerRail();
    on.setLead(3.0F, 0.0F, true);
    on.update(Vec3{8.0F, -1.0F, 0.0F}, true, kStep);
    const float dx = 2.0F;
    const float dy = 10.0F * share;
    const float plan = std::hypot(dx, dy);
    CHECK(on.position().x == Approx(8.0F + (3.0F * dx / plan)));
    CHECK(on.position().y == Approx(-1.0F + (3.0F * dy / plan)));
    CHECK(on.position().z == Approx(2.0F));
    CHECK(on.lookAt().x == Approx(8.0F));
    CHECK(on.lookAt().y == Approx(-1.0F));
    CHECK(on.lookAt().z == Approx(1.5F));

    // Along a straight rail it stands the lead ahead of him, beside the rail.
    RailCamera beside = straightRail();
    beside.setLead(3.0F, 0.0F, true);
    beside.update(Vec3{12.0F, 4.0F, 0.0F}, true, kStep);
    CHECK(beside.position().x == Approx(15.0F));
    CHECK(beside.position().y == Approx(4.0F));
    CHECK(beside.position().z == Approx(2.0F));
    CHECK(beside.lookAt().x == Approx(12.0F));
}

TEST_CASE("the rail camera's height setting of exactly 0 is off in mode 0", "[camera]") {
    // Expected: camera.md#rail, confirmed (code): CamRail_UpdatePosition (0x0013d6b0) step 4 tests setting 1 above 0.
    RailCamera rail = straightRail();
    rail.modify(RailCamera::kHeight, 0.0F, 0.0F);
    rail.update(Vec3{15.0F, 4.0F, 0.0F}, false, kStep);
    CHECK(rail.position().z == Approx(2.0F));
}

TEST_CASE("the rail camera does not damp its points while the target is airborne", "[camera]") {
    // Expected: camera.md#rail, confirmed (code): CamRail_DampTargetPoint (0x0013bfe0) returns at once while the first
    // target has a state flag of 0x1c00000000 (jumping or falling).
    RailCamera rail = straightRail();
    rail.update(Vec3{5.0F, 4.0F, 0.0F}, false, kStep);
    // A 3 m step in plan, damped to a quarter on the ground, is taken whole in the air.
    rail.update(Vec3{8.0F, 4.0F, 0.0F}, false, kStep, Vec3{}, true);
    CHECK(rail.targetPoint().x == Approx(8.0F));
    CHECK(rail.position().x == Approx(8.0F));
    rail.update(Vec3{11.0F, 4.0F, 0.0F}, false, kStep, Vec3{}, false);
    CHECK(rail.targetPoint().x == Approx(8.75F));
}

TEST_CASE("the rail camera's damping skips its rise tests on a segment under 1e-5 squared in plan", "[camera]") {
    // Expected: camera.md#rail, confirmed (code): CamRail_DampTargetPoint (0x0013bfe0) runs the rise tests only when
    // the segment's plan length squared is at least 1e-5.
    // A segment 2 mm long in plan (4e-6 squared) that climbs 10 m; the leading mode, which never holds at an end.
    RailCamera rail;
    RailSetup setup;
    setup.offset = Vec3{0.0F, 0.0F, 1.5F};
    rail.setup(setup);
    rail.addPoint(Vec3{0.0F, 0.0F, 2.0F});
    rail.addPoint(Vec3{0.002F, 0.0F, 12.0F});
    rail.setLead(1.0F, 0.0F, true);
    rail.update(Vec3{0.0F, 1.0F, 0.0F}, false, kStep);
    REQUIRE(rail.targetPoint().z == Approx(1.5F));
    // A rise of 0.3 m with no step in plan: the rise test would keep 0.625 of it; without it, no damping.
    rail.update(Vec3{0.0F, 1.0F, 0.3F}, false, kStep);
    CHECK(rail.targetPoint().z == Approx(1.8F));
}

TEST_CASE("a rail camera held at the start of its mode 0 placement is put back as it was, its target point too",
          "[camera]") {
    // Expected: camera.md#rail, confirmed (code): CamRail_UpdatePosition (0x0013d6b0), the hold.
    // Mode 3 holds at the rail's first point; switched back to mode 0 the hold stays.
    RailCamera rail = straightRail();
    rail.modify(RailCamera::kMode3Pitch, -7.0F, 0.0F);
    rail.activate();
    rail.update(Vec3{-5.0F, 4.0F, 0.0F}, false, kStep, Vec3{});
    rail.update(Vec3{5.0F, 4.0F, 0.0F}, false, kStep, Vec3{3.0F, 0.0F, 0.0F});
    REQUIRE(rail.held());
    const Vec3 target = rail.targetPoint();
    const Vec3 look = rail.lookAt();
    rail.modify(RailCamera::kMode3Pitch, -400.0F, 0.0F);
    REQUIRE(rail.mode() == RailMode::Level);
    rail.update(Vec3{8.0F, 4.0F, 0.0F}, false, kStep);
    CHECK(rail.held());
    CHECK(rail.position().x == Approx(0.0F));
    CHECK(rail.targetPoint().x == Approx(target.x));
    CHECK(rail.lookAt().x == Approx(look.x));
    CHECK(rail.lookAt().z == Approx(look.z));
}

TEST_CASE("the rail camera's hand-over between segments grows by 1.05 an update and lands on the rail's foot once "
          "the step is under 1 cm in plan",
          "[camera]") {
    // Expected: camera.md#rail, confirmed (code): CamRail_UpdatePosition (0x0013d6b0) step 3.
    // An L-shaped rail: along +x to (10, 0), then along +y. The target starts on the first segment and steps round
    // the corner onto the second, which starts a hand-over of 0.1.
    RailCamera rail;
    RailSetup setup;
    setup.fieldOfView = 60.0F;
    rail.setup(setup);
    rail.addPoint(Vec3{0.0F, 0.0F, 2.0F});
    rail.addPoint(Vec3{10.0F, 0.0F, 2.0F});
    rail.addPoint(Vec3{10.0F, 10.0F, 2.0F});
    rail.update(Vec3{9.7F, -0.2F, 0.0F}, false, kStep);
    REQUIRE(rail.position().x == Approx(9.7F));
    REQUIRE(rail.position().y == Approx(0.0F).margin(1e-6F));
    // The foot on the second segment, the camera's goal while the target stands there (0.39 m away: no damping).
    const Vec3 target{9.95F, 0.1F, 0.0F};
    const float footX = 10.0F;
    const float footY = 0.1F;
    // Each update moves the camera the blend's share of the way and multiplies the blend by 1.05; the update whose
    // unblended step is under 1 cm in plan (or whose grown blend reaches 1) ends it on the foot itself.
    float x = rail.position().x;
    float y = rail.position().y;
    float blend = 0.1F;
    bool blending = true;
    int updates = 0;
    while (blending && updates < 200) {
        rail.update(target, false, kStep);
        ++updates;
        const float dx = footX - x;
        const float dy = footY - y;
        const float grown = blend * 1.05F;
        if ((dx * dx) + (dy * dy) < 1e-4F || grown >= 1.0F) {
            x = footX;
            y = footY;
            blending = false;
        } else {
            x += dx * blend;
            y += dy * blend;
            blend = grown;
        }
        CHECK(rail.position().x == Approx(x).margin(1e-5F));
        CHECK(rail.position().y == Approx(y).margin(1e-5F));
    }
    CHECK_FALSE(blending);
    // The second update moves 0.105 of the way, not 0.15.
    CHECK(updates > 2);
    // Ended: the camera stays on the foot.
    rail.update(target, false, kStep);
    CHECK(rail.position().x == Approx(footX));
    CHECK(rail.position().y == Approx(footY));
}

TEST_CASE("the rail camera holds at the rail's end; its settings ease linearly; the pitch fixes the look-at point",
          "[camera]") {
    // Expected: camera.md#fn-rail, confirmed (code)
    RailCamera rail = straightRail();
    rail.update(Vec3{15.0F, 4.0F, 0.0F}, false, kStep);
    CHECK(rail.position().x == Approx(15.0F));
    CHECK_FALSE(rail.held());
    // The target runs on past the end: P is damped from 15 to 18.75, then to 21.5625 with the look-at point beyond the
    // end, so the update puts the camera, P and the look-at point back (camera.md#rail step 6); the hold lasts that
    // update only, and every later one does the same.
    for (int i = 0; i < 30; ++i) {
        rail.update(Vec3{30.0F, 4.0F, 0.0F}, false, kStep);
    }
    CHECK_FALSE(rail.held());
    CHECK(rail.position().x == Approx(18.75F));
    CHECK(rail.targetPoint().x == Approx(18.75F));
    CHECK(rail.lookAt().x == Approx(18.75F));

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
    // Expected: camera.md#rail-mode3, confirmed (code)
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
    // Expected: camera.md#rail-mode3, confirmed (code)
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
    // Expected: camera.md#fixed-third, confirmed (code)
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
    // Expected: camera.md#fn-manager, confirmed (code)
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
