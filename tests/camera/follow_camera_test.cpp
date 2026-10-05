// SPDX-License-Identifier: GPL-3.0-or-later
#include "camera/follow_camera.h"

#include <cmath>
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

// A target standing at `feet` facing +y: nothing the automatic rules follow.
FollowTarget standing(Vec3 feet) {
    return FollowTarget{.feet = feet, .heading = 0.0F, .turnsCamera = false, .running = false, .sprinting = false};
}

// A running target at `feet` facing `heading`: the auto-centre rule follows it.
FollowTarget running(Vec3 feet, float heading, bool sprinting = false) {
    return FollowTarget{.feet = feet, .heading = heading, .turnsCamera = true, .running = true, .sprinting = sprinting};
}

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

TEST_CASE("the camera starts behind the player at the leash's near edge and 13 degrees above, looking 1.4 m up",
          "[camera]") {
    const FollowCamera camera(Vec3{10.0F, 20.0F, 1.0F}, 0.0F);
    CHECK(camera.lookAt().z == Approx(2.4F));
    CHECK(coney::anim::distance(camera.position(), camera.lookAt()) == Approx(4.8F));
    CHECK(camera.position().y < 20.0F); // behind a player facing +y
    CHECK(camera.position().x == Approx(10.0F));
    CHECK(pitchOf(camera) == Approx(13.0F * kDegree));
    CHECK(camera.forward().y > 0.9F);
    // The pitch limits: -3.5° (atan((1 - 1.4) / 6.6)) and 40°.
    CHECK(camera.lowerPitch() == Approx(std::atan(-0.4F / 6.6F)));
    CHECK(camera.upperPitch() == Approx(40.0F * kDegree));
    CHECK(camera.bandNear() == Approx(4.8F));
    CHECK(camera.bandFar() == Approx(5.3F));
}

