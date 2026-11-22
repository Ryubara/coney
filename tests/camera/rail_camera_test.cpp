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
#include "camera/rail_frame.h"
#include "camera/third_camera.h"

using Catch::Approx;
using coney::anim::Vec3;
using coney::camera::CameraKind;
using coney::camera::Cameras;
using coney::camera::CameraView;
using coney::camera::FixedCamera;
using coney::camera::RailCamera;
using coney::camera::RailFrameInput;
using coney::camera::RailFrameRules;
using coney::camera::RailFrameTarget;
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

TEST_CASE("the rail camera's end hold reads the camera's point after the reach pulls it off the rail", "[camera]") {
    // Expected: camera.md#rail, confirmed (code): CamRail_UpdatePosition (0x0013d6b0) step 6 tests Q, the point after
    // setting 0's pull, against the end point (3D, squared under 1e-10).
    // P past the rail's end, 4 m to the side: the foot (20, 0) is pulled to within 1 m of P in plan, so Q is not the
    // last point and the camera does not hold.
    RailCamera rail = straightRail();
    rail.modify(RailCamera::kDistance, 1.0F, 0.0F);
    rail.update(Vec3{21.0F, 4.0F, 0.0F}, false, kStep);
    rail.update(Vec3{21.3F, 4.0F, 0.0F}, false, kStep);
    const float away = std::hypot(1.3F, 4.0F);
    CHECK(rail.position().x == Approx(20.0F + (1.3F / away * (away - 1.0F))));
    CHECK(rail.position().y == Approx(4.0F / away * (away - 1.0F)));
}

TEST_CASE("the rail camera's end hold reads the look-at point as made, before the damping", "[camera]") {
    // Expected: camera.md#rail, confirmed (code): CamRail_UpdatePosition (0x0013d6b0) step 6 tests +0x300, the look-at
    // point CamRail_Update keeps as made (0x0013d464), not the damped one.
    // A look-at offset 1 m back along x: P (19.5) and the look-at point (18.5), then the feet jump 4.5 m on. Damped by
    // a quarter, P (20.625) has its foot at the end and the damped look-at point (19.625) is still short of it, but the
    // look-at point as made (23) is beyond: the camera holds and P goes back.
    RailCamera rail;
    RailSetup setup;
    setup.offset = Vec3{-1.0F, 0.0F, 1.5F};
    rail.setup(setup);
    rail.addPoint(Vec3{0.0F, 0.0F, 2.0F});
    rail.addPoint(Vec3{10.0F, 0.0F, 2.0F});
    rail.addPoint(Vec3{20.0F, 0.0F, 2.0F});
    rail.update(Vec3{19.5F, 4.0F, 0.0F}, false, kStep);
    REQUIRE(rail.targetPoint().x == Approx(19.5F));
    rail.update(Vec3{24.0F, 4.0F, 0.0F}, false, kStep);
    CHECK(rail.position().x == Approx(20.0F));
    CHECK(rail.targetPoint().x == Approx(19.5F));
    CHECK(rail.lookAt().x == Approx(18.5F));
}

