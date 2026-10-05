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

namespace {

constexpr float kDegree = std::numbers::pi_v<float> / 180.0F;
constexpr float kStep = 1.0F / 30.0F;
constexpr std::uint8_t kRest = 128;

// The camera's elevation above its look-at point, radians.
float pitchOf(const FollowCamera& camera) {
    const Vec3 offset = coney::anim::subtract(camera.position(), camera.lookAt());
    return std::atan2(offset.z, std::hypot(offset.x, offset.y));
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

TEST_CASE("the camera starts behind the player at the leash's near edge and 13 degrees above, looking 1.4 m up",
          "[camera]") {
    const FollowCamera camera(Vec3{10.0F, 20.0F, 1.0F}, 0.0F);
    CHECK(camera.lookAt().z == Approx(2.4F));
    CHECK(coney::anim::distance(camera.position(), camera.lookAt()) == Approx(3.0F));
    CHECK(camera.position().y < 20.0F); // behind a player facing +y
    CHECK(camera.position().x == Approx(10.0F));
    CHECK(pitchOf(camera) == Approx(13.0F * kDegree));
    CHECK(camera.forward().y > 0.9F);
    // The pitch limits: -3.5° (atan((1 - 1.4) / 6.6)) and 30°.
    CHECK(camera.lowerPitch() == Approx(std::atan(-0.4F / 6.6F)));
    CHECK(camera.upperPitch() == Approx(30.0F * kDegree));
}

TEST_CASE("the leash drags the camera into its 3.0-3.5 m band, 22% of the way each update", "[camera]") {
    FollowCamera camera(Vec3{0.0F, 0.0F, 0.0F}, 0.0F);
    const Vec3 start = camera.position();
    // The player steps 0.6 m away along +y: the wanted position comes along to 3.5 m from the look-at point.
    camera.update(Vec3{0.0F, 0.6F, 0.0F}, kRest, kRest, nullptr, kStep);
    CHECK(coney::anim::distance(camera.wanted(), camera.lookAt()) == Approx(3.5F));
    const Vec3 moved = coney::anim::subtract(camera.position(), start);
    const Vec3 wanted = coney::anim::subtract(camera.wanted(), start);
    CHECK(moved.y == Approx(0.22F * wanted.y));
    // Standing still, it settles where it is inside the band.
    for (int i = 0; i < 120; ++i) {
        camera.update(Vec3{0.0F, 0.6F, 0.0F}, kRest, kRest, nullptr, kStep);
    }
    CHECK(coney::anim::distance(camera.position(), camera.lookAt()) == Approx(3.5F).margin(1e-3));
    CHECK(pitchOf(camera) == Approx(13.0F * kDegree).margin(1e-3));
}

TEST_CASE("the right stick turns the wanted position about the player at 150 deg/s", "[camera]") {
    FollowCamera camera(Vec3{0.0F, 0.0F, 0.0F}, 0.0F);
    const Vec3 before = coney::anim::subtract(camera.wanted(), camera.lookAt());
    camera.update(Vec3{}, 0, kRest, nullptr, kStep); // full left
    const Vec3 after = coney::anim::subtract(camera.wanted(), camera.lookAt());
    const float turned = std::atan2(after.y, after.x) - std::atan2(before.y, before.x);
    CHECK(turned == Approx(5.0F * kDegree).epsilon(1e-3));
    CHECK(camera.inputHold() > 0.29F);
}

TEST_CASE("the right stick moves the pitch target up to its 30 degree limit", "[camera]") {
    FollowCamera camera(Vec3{0.0F, 0.0F, 0.0F}, 0.0F);
    for (int i = 0; i < 30; ++i) {
        camera.update(Vec3{}, kRest, 0, nullptr, kStep); // full up
    }
    CHECK(camera.targetPitch() == Approx(30.0F * kDegree));
    for (int i = 0; i < 60; ++i) {
        camera.update(Vec3{}, kRest, kRest, nullptr, kStep);
    }
    CHECK(pitchOf(camera) == Approx(30.0F * kDegree).margin(2e-3));
}

TEST_CASE("a wall between the player and the camera pulls the camera in, 0.2 m short of it", "[camera]") {
    // A one-sided wall in the plane x = 40, facing -x.
    const auto mesh = coney::test::makeMesh(coney::test::wallFacingMinusX(40.0F, 0.0F, 80.0F, -5.0F, 10.0F));
    const float halfTurn = std::numbers::pi_v<float> / 2.0F;
    // A player at x = 38 facing -x: the camera, 3 m behind it at 13°, would stand at x ≈ 40.9, past the wall's face.
    // The ray from the look-at point meets the wall 2 / cos 13° along: the camera comes in to 0.2 m short of that.
    FollowCamera blocked(Vec3{38.0F, 40.0F, 0.0F}, halfTurn);
    blocked.update(Vec3{38.0F, 40.0F, 0.0F}, kRest, kRest, mesh.get(), kStep);
    CHECK(blocked.position().x < 40.0F);
    CHECK(coney::anim::distance(blocked.position(), blocked.lookAt()) ==
          Approx(2.0F / std::cos(13.0F * kDegree) - FollowCamera::kCollisionMargin).margin(1e-3));
    // A player at x = 42 facing +x puts the camera at x ≈ 39.1, behind the wall's back: a one-sided wall does not stop
    // the ray, and the camera stays 3 m out.
    FollowCamera clear(Vec3{42.0F, 40.0F, 0.0F}, -halfTurn);
    clear.update(Vec3{42.0F, 40.0F, 0.0F}, kRest, kRest, mesh.get(), kStep);
    CHECK(coney::anim::distance(clear.position(), clear.lookAt()) == Approx(3.0F).margin(1e-3));
}
