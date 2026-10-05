// SPDX-License-Identifier: GPL-3.0-or-later
#include "camera/follow_camera.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <numbers>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "support/collision_fixtures.h"

using Catch::Approx;
using coney::anim::Vec3;
using coney::camera::FollowCamera;
using coney::camera::FollowTarget;

namespace {

constexpr float kDegree = std::numbers::pi_v<float> / 180.0F;
constexpr float kStep = 1.0F / 30.0F;
constexpr std::uint8_t kRest = 128;

// The camera's elevation above its look-at point, radians.
float pitchOf(const FollowCamera& camera) {
    const Vec3 offset = coney::anim::subtract(camera.position(), camera.lookAt());
    return std::atan2(offset.z, std::hypot(offset.x, offset.y));
}

// The heading the wanted position's view faces (0 facing +y, anticlockwise), radians.
float wantedYaw(const FollowCamera& camera) {
    const Vec3 view = coney::anim::subtract(camera.lookAt(), camera.wanted());
    return std::atan2(-view.x, view.y);
}

// A target at `feet` facing `heading` at the stored gait `gait` (0 standing, 2 walk, 4 run, 5 sprint), on the ground.
FollowTarget moving(Vec3 feet, float heading, std::uint8_t gait) {
    return FollowTarget{
        .feet = feet, .heading = heading, .gait = gait, .airborne = false, .stickBack = false, .nearestEnemy = {}};
}

// A target standing at `feet` facing +y: nothing the automatic rules follow.
FollowTarget standing(Vec3 feet) { return moving(feet, 0.0F, 0); }

// A running target at `feet` facing `heading` (gait 4, or 5 sprinting).
FollowTarget running(Vec3 feet, float heading, bool sprinting = false) {
    return moving(feet, heading, sprinting ? coney::camera::kGaitSprint : coney::camera::kGaitRun);
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

// The sprint zoom's band near edge after each update of the way in from 4.8 m (the timer rule, d × |d| / T × dt; the
// street's measured 4.569 ... 3.216, 3.0, docs/research/camera.md#street).
constexpr std::array<float, 15> kBandIn{4.5686F, 4.3793F, 4.2208F, 4.0853F, 3.9675F, 3.8635F, 3.7703F, 3.6855F,
                                        3.6072F, 3.5335F, 3.4623F, 3.3911F, 3.3146F, 3.2156F, 3.0F};

} // namespace

TEST_CASE("the right stick's yaw is 150 deg/s at the end of its travel and 60 deg/s at its dead zone's edge",
          "[camera]") {
    CHECK(coney::camera::rightStickYawRate(0) == Approx(150.0F * kDegree).epsilon(1e-3));
    CHECK(coney::camera::rightStickYawRate(64) == Approx(60.0F * kDegree).epsilon(1e-3));
    CHECK(coney::camera::rightStickYawRate(65) == 0.0F);
    CHECK(coney::camera::rightStickYawRate(kRest) == 0.0F);
    CHECK(coney::camera::rightStickYawRate(175) == 0.0F);
    CHECK(coney::camera::rightStickYawRate(176) == Approx(-60.0F * kDegree).epsilon(1e-3));
    CHECK(coney::camera::rightStickYawRate(255) == Approx(-150.0F * kDegree).epsilon(1e-3));
    // Pitch only near the ends: 85°/s to 55°/s up, -55°/s to -85°/s down.
    CHECK(coney::camera::rightStickPitchRate(0) == Approx(85.0F * kDegree));
    CHECK(coney::camera::rightStickPitchRate(8) == Approx(55.0F * kDegree));
    CHECK(coney::camera::rightStickPitchRate(9) == 0.0F);
    CHECK(coney::camera::rightStickPitchRate(231) == 0.0F);
    CHECK(coney::camera::rightStickPitchRate(232) == Approx(-55.0F * kDegree));
    CHECK(coney::camera::rightStickPitchRate(255) == Approx(-85.0F * kDegree));
}

TEST_CASE("the auto-centre rate is the original's piecewise rule of the angle to the facing", "[camera]") {
    const auto rate = [](float degrees, bool runs = false) {
        return coney::camera::autoCentreRate(degrees * kDegree, runs) / kDegree;
    };
    CHECK(rate(10.0F) == 0.0F);
    CHECK(rate(22.4F) == 0.0F);
    CHECK(rate(23.0F) < 0.0F); // just above 22.5°: a small turn the other way
    CHECK(rate(45.0F) == Approx(45.0F));
    CHECK(rate(67.0F) == Approx(98.77F).epsilon(1e-3));
    CHECK(rate(95.0F) == Approx(200.0F));
    CHECK(rate(100.0F) == Approx(200.0F).epsilon(1e-3));
    CHECK(rate(157.5F) == Approx(60.0F));
    // Beyond 157.5° only for a running target, on the same line.
    CHECK(rate(170.0F) == 0.0F);
    CHECK(rate(170.0F, true) == Approx(60.0F - 12.5F * 2.435F).epsilon(1e-3));
}

TEST_CASE("the default follow rate rises to 60 deg/s by 45 degrees and falls from 120 to 60 deg/s past 135",
          "[camera]") {
    const auto rate = [](float degrees) { return coney::camera::defaultFollowRate(degrees * kDegree) / kDegree; };
    CHECK(rate(20.0F) == 0.0F);
    CHECK(rate(30.0F) == Approx(7.5F * 2.667F).epsilon(1e-3));
    CHECK(rate(45.0F) == Approx(60.0F).epsilon(1e-3));
    CHECK(rate(90.0F) == Approx(60.0F));
    CHECK(rate(135.0F) == Approx(60.0F));
    CHECK(rate(136.0F) == Approx(120.0F - 60.0F / 22.5F).epsilon(1e-3));
    CHECK(rate(157.5F) == Approx(60.0F));
    CHECK(rate(160.0F) == 0.0F);
}

TEST_CASE("the camera starts behind the player at the leash's near edge and 13 degrees above, looking 1.4 m up",
          "[camera]") {
    const FollowCamera camera(Vec3{10.0F, 20.0F, 1.0F}, 0.0F);
    CHECK(camera.lookAt().z == Approx(2.4F));
    CHECK(coney::anim::distance(camera.position(), camera.lookAt()) == Approx(4.8F));
    CHECK(camera.position().y < 20.0F); // behind a player facing +y
    CHECK(camera.position().x == Approx(10.0F));
    CHECK(pitchOf(camera) == Approx(13.0F * kDegree));
    CHECK(camera.forward().y > 0.9F);
    // The pitch limits: -3.5° (atan((1 - 1.4) / 6.6)) and 40° (the zoom at the maximum distance, as in the street).
    CHECK(camera.lowerPitch() == Approx(std::atan(-0.4F / 6.6F)));
    CHECK(camera.zoomDistance() == Approx(6.6F));
    CHECK(camera.upperPitch() == Approx(40.0F * kDegree));
    CHECK(camera.bandNear() == Approx(4.8F));
    CHECK(camera.bandFar() == Approx(5.3F));
}

TEST_CASE("the leash drags the camera into its 4.8-5.3 m band, 22% of the way each update", "[camera]") {
    FollowCamera camera(Vec3{0.0F, 0.0F, 0.0F}, 0.0F);
    const Vec3 start = camera.position();
    // The player steps 0.3 m away along +y: the wanted position comes along to 5.3 m from the look-at point.
    camera.update(standing(Vec3{0.0F, 0.3F, 0.0F}), kRest, kRest, nullptr, kStep);
    CHECK(coney::anim::distance(camera.wanted(), camera.lookAt()) == Approx(5.093F).margin(1e-3));
    camera.update(standing(Vec3{0.0F, 0.6F, 0.0F}), kRest, kRest, nullptr, kStep);
    CHECK(coney::anim::distance(camera.wanted(), camera.lookAt()) == Approx(5.3F));
    const Vec3 moved = coney::anim::subtract(camera.position(), start);
    CHECK(moved.y > 0.0F);
    // Standing still, it settles where it is inside the band.
    for (int i = 0; i < 120; ++i) {
        camera.update(standing(Vec3{0.0F, 0.6F, 0.0F}), kRest, kRest, nullptr, kStep);
    }
    CHECK(coney::anim::distance(camera.position(), camera.lookAt()) == Approx(5.3F).margin(1e-3));
    CHECK(pitchOf(camera) == Approx(13.0F * kDegree).margin(1e-3));
}

TEST_CASE("the right stick turns the wanted position about the player at 150 deg/s", "[camera]") {
    FollowCamera camera(Vec3{0.0F, 0.0F, 0.0F}, 0.0F);
    const Vec3 before = coney::anim::subtract(camera.wanted(), camera.lookAt());
    camera.update(standing(Vec3{}), 0, kRest, nullptr, kStep); // full left
    const Vec3 after = coney::anim::subtract(camera.wanted(), camera.lookAt());
    const float turned = std::atan2(after.y, after.x) - std::atan2(before.y, before.x);
    CHECK(turned == Approx(5.0F * kDegree).epsilon(1e-3));
    CHECK(camera.inputHold() > 0.29F);
}

TEST_CASE("the right stick moves the pitch target up to its 40 degree limit", "[camera]") {
    FollowCamera camera(Vec3{0.0F, 0.0F, 0.0F}, 0.0F);
    for (int i = 0; i < 30; ++i) {
        camera.update(standing(Vec3{}), kRest, 0, nullptr, kStep); // full up
    }
    CHECK(camera.targetPitch() == Approx(40.0F * kDegree));
    for (int i = 0; i < 60; ++i) {
        camera.update(standing(Vec3{}), kRest, kRest, nullptr, kStep);
    }
    CHECK(pitchOf(camera) == Approx(40.0F * kDegree).margin(2e-3));
}

TEST_CASE("a moving player swings the camera round behind its facing by the auto-centre rule", "[camera]") {
    // The player faces 60° left of the camera's view: the rule turns the wanted position (60 - 45) × 2.444 + 45 =
    // 81.7°/s toward it, 2.72° in the first update.
    FollowCamera camera(Vec3{40.0F, 40.0F, 0.0F}, 0.0F);
    const float facing = 60.0F * kDegree;
    camera.update(running(Vec3{40.0F, 40.0F, 0.0F}, facing), kRest, kRest, nullptr, kStep);
    CHECK(camera.lastAutoTurn() == Approx(81.66F * kDegree * kStep).epsilon(1e-3));
    CHECK(wantedYaw(camera) == Approx(camera.lastAutoTurn()).margin(1e-4));
    // Standing, nothing turns it.
    FollowCamera still(Vec3{40.0F, 40.0F, 0.0F}, 0.0F);
    FollowTarget target = standing(Vec3{40.0F, 40.0F, 0.0F});
    target.heading = facing;
    still.update(target, kRest, kRest, nullptr, kStep);
    CHECK(still.lastAutoTurn() == 0.0F);
    // Kept up, the view comes round until the rule's rate falls to 0, near 26.6°.
    for (int i = 0; i < 90; ++i) {
        camera.update(running(Vec3{40.0F, 40.0F, 0.0F}, facing), kRest, kRest, nullptr, kStep);
    }
    const float left = std::abs(facing - wantedYaw(camera));
    CHECK(left < 27.0F * kDegree);
    CHECK(left > 20.0F * kDegree);
}

TEST_CASE("auto-follow runs at the walk, run and sprint gaits only, not idle, at a start clip's speed or a jog",
          "[camera]") {
    // The gaits 0-5: standing, below a walk (the walk start's 0.76 m/s), walk, jog, run, sprint.
    const float facing = 60.0F * kDegree;
    for (std::uint8_t gait = 0; gait <= 5; ++gait) {
        FollowCamera camera(Vec3{40.0F, 40.0F, 0.0F}, 0.0F);
        camera.update(moving(Vec3{40.0F, 40.0F, 0.0F}, facing, gait), kRest, kRest, nullptr, kStep);
        const bool turns = gait == 2 || gait == 4 || gait == 5;
        CHECK((camera.lastAutoTurn() != 0.0F) == turns);
    }
    // With the option off the default rule runs instead, at a run or sprint only: 60°/s at 60°.
    const TuningGuard guard;
    coney::camera::followTuning().autoCentre = false;
    FollowCamera walking(Vec3{40.0F, 40.0F, 0.0F}, 0.0F);
    walking.update(moving(Vec3{40.0F, 40.0F, 0.0F}, facing, coney::camera::kGaitWalk), kRest, kRest, nullptr, kStep);
    CHECK(walking.lastAutoTurn() == 0.0F);
    FollowCamera runs(Vec3{40.0F, 40.0F, 0.0F}, 0.0F);
    runs.update(running(Vec3{40.0F, 40.0F, 0.0F}, facing), kRest, kRest, nullptr, kStep);
    CHECK(runs.lastAutoTurn() == Approx(60.0F * kDegree * kStep).epsilon(1e-3));
}

TEST_CASE("the right stick holds the auto-centre rule off for 0.334 s", "[camera]") {
    FollowCamera camera(Vec3{40.0F, 40.0F, 0.0F}, 0.0F);
    const float facing = 80.0F * kDegree;
    camera.update(running(Vec3{40.0F, 40.0F, 0.0F}, facing), 38, kRest, nullptr, kStep); // 60% left
    CHECK(camera.lastAutoTurn() == 0.0F);
    // Released: held off for 10 more updates (the 0.334 s outlasts ten 1/30 s counts), then the rule runs again.
    int held = 0;
    while (held < 20) {
        camera.update(running(Vec3{40.0F, 40.0F, 0.0F}, facing), kRest, kRest, nullptr, kStep);
        if (camera.lastAutoTurn() != 0.0F) {
            break;
        }
        ++held;
    }
    CHECK(held == 10);
}

TEST_CASE("a blocked view stops auto-follow until the player stops; so does a stick pulled back", "[camera]") {
    // A wall across y = 37 facing +y (toward the player at y = 40): the camera, 4.8 m behind, is past it.
    const auto wall = coney::test::makeMesh(coney::test::wallFacingPlusY(37.0F, 0.0F, 80.0F, -5.0F, 10.0F));
    const float facing = 60.0F * kDegree;
    FollowCamera camera(Vec3{40.0F, 40.0F, 0.0F}, 0.0F);
    // The first update turns (nothing was blocked before it) and finds the view blocked.
    camera.update(running(Vec3{40.0F, 40.0F, 0.0F}, facing), kRest, kRest, wall.get(), kStep);
    CHECK(camera.lastAutoTurn() != 0.0F);
    CHECK(camera.viewBlocked());
    // From then on it does not turn, even with the view clear again: the latch holds while the player moves.
    CHECK(camera.viewLatched());
    for (int i = 0; i < 10; ++i) {
        camera.update(running(Vec3{40.0F, 40.0F, 0.0F}, facing), kRest, kRest, i < 2 ? wall.get() : nullptr, kStep);
        CHECK(camera.lastAutoTurn() == 0.0F);
    }
    CHECK_FALSE(camera.viewBlocked());
    CHECK(camera.viewLatched());
    // The player stops: the latch clears on the second update standing, and the next run turns the camera again.
    camera.update(standing(Vec3{40.0F, 40.0F, 0.0F}), kRest, kRest, nullptr, kStep);
    CHECK(camera.viewLatched());
    camera.update(standing(Vec3{40.0F, 40.0F, 0.0F}), kRest, kRest, nullptr, kStep);
    CHECK_FALSE(camera.viewLatched());
    camera.update(running(Vec3{40.0F, 40.0F, 0.0F}, facing), kRest, kRest, nullptr, kStep);
    CHECK(camera.lastAutoTurn() != 0.0F);
    // The stick pulled back toward the camera holds it off too.
    FollowCamera back(Vec3{40.0F, 40.0F, 0.0F}, 0.0F);
    FollowTarget target = running(Vec3{40.0F, 40.0F, 0.0F}, facing);
    target.stickBack = true;
    back.update(target, kRest, kRest, nullptr, kStep);
    CHECK(back.lastAutoTurn() == 0.0F);
}

TEST_CASE("the sprint zoom eases the band to 3.0 m and the pitch to 7 degrees over the 0.5 s timer, and back",
          "[camera]") {
    FollowCamera camera(Vec3{40.0F, 40.0F, 0.0F}, 0.0F);
    const FollowTarget sprint = running(Vec3{40.0F, 40.0F, 0.0F}, 0.0F, true);
    // The first update at the sprint gait sets the timer and the goal; nothing moves yet. The zoom steps to the
    // default distance, whose upper pitch limit is 30° with one player camera.
    camera.update(sprint, kRest, kRest, nullptr, kStep);
    CHECK(camera.sprintZoomActive());
    CHECK(camera.bandNear() == Approx(4.8F));
    CHECK(camera.targetPitch() == Approx(13.0F * kDegree));
    CHECK(camera.zoomDistance() == Approx(4.8F));
    CHECK(camera.upperPitch() == Approx(30.0F * kDegree));
    // Then the band by d × |d| / T × dt (the measured curve) and the pitch in a straight line, 6° over 14 updates.
    for (std::size_t i = 0; i < kBandIn.size(); ++i) {
        camera.update(sprint, kRest, kRest, nullptr, kStep);
        CHECK(camera.bandNear() == Approx(kBandIn.at(i)).margin(1e-3));
        CHECK(camera.bandFar() == Approx(kBandIn.at(i) + 0.5F).margin(1e-3));
        const float pitch = 13.0F - 6.0F / 14.0F * static_cast<float>(std::min<std::size_t>(i + 1, 14));
        CHECK(camera.targetPitch() == Approx(pitch * kDegree).margin(1e-4));
    }
    // The sprint ends (the run stop, at the run gait here): the zoom waits 250 ms (8 updates), then goes back the
    // same way, to the saved band, zoom and pitch, and is over on the 15th update of the way back.
    const FollowTarget run = running(Vec3{40.0F, 40.0F, 0.0F}, 0.0F);
    for (int i = 0; i < 9; ++i) {
        camera.update(run, kRest, kRest, nullptr, kStep);
        CHECK(camera.bandNear() == Approx(3.0F));
    }
    CHECK(camera.zoomDistance() == Approx(6.6F));
    CHECK(camera.upperPitch() == Approx(40.0F * kDegree));
    for (std::size_t i = 0; i < kBandIn.size(); ++i) {
        CHECK(camera.sprintZoomActive());
        camera.update(run, kRest, kRest, nullptr, kStep);
        CHECK(camera.bandNear() == Approx(4.8F + 3.0F - kBandIn.at(i)).margin(1e-3));
    }
    CHECK(camera.targetPitch() == Approx(13.0F * kDegree));
    CHECK_FALSE(camera.sprintZoomActive());
    // A run alone never zooms.
    FollowCamera runOnly(Vec3{40.0F, 40.0F, 0.0F}, 0.0F);
    for (int i = 0; i < 30; ++i) {
        runOnly.update(run, kRest, kRest, nullptr, kStep);
    }
    CHECK(runOnly.bandNear() == Approx(4.8F));
    CHECK_FALSE(runOnly.sprintZoomActive());
}

TEST_CASE("a sprint zooms in only with no enemies or the nearest within 12 m", "[camera]") {
    FollowTarget sprint = running(Vec3{40.0F, 40.0F, 0.0F}, 0.0F, true);
    // The nearest enemy 15 m away: the sprint does not zoom, however long it lasts.
    sprint.nearestEnemy = 15.0F;
    FollowCamera camera(Vec3{40.0F, 40.0F, 0.0F}, 0.0F);
    for (int i = 0; i < 20; ++i) {
        camera.update(sprint, kRest, kRest, nullptr, kStep);
    }
    CHECK_FALSE(camera.sprintZoomActive());
    CHECK(camera.bandNear() == Approx(4.8F));
    // It comes within 12 m during the same sprint: the arm has waited, and the zoom starts.
    sprint.nearestEnemy = 11.9F;
    camera.update(sprint, kRest, kRest, nullptr, kStep);
    CHECK(camera.sprintZoomActive());
    // An enemy at 12 m exactly is out of range; with none at all the sprint zooms.
    sprint.nearestEnemy = 12.0F;
    FollowCamera atRange(Vec3{40.0F, 40.0F, 0.0F}, 0.0F);
    atRange.update(sprint, kRest, kRest, nullptr, kStep);
    CHECK_FALSE(atRange.sprintZoomActive());
    sprint.nearestEnemy.reset();
    FollowCamera none(Vec3{40.0F, 40.0F, 0.0F}, 0.0F);
    none.update(sprint, kRest, kRest, nullptr, kStep);
    CHECK(none.sprintZoomActive());
}

TEST_CASE("CamEnable(5, off) stops the sprint zoom from starting and cancels one under way", "[camera]") {
    const FollowTarget sprint = running(Vec3{40.0F, 40.0F, 0.0F}, 0.0F, true);
    FollowCamera off(Vec3{40.0F, 40.0F, 0.0F}, 0.0F);
    off.enableSprintZoom(false);
    for (int i = 0; i < 20; ++i) {
        off.update(sprint, kRest, kRest, nullptr, kStep);
    }
    CHECK(off.bandNear() == Approx(4.8F));
    CHECK(off.targetPitch() == Approx(13.0F * kDegree));
    // Switched off half-way in: the band and the pitch stay where they were.
    FollowCamera cut(Vec3{40.0F, 40.0F, 0.0F}, 0.0F);
    for (int i = 0; i < 6; ++i) {
        cut.update(sprint, kRest, kRest, nullptr, kStep);
    }
    const float near = cut.bandNear();
    cut.enableSprintZoom(false);
    for (int i = 0; i < 30; ++i) {
        cut.update(running(Vec3{40.0F, 40.0F, 0.0F}, 0.0F), kRest, kRest, nullptr, kStep);
    }
    CHECK(cut.bandNear() == Approx(near));
    CHECK_FALSE(cut.sprintZoomActive());
}

TEST_CASE("with two player cameras the sprint zoom keeps the band and only lowers the pitch", "[camera]") {
    const TuningGuard guard;
    coney::camera::followTuning().onePlayerCamera = false;
    FollowCamera camera(Vec3{40.0F, 40.0F, 0.0F}, 0.0F);
    const FollowTarget sprint = running(Vec3{40.0F, 40.0F, 0.0F}, 0.0F, true);
    for (int i = 0; i < 20; ++i) {
        camera.update(sprint, kRest, kRest, nullptr, kStep);
    }
    CHECK(camera.bandNear() == Approx(4.8F));
    CHECK(camera.targetPitch() == Approx(7.0F * kDegree));
}

TEST_CASE("the look-at point's move is limited by its length: 20% beyond 0.8 m, all of it within 0.4 m", "[camera]") {
    // The feet rise 2.64 m at once (a wall onto a roof): 20% of the way while more than 0.8 m is left, then the
    // falling share 1 - 2 (d - 0.4), then all of it.
    FollowCamera camera(Vec3{40.0F, 40.0F, 0.0F}, 0.0F);
    const Vec3 top{40.0F, 40.0F, 2.64F};
    camera.update(standing(top), kRest, kRest, nullptr, kStep);
    CHECK(camera.lookAt().z == Approx(1.4F + 0.2F * 2.64F));
    float left = 2.64F * 0.8F;
    int updates = 1;
    while (left > 0.8F) {
        camera.update(standing(top), kRest, kRest, nullptr, kStep);
        left *= 0.8F;
        ++updates;
        CHECK(4.04F - camera.lookAt().z == Approx(left).margin(1e-4));
    }
    CHECK(updates == 6);
    camera.update(standing(top), kRest, kRest, nullptr, kStep);
    left -= left * (1.0F - 2.0F * (left - 0.4F));
    CHECK(4.04F - camera.lookAt().z == Approx(left).margin(1e-4));
    for (int i = 0; i < 2; ++i) {
        camera.update(standing(top), kRest, kRest, nullptr, kStep);
    }
    CHECK(camera.lookAt().z == Approx(4.04F).margin(1e-3));
    // A run's 0.26 m an update is followed in full; a 0.6 m move covers 1 - 2 × 0.2 = 60% of it.
    FollowCamera along(Vec3{40.0F, 40.0F, 0.0F}, 0.0F);
    along.update(standing(Vec3{40.0F, 40.26F, 0.0F}), kRest, kRest, nullptr, kStep);
    CHECK(along.lookAt().y == Approx(40.26F));
    along.update(standing(Vec3{40.0F, 40.86F, 0.0F}), kRest, kRest, nullptr, kStep);
    CHECK(along.lookAt().y == Approx(40.26F + 0.6F * 0.6F));
    // In the air (a jump or a fall) the point follows the feet directly.
    FollowCamera jumping(Vec3{40.0F, 40.0F, 0.0F}, 0.0F);
    FollowTarget air = standing(Vec3{40.0F, 40.0F, 1.0F});
    air.airborne = true;
    jumping.update(air, kRest, kRest, nullptr, kStep);
    CHECK(jumping.lookAt().z == Approx(2.4F));
}

TEST_CASE("the height hold eases the wanted position's height 30% an update times its scale", "[camera]") {
    // Two cameras alike but for the hold; the target jumps 1 m (the look-at point follows at once in the air).
    for (const float scale : {1.0F, 0.25F}) {
        FollowCamera free(Vec3{40.0F, 40.0F, 0.0F}, 0.0F);
        FollowCamera held(Vec3{40.0F, 40.0F, 0.0F}, 0.0F);
        const float heldAbove = held.wanted().z - held.lookAt().z;
        held.holdHeight(true, scale);
        FollowTarget air = standing(Vec3{40.0F, 40.0F, 1.0F});
        air.airborne = true;
        free.update(air, kRest, kRest, nullptr, kStep);
        held.update(air, kRest, kRest, nullptr, kStep);
        const float goal = held.lookAt().z + heldAbove;
        CHECK(held.wanted().z - free.wanted().z == Approx(0.3F * scale * (goal - free.wanted().z)).margin(1e-4));
    }
}

TEST_CASE("a wall between the player and the camera pulls the camera in, 0.2 m short of it", "[camera]") {
    // A one-sided wall in the plane x = 40, facing -x.
    const auto mesh = coney::test::makeMesh(coney::test::wallFacingMinusX(40.0F, 0.0F, 80.0F, -5.0F, 10.0F));
    const float halfTurn = std::numbers::pi_v<float> / 2.0F;
    // A player at x = 38 facing -x: the camera, 4.8 m behind it at 13°, would stand at x ≈ 42.7, past the wall's face.
    // The ray from the look-at point meets the wall 2 / cos 13° along: the camera comes in to 0.2 m short of that.
    FollowCamera blocked(Vec3{38.0F, 40.0F, 0.0F}, halfTurn);
    blocked.update(standing(Vec3{38.0F, 40.0F, 0.0F}), kRest, kRest, mesh.get(), kStep);
    CHECK(blocked.position().x < 40.0F);
    CHECK(blocked.viewBlocked());
    CHECK(coney::anim::distance(blocked.position(), blocked.lookAt()) ==
          Approx(2.0F / std::cos(13.0F * kDegree) - FollowCamera::kCollisionMargin).margin(1e-3));
    // A player at x = 42 facing +x puts the camera at x ≈ 37.3, behind the wall's back: a one-sided wall does not stop
    // the ray, and the camera stays 4.8 m out.
    FollowCamera clear(Vec3{42.0F, 40.0F, 0.0F}, -halfTurn);
    clear.update(standing(Vec3{42.0F, 40.0F, 0.0F}), kRest, kRest, mesh.get(), kStep);
    CHECK(coney::anim::distance(clear.position(), clear.lookAt()) == Approx(4.8F).margin(1e-3));
    CHECK_FALSE(clear.viewBlocked());
}

TEST_CASE("the camera's rays pass through low fences, railings and unclimbable chain-link but not another wall",
          "[camera]") {
    // A wall across y = 40 facing +y, the player 2 m in front of it facing +y: the camera wants to stand behind it.
    for (const std::uint8_t material : {std::uint8_t{30}, std::uint8_t{122}, std::uint8_t{107}}) {
        const auto fence =
            coney::test::makeMesh(coney::test::wallFacingPlusY(40.0F, 0.0F, 80.0F, -5.0F, 10.0F, 0, material));
        FollowCamera throughFence(Vec3{40.0F, 42.0F, 0.0F}, 0.0F);
        throughFence.update(standing(Vec3{40.0F, 42.0F, 0.0F}), kRest, kRest, fence.get(), kStep);
        CHECK(coney::anim::distance(throughFence.position(), throughFence.lookAt()) == Approx(4.8F).margin(1e-3));
    }
    const auto wall = coney::test::makeMesh(coney::test::wallFacingPlusY(40.0F, 0.0F, 80.0F, -5.0F, 10.0F));
    FollowCamera blocked(Vec3{40.0F, 42.0F, 0.0F}, 0.0F);
    blocked.update(standing(Vec3{40.0F, 42.0F, 0.0F}), kRest, kRest, wall.get(), kStep);
    CHECK(coney::anim::distance(blocked.position(), blocked.lookAt()) < 2.2F);
}

TEST_CASE("a panel the game switched off pulls the camera in only while the look-at point is well clear of it",
          "[camera]") {
    // A two-sided panel across y = 39.8 (x 30-50, up to 2.65 m), switched off as the game switches off doors and
    // glass: 0.2 m behind a player at y = 40 facing +y, the look-at point is under 0.5 m from it and the camera's ray
    // is cast again without it; 2 m behind, it still blocks the view.
    for (const float behind : {0.2F, 2.0F}) {
        const float y = 40.0F - behind;
        auto mesh = coney::test::makeMesh(
            coney::test::wallFacingPlusY(y, 30.0F, 50.0F, 0.0F, 2.65F, coney::raycast::kTriangleTwoSided));
        mesh->setEnabledInBox(coney::raycast::Vec3{29.0F, y - 0.1F, -1.0F}, coney::raycast::Vec3{51.0F, y + 0.1F, 3.0F},
                              false);
        FollowCamera camera(Vec3{40.0F, 40.0F, 0.0F}, 0.0F);
        camera.update(standing(Vec3{40.0F, 40.0F, 0.0F}), kRest, kRest, mesh.get(), kStep);
        CHECK(camera.viewBlocked() == (behind > 1.0F));
        if (behind < 1.0F) {
            CHECK(coney::anim::distance(camera.position(), camera.lookAt()) == Approx(4.8F).margin(1e-3));
        }
    }
}

TEST_CASE("a wall on one side swings the camera toward the side with more room", "[camera]") {
    // A wall along x = 40.9 facing -x, from y = 30 to 37: right of the camera's ray (the camera behind a player at
    // (40, 40) facing +y stands at about (40, 35.3)). The probes 2 and 3 × 7° to that side meet it, all three to the
    // other are clear.
    const auto mesh = coney::test::makeMesh(coney::test::wallFacingMinusX(40.9F, 30.0F, 37.0F, -5.0F, 10.0F));
    FollowCamera camera(Vec3{40.0F, 40.0F, 0.0F}, 0.0F);
    const float before = wantedYaw(camera);
    camera.update(standing(Vec3{40.0F, 40.0F, 0.0F}), kRest, kRest, mesh.get(), kStep);
    CHECK_FALSE(camera.viewBlocked());
    // The wanted position turns away from the wall (clockwise seen from above moves the camera to -x).
    CHECK(wantedYaw(camera) - before < -0.5F * kDegree);
    // In the open nothing swings it.
    const auto open = coney::test::makeMesh(coney::test::floorAt(0.0F, 0.0F, 80.0F, 0.0F, 80.0F));
    FollowCamera free(Vec3{40.0F, 40.0F, 0.0F}, 0.0F);
    free.update(standing(Vec3{40.0F, 40.0F, 0.0F}), kRest, kRest, open.get(), kStep);
    CHECK(wantedYaw(free) == Approx(before).margin(1e-6));
}