TEST_CASE("the rail camera's end hold on an activation's update keeps the points it made, on the end point",
          "[camera]") {
    // Expected: camera.md#rail, confirmed (code): CamRail_Update (0x0013d010) replaces the update's start P and look-at
    // point with the fresh ones while +0x3e8 is set (0x0013d45c-0x0013d488), so a hold puts back those; the camera's
    // saved place is then its place at Q (set at 0x0013d894, copied at 0x0013db10), the end point.
    RailCamera rail = straightRail();
    rail.update(Vec3{15.0F, 4.0F, 0.0F}, false, kStep);
    rail.activate();
    rail.update(Vec3{30.0F, 4.0F, 0.0F}, false, kStep);
    CHECK(rail.position().x == Approx(20.0F));
    CHECK(rail.targetPoint().x == Approx(30.0F));
    CHECK(rail.lookAt().x == Approx(30.0F));
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
    // The target runs on past the end: P is damped from 15 to 18.75, then to 21.5625, whose foot is the last point,
    // with the look-at point beyond the end: the camera takes the end point and P and the look-at point go back
    // (camera.md#rail step 6); the hold lasts that update only, and every later one does the same.
    for (int i = 0; i < 30; ++i) {
        rail.update(Vec3{30.0F, 4.0F, 0.0F}, false, kStep);
    }
    CHECK_FALSE(rail.held());
    CHECK(rail.position().x == Approx(20.0F));
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

TEST_CASE("CamLockRail's hold keeps the rail camera in mode 0 where it stands until it is let go, through a set-up and "
          "an activation",
          "[camera]") {
    // Expected: camera.md#rail, confirmed (code): Camera_LockRail (0x0011d180) writes the hold `+0x3e4`; a held mode
    // 0 placement (0x0013d6b0) skips the rail and puts the camera back; CamRail_Reset (0x0013b2b8), Camera_SetupRail
    // (0x0011cce8) and CamRail_Activate (0x0013b508) never write it.
    RailCamera rail = straightRail();
    rail.update(Vec3{5.0F, 4.0F, 0.0F}, false, kStep);
    REQUIRE(rail.position().x == Approx(5.0F));
    rail.setHeld(true);
    CHECK(rail.held());
    for (int i = 0; i < 10; ++i) {
        rail.update(Vec3{12.0F, 4.0F, 0.0F}, false, kStep);
    }
    CHECK(rail.held());
    CHECK(rail.position().x == Approx(5.0F));
    // A set-up and an activation keep the lock.
    RailSetup setup;
    setup.offset = Vec3{0.0F, 0.0F, 1.5F};
    rail.setup(setup);
    rail.addPoint(Vec3{0.0F, 0.0F, 2.0F});
    rail.addPoint(Vec3{20.0F, 0.0F, 2.0F});
    rail.activate();
    CHECK(rail.held());
    // Let go, the camera follows its target along the rail again.
    rail.setHeld(false);
    for (int i = 0; i < 30; ++i) {
        rail.update(Vec3{12.0F, 4.0F, 0.0F}, false, kStep);
    }
    CHECK_FALSE(rail.held());
    CHECK(rail.position().x == Approx(12.0F).margin(0.05F));
}

TEST_CASE("CamLockRail's lock reaches player 1's rail camera for player 0 or any negative player, none for another",
          "[camera]") {
    // Expected: camera.md#rail, confirmed (code): Camera_LockRail (0x0011d180) reads a negative player as player 0
    // and sets nothing for a player with no rail camera (Camera_GetPlayerRail).
    Cameras cameras;
    cameras.lockRail(-1, true); // no rail camera yet: nothing
    CHECK(cameras.rail() == nullptr);
    RailSetup setup;
    setup.fieldOfView = 60.0F;
    (void)cameras.setupRail(20.0, 3.0, setup);
    REQUIRE(cameras.rail() != nullptr);
    CHECK_FALSE(cameras.rail()->held());
    cameras.lockRail(-1, true);
    CHECK(cameras.rail()->held());
    cameras.lockRail(0, false);
    CHECK_FALSE(cameras.rail()->held());
    cameras.lockRail(-5, true);
    CHECK(cameras.rail()->held());
    cameras.lockRail(1, false); // player 2 has no rail camera in Coney
    CHECK(cameras.rail()->held());
}

namespace {

// A level view from the origin along +y with a 60 degree field of view: its left edge leans tan(30 deg) across.
CameraView levelView() { return coney::camera::viewLookingAt(Vec3{}, Vec3{0.0F, 10.0F, 0.0F}, 60.0F, 0.1F, 100.0F); }

// How far `p` is inside the plane through the origin with the inward normal `n` (any length).
float insideOf(Vec3 p, Vec3 n) {
    const float length = std::sqrt((n.x * n.x) + (n.y * n.y) + (n.z * n.z));
    return ((p.x * n.x) + (p.y * n.y) + (p.z * n.z)) / length;
}

} // namespace

TEST_CASE("the rail's frame planes are the left, right and bottom edges of the camera's 4:3 view window", "[camera]") {
    // Expected: camera.md#rail-frame, confirmed (code): CamRail_KeepTargetsInFrame (0x00140d68) reads planes 0, 1 and
    // 3 of Camera_BuildFrustum (0x00120c50), through the camera, normals inward; the window is tan(fov / 2) wide and
    // three quarters of that high (0x00120a98).
    const float wide = std::tan(30.0F * std::numbers::pi_v<float> / 180.0F);
    const auto planes = coney::camera::framePlanes(levelView());
    const float across = std::sqrt(1.0F + (wide * wide));
    CHECK(planes[0].normal.x == Approx(1.0F / across));
    CHECK(planes[0].normal.y == Approx(wide / across));
    CHECK(planes[0].w == Approx(0.0F).margin(1e-6F));
    CHECK(planes[1].normal.x == Approx(-1.0F / across));
    CHECK(planes[1].normal.y == Approx(wide / across));
    const float high = 0.75F * wide;
    const float up = std::sqrt(1.0F + (high * high));
    CHECK(planes[2].normal.z == Approx(1.0F / up));
    CHECK(planes[2].normal.y == Approx(high / up));
}

TEST_CASE("the rail's keep-in-frame push moves its target just inside the view's left edge and keeps the rest of his "
          "move",
          "[camera]") {
    // Expected: camera.md#rail-frame, confirmed (code) at 0x00140d68: within the margin of the left plane, the move
    // from where he stood is split into its part along the plane's normal, which becomes the margin + 1e-5 inside,
    // and the rest, kept (no wall to cut it short); his height stays where nothing hits below.
    const float wide = std::tan(30.0F * std::numbers::pi_v<float> / 180.0F);
    const Vec3 left{1.0F, wide, 0.0F};
    const RailFrameRules rules{.offset = Vec3{0.0F, 0.0F, 0.0F}, .sideMargin = 0.5F};
    const RailFrameTarget target{.feet = Vec3{-6.0F, 5.0F, 0.0F}, .before = Vec3{-4.0F, 5.0F, 0.0F}, .radius = 0.5F};
    const std::optional<Vec3> placed = coney::camera::keepTargetInFrame(levelView(), target, rules, nullptr);
    REQUIRE(placed.has_value());
    const Vec3 at = placed.value_or(Vec3{});
    CHECK(insideOf(at, left) == Approx(0.5F).margin(1e-4F));
    CHECK(at.z == Approx(0.0F));
    // The rest of his move (across the plane's normal) is kept, less the 1e-5 the ray's stop takes off.
    const float across = std::sqrt(1.0F + (wide * wide));
    const Vec3 n{left.x / across, left.y / across, 0.0F};
    const auto along = [&n](Vec3 v) {
        const float d = (v.x * n.x) + (v.y * n.y);
        return Vec3{v.x - (n.x * d), v.y - (n.y * d), 0.0F};
    };
    const Vec3 restBefore = along(Vec3{target.feet.x - target.before.x, target.feet.y - target.before.y, 0.0F});
    const Vec3 restAfter = along(Vec3{at.x - target.before.x, at.y - target.before.y, 0.0F});
    CHECK(restAfter.x == Approx(restBefore.x).margin(1e-4F));
    CHECK(restAfter.y == Approx(restBefore.y).margin(1e-4F));

    // Well inside the frame he stays.
    const RailFrameTarget inside{.feet = Vec3{0.0F, 5.0F, 0.0F}, .before = Vec3{0.0F, 5.0F, 0.0F}};
    CHECK_FALSE(coney::camera::keepTargetInFrame(levelView(), inside, rules, nullptr).has_value());
}

TEST_CASE("the rail's keep-in-frame push lifts its target over the view's bottom edge by his radius", "[camera]") {
    // Expected: camera.md#rail-frame, confirmed (code) at 0x00140d68: within his radius of the bottom plane (3) the
    // point moves across it by the shortfall + 1e-5 along the plane's normal; his height is then kept at his feet's
    // where no ground is hit below.
    const CameraView view =
        coney::camera::viewLookingAt(Vec3{0.0F, 0.0F, 5.0F}, Vec3{0.0F, 10.0F, 0.0F}, 60.0F, 0.1F, 100.0F);
    const float high = 0.75F * std::tan(30.0F * std::numbers::pi_v<float> / 180.0F);
    const float hypot = std::sqrt(125.0F);
    const Vec3 forward{0.0F, 10.0F / hypot, -5.0F / hypot};
    const Vec3 up{0.0F, 5.0F / hypot, 10.0F / hypot};
    const Vec3 bottom{0.0F, up.y + (high * forward.y), up.z + (high * forward.z)};
    const float length = std::sqrt((bottom.y * bottom.y) + (bottom.z * bottom.z));
    const Vec3 feet{0.0F, 2.0F, 0.0F};
    const float inside = insideOf(Vec3{feet.x, feet.y, feet.z - 5.0F}, bottom);
    REQUIRE(inside < 0.5F);
    const std::optional<Vec3> placed =
        coney::camera::keepTargetInFrame(view, RailFrameTarget{.feet = feet, .before = feet, .radius = 0.5F},
                                         RailFrameRules{.offset = Vec3{}, .sideMargin = 0.5F}, nullptr);
    REQUIRE(placed.has_value());
    CHECK(placed.value_or(Vec3{}).y == Approx(feet.y + (bottom.y / length * (0.5F - inside))).margin(1e-4F));
    CHECK(placed.value_or(Vec3{}).z == Approx(0.0F));
}

TEST_CASE("the rail's keep-in-frame push in mode 3 keeps its target inside the sides by setting 7, not his radius",
          "[camera]") {
    // Expected: camera.md#rail-frame and #rail-mode3, confirmed (code) at 0x00140d68: in mode 3 the side margin is
    // setting 7 (`+0x3d4`); the Armies gang scripts set it to -10 or -15, so a target that far outside stays.
    const RailFrameTarget target{.feet = Vec3{-6.0F, 5.0F, 0.0F}, .before = Vec3{-6.0F, 5.0F, 0.0F}, .radius = 0.5F};
    const RailFrameRules loose{.offset = Vec3{}, .sideMargin = -10.0F, .trailing = true};
    const RailFrameRules tight{.offset = Vec3{}, .sideMargin = 0.5F, .trailing = true};
    CHECK_FALSE(coney::camera::keepTargetInFrame(levelView(), target, loose, nullptr).has_value());
    CHECK(coney::camera::keepTargetInFrame(levelView(), target, tight, nullptr).has_value());
}

TEST_CASE("a mode 3 rail camera pushes its target into the frame on each update after its start and trails him from "
          "there",
          "[camera]") {
    // Expected: camera.md#rail-mode3, confirmed (code): CamRail_PlaceMode3 (0x0013dcc8) calls the keeper before it
    // makes P from the target's feet, so P is made from where he is put; the start after an activation does not.
    RailCamera rail = straightRail();
    rail.modify(RailCamera::kMode3Pitch, -7.0F, 0.0F);
    rail.modify(RailCamera::kMode3Margin, 0.5F, 0.0F);
    rail.activate();
    const RailFrameInput frame{.before = Vec3{-5.0F, 4.0F, 0.0F}};
    // The start: placed level with him, no push.
    CHECK_FALSE(rail.update(Vec3{5.0F, 4.0F, 0.0F}, false, kStep, Vec3{}, false, &frame).has_value());
    // Far off the left edge: pushed right, and P is made from where he is put.
    const std::optional<Vec3> pushed = rail.update(Vec3{-5.0F, 4.0F, 0.0F}, false, kStep, Vec3{}, false, &frame);
    REQUIRE(pushed.has_value());
    CHECK(pushed.value_or(Vec3{}).x > -4.0F);
    CHECK(rail.targetPoint().x == Approx(pushed.value_or(Vec3{}).x));
}

TEST_CASE("a held mode 0 rail camera pushes its target into the frame; let go it does not", "[camera]") {
    // Expected: camera.md#rail, confirmed (code): a held mode 0 placement runs CamRail_CollideTwoTargets (0x00140708),
    // which pushes a target out of frame back in (0x00140d68); with one target and no hold CamRail_Collide runs
    // instead, which moves no one with switch 2 off.
    const RailFrameInput frame{.before = Vec3{-5.0F, 4.0F, 0.0F}};
    RailCamera rail = straightRail();
    (void)rail.update(Vec3{5.0F, 4.0F, 0.0F}, false, kStep);
    CHECK_FALSE(rail.update(Vec3{-5.0F, 4.0F, 0.0F}, false, kStep, Vec3{}, false, &frame).has_value());
    RailCamera held = straightRail();
    (void)held.update(Vec3{5.0F, 4.0F, 0.0F}, false, kStep);
    held.setHeld(true);
    const std::optional<Vec3> pushed = held.update(Vec3{-5.0F, 4.0F, 0.0F}, false, kStep, Vec3{}, false, &frame);
    REQUIRE(pushed.has_value());
    CHECK(pushed.value_or(Vec3{}).x > -5.0F);
    CHECK(held.position().x == Approx(5.0F));
}

TEST_CASE("the cameras' manager moves a held rail camera's target back into its frame", "[camera]") {
    // Expected: camera.md#rail-frame, confirmed (code) at 0x00140d68: the keeper places the target with his rotation
    // (Human_SetTransform, vtable +0x6c); Coney's manager hands the new feet to its mover.
    Cameras cameras;
    Vec3 at{5.0F, 4.0F, 0.0F};
    cameras.setPlacer([&at](double handle) -> std::optional<std::pair<Vec3, float>> {
        if (handle == 3.0) {
            return std::pair<Vec3, float>{at, 0.0F};
        }
        return std::nullopt;
    });
    std::optional<Vec3> moved;
    cameras.setMover([&moved, &at](double handle, Vec3 feet) {
        if (handle == 3.0) {
            moved = feet;
            at = feet;
        }
    });
    RailSetup setup;
    setup.fieldOfView = 60.0F;
    setup.offset = Vec3{0.0F, 0.0F, 1.5F};
    (void)cameras.setupRail(20.0, 3.0, setup);
    (void)cameras.addRailPoint(Vec3{0.0F, 0.0F, 2.0F});
    (void)cameras.addRailPoint(Vec3{20.0F, 0.0F, 2.0F});
    cameras.makeActive(20.0, 0.0F);
    cameras.update(coney::camera::FollowTarget{}, 128, 128, nullptr, kStep);
    REQUIRE_FALSE(moved.has_value());
    cameras.lockRail(0, true);
    at = Vec3{-5.0F, 4.0F, 0.0F};
    cameras.update(coney::camera::FollowTarget{}, 128, 128, nullptr, kStep);
    REQUIRE(moved.has_value());
    CHECK(moved.value_or(Vec3{}).x > -5.0F);
}
