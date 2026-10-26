// SPDX-License-Identifier: GPL-3.0-or-later
// The follow camera's own Auto Adjust option (docs/research/camera.md#heading): each camera reads its player's option
// (the byte of its pad), not a setting shared by every camera.
#include <cstdint>
#include <numbers>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "camera/follow_camera.h"

using Catch::Approx;
using coney::anim::Vec3;
using coney::camera::FollowCamera;
using coney::camera::FollowTarget;

namespace {

constexpr float kDegree = std::numbers::pi_v<float> / 180.0F;
constexpr float kStep = 1.0F / 30.0F;
constexpr std::uint8_t kRest = 128;

// A target at (40, 40, 0) facing 60° at the stored gait `gait`, on the ground.
FollowTarget moving(std::uint8_t gait) {
    FollowTarget target;
    target.feet = Vec3{40.0F, 40.0F, 0.0F};
    target.heading = 60.0F * kDegree;
    target.gait = gait;
    return target;
}

// One update of a new camera behind the target with its Auto Adjust option `on`; the turn it made.
float turnOf(bool on, std::uint8_t gait) {
    FollowCamera camera(Vec3{40.0F, 40.0F, 0.0F}, 0.0F);
    camera.setAutoAdjust(on);
    camera.update(moving(gait), kRest, kRest, nullptr, kStep);
    return camera.lastAutoTurn();
}

} // namespace

TEST_CASE("a follow camera with its own Auto Adjust off runs the default rule, other cameras and the tuning untouched",
          "[camera]") {
    // Expected: camera.md#heading, confirmed (code)
    // Off: a walk is not followed, a run at 60° turns at the default rule's 60°/s.
    CHECK(turnOf(false, coney::camera::kGaitWalk) == 0.0F);
    CHECK(turnOf(false, coney::camera::kGaitRun) == Approx(60.0F * kDegree * kStep).epsilon(1e-3));
    // On (the default): the auto-centre rule follows the walk too.
    CHECK(turnOf(true, coney::camera::kGaitWalk) != 0.0F);
    FollowCamera fresh(Vec3{40.0F, 40.0F, 0.0F}, 0.0F);
    CHECK(fresh.autoAdjust());
    // The shared tuning is the debug menus' switch, left on.
    CHECK(coney::camera::followTuning().autoCentre);
}