TEST_CASE("the leash drags the camera into its 4.8-5.3 m band, 22% of the way each update", "[camera]") {
    FollowCamera camera(Vec3{0.0F, 0.0F, 0.0F}, 0.0F);
    const Vec3 start = camera.position();
    // The player steps 0.6 m away along +y: the wanted position comes along to 5.3 m from the look-at point.
    camera.update(standing(Vec3{0.0F, 0.6F, 0.0F}), kRest, kRest, nullptr, kStep);
    CHECK(coney::anim::distance(camera.wanted(), camera.lookAt()) == Approx(5.3F));
    const Vec3 moved = coney::anim::subtract(camera.position(), start);
    const Vec3 wanted = coney::anim::subtract(camera.wanted(), start);
    CHECK(moved.y == Approx(0.22F * wanted.y));
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

TEST_CASE("sprinting zooms the band in to 3.0-3.5 m and the pitch to 7 degrees over 14 updates, and back after",
          "[camera]") {
    FollowCamera camera(Vec3{40.0F, 40.0F, 0.0F}, 0.0F);
    const FollowTarget sprint = running(Vec3{40.0F, 40.0F, 0.0F}, 0.0F, true);
    camera.update(sprint, kRest, kRest, nullptr, kStep);
    CHECK(camera.bandNear() == Approx(4.569F).margin(2e-3));
    CHECK(camera.targetPitch() == Approx((13.0F - 6.0F / 14.0F) * kDegree).margin(1e-4));
    for (int i = 1; i < 14; ++i) {
        camera.update(sprint, kRest, kRest, nullptr, kStep);
    }
    CHECK(camera.bandNear() == Approx(3.216F).margin(2e-3));
    CHECK(camera.targetPitch() == Approx(7.0F * kDegree).margin(1e-4));
    camera.update(sprint, kRest, kRest, nullptr, kStep);
    CHECK(camera.bandNear() == Approx(3.0F));
    CHECK(camera.bandFar() == Approx(3.5F));
    // The sprint ends: the zoom holds for 8 updates, then goes back the same way over 15.
    const FollowTarget run = running(Vec3{40.0F, 40.0F, 0.0F}, 0.0F);
    for (int i = 0; i < 8; ++i) {
        camera.update(run, kRest, kRest, nullptr, kStep);
    }
    CHECK(camera.bandNear() == Approx(3.0F));
    camera.update(run, kRest, kRest, nullptr, kStep);
    CHECK(camera.bandNear() == Approx(3.216F).margin(2e-3));
    for (int i = 0; i < 14; ++i) {
        camera.update(run, kRest, kRest, nullptr, kStep);
    }
    CHECK(camera.bandNear() == Approx(4.8F));
    CHECK(camera.targetPitch() == Approx(13.0F * kDegree));
}

TEST_CASE("after a climb's rise the look-at height eases 20% an update, then finishes in two", "[camera]") {
    FollowCamera camera(Vec3{40.0F, 40.0F, 0.0F}, 0.0F);
    // The feet rise 2.64 m at once (a wall onto a roof): the look-at point stays and then eases up.
    const Vec3 top{40.0F, 40.0F, 2.64F};
    camera.update(standing(top), kRest, kRest, nullptr, kStep);
    CHECK(camera.lookAt().z == Approx(1.4F + 0.2F * 2.64F));
    int updates = 1;
    while (camera.lookAt().z < 4.04F - 1e-4F && updates < 30) {
        camera.update(standing(top), kRest, kRest, nullptr, kStep);
        ++updates;
    }
    // 20% while more than 0.6 m is left (7 updates from 2.64 m), then the rest in 2.
    CHECK(updates == 9);
    // A jump's rise of 0.18 m an update is followed directly.
    FollowCamera jumping(Vec3{40.0F, 40.0F, 0.0F}, 0.0F);
    jumping.update(standing(Vec3{40.0F, 40.0F, 0.18F}), kRest, kRest, nullptr, kStep);
    CHECK(jumping.lookAt().z == Approx(1.58F));
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
    CHECK(coney::anim::distance(blocked.position(), blocked.lookAt()) ==
          Approx(2.0F / std::cos(13.0F * kDegree) - FollowCamera::kCollisionMargin).margin(1e-3));
    // A player at x = 42 facing +x puts the camera at x ≈ 37.3, behind the wall's back: a one-sided wall does not stop
    // the ray, and the camera stays 4.8 m out.
    FollowCamera clear(Vec3{42.0F, 40.0F, 0.0F}, -halfTurn);
    clear.update(standing(Vec3{42.0F, 40.0F, 0.0F}), kRest, kRest, mesh.get(), kStep);
    CHECK(coney::anim::distance(clear.position(), clear.lookAt()) == Approx(4.8F).margin(1e-3));
}

TEST_CASE("the camera's ray passes through a low fence (material 30) but not another wall", "[camera]") {
    // A wall across y = 40 facing +y, the player 2 m in front of it facing +y: the camera wants to stand behind it.
    constexpr std::uint8_t kLowFence = 30;
    const auto fence =
        coney::test::makeMesh(coney::test::wallFacingPlusY(40.0F, 0.0F, 80.0F, -5.0F, 10.0F, 0, kLowFence));
    const auto wall = coney::test::makeMesh(coney::test::wallFacingPlusY(40.0F, 0.0F, 80.0F, -5.0F, 10.0F));
    FollowCamera throughFence(Vec3{40.0F, 42.0F, 0.0F}, 0.0F);
    throughFence.update(standing(Vec3{40.0F, 42.0F, 0.0F}), kRest, kRest, fence.get(), kStep);
    CHECK(coney::anim::distance(throughFence.position(), throughFence.lookAt()) == Approx(4.8F).margin(1e-3));
    FollowCamera blocked(Vec3{40.0F, 42.0F, 0.0F}, 0.0F);
    blocked.update(standing(Vec3{40.0F, 42.0F, 0.0F}), kRest, kRest, wall.get(), kStep);
    CHECK(coney::anim::distance(blocked.position(), blocked.lookAt()) < 2.2F);
}
