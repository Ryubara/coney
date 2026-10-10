// SPDX-License-Identifier: GPL-3.0-or-later
#include <cmath>
#include <optional>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "animation/anim_math.h"
#include "combat/throw_velocity.h"

using Catch::Approx;
using coney::anim::Vec3;
using namespace coney::combat;

TEST_CASE("a throw's weight factor: 1 for a knife, else a twentieth of the weight, halved for a big thrower",
          "[combat]") {
    CHECK(throwWeightFactor(11, 50, 1.0F) == Approx(1.0F));
    CHECK(throwWeightFactor(0, 20, 1.0F) == Approx(1.0F));  // a bottle
    CHECK(throwWeightFactor(0, 50, 1.0F) == Approx(2.5F));  // a crate or chair
    CHECK(throwWeightFactor(0, 75, 1.0F) == Approx(3.75F)); // a fridge
    CHECK(throwWeightFactor(0, 50, 1.2F) == Approx(1.25F)); // above 1.1
    CHECK(throwWeightFactor(0, 50, 1.1F) == Approx(2.5F));  // not above
    CHECK(throwWeightFactor(0, 0, 1.0F) == Approx(1.0F));   // Coney's choice: never 0
}

TEST_CASE("with no target a throw goes straight ahead with a tenth of lift at 30 / w", "[combat]") {
    const Vec3 v = throwVelocity(ThrowAim{.weightFactor = 2.5F, .toTarget = std::nullopt, .spread = std::nullopt});
    CHECK(v.x == Approx(0.0F));
    CHECK(coney::anim::length(v) == Approx(12.0F));
    CHECK(v.z / v.y == Approx(0.1F));
}

TEST_CASE("at a target a throw goes at 30 / w with the lift that brings it down there", "[combat]") {
    // A bottle at a target 15 m ahead at the hand's height: 30 m/s, flight 0.5 s, lift 7.84 * 15 / 30.
    const Vec3 v =
        throwVelocity(ThrowAim{.weightFactor = 1.0F, .toTarget = Vec3{0.0F, 15.0F, 0.0F}, .spread = std::nullopt});
    CHECK(v.x == Approx(0.0F));
    CHECK(v.y == Approx(30.0F));
    CHECK(v.z == Approx(3.92F));
    // So it is at the target's height when it is there: z(t) = v.z t - g t^2 / 2 at t = d / v.y.
    const float t = 15.0F / v.y;
    CHECK(v.z * t - (0.5F * kThrowGravity * t * t) == Approx(0.0F).margin(1e-5));
}

TEST_CASE("a target more to the side than ahead is thrown at 45 degrees; a steep one takes the default", "[combat]") {
    const Vec3 side =
        throwVelocity(ThrowAim{.weightFactor = 1.0F, .toTarget = Vec3{-10.0F, 2.0F, 0.0F}, .spread = std::nullopt});
    CHECK(side.x == Approx(-30.0F * 0.70710678F));
    CHECK(side.y == Approx(30.0F * 0.70710678F));
    const Vec3 steep =
        throwVelocity(ThrowAim{.weightFactor = 1.0F, .toTarget = Vec3{0.0F, 1.0F, 5.0F}, .spread = std::nullopt});
    const Vec3 ahead = throwVelocity(ThrowAim{.weightFactor = 1.0F, .toTarget = std::nullopt, .spread = std::nullopt});
    CHECK(steep.y == Approx(ahead.y));
    CHECK(steep.z == Approx(ahead.z));
    // From a jog or faster with an overhead or ghetto object, the default goes at 50 / w instead of 30 / w.
    const Vec3 fast = throwVelocity(
        ThrowAim{.weightFactor = 2.5F, .toTarget = std::nullopt, .spread = std::nullopt, .fastDefault = true});
    CHECK(coney::anim::length(fast) == Approx(20.0F));
    CHECK(coney::anim::length(throwVelocity(
              ThrowAim{.weightFactor = 2.5F, .toTarget = std::nullopt, .spread = std::nullopt})) == Approx(12.0F));
}

TEST_CASE("the spread turns the throw by up to 8 degrees each way, less for a near target", "[combat]") {
    const ThrowAim far{.weightFactor = 1.0F, .toTarget = Vec3{0.0F, 40.0F, 0.0F}, .spread = Vec3{1.0F, 0.0F, 0.0F}};
    const Vec3 v = throwVelocity(far);
    // About world z only: the heading turns by 8 degrees, to the left (counter-clockwise) for a positive draw.
    CHECK(std::atan2(-v.x, v.y) * 180.0F / 3.14159265F == Approx(8.0F));
    // At 6 m: a fifth of that.
    const Vec3 near = throwVelocity(
        ThrowAim{.weightFactor = 1.0F, .toTarget = Vec3{0.0F, 6.0F, 0.0F}, .spread = Vec3{1.0F, 0.0F, 0.0F}});
    CHECK(std::atan2(-near.x, near.y) * 180.0F / 3.14159265F == Approx(1.6F));
    // The second draw pitches it.
    const Vec3 up = throwVelocity(
        ThrowAim{.weightFactor = 1.0F, .toTarget = Vec3{0.0F, 40.0F, 0.0F}, .spread = Vec3{0.0F, 1.0F, 0.0F}});
    const Vec3 none =
        throwVelocity(ThrowAim{.weightFactor = 1.0F, .toTarget = Vec3{0.0F, 40.0F, 0.0F}, .spread = std::nullopt});
    CHECK(coney::anim::length(up) == Approx(coney::anim::length(none)));
    CHECK(std::fabs(std::atan2(up.z, up.y) - std::atan2(none.z, none.y)) * 180.0F / 3.14159265F == Approx(8.0F));
}

TEST_CASE("an aimed throw with no target follows the aim's pitch at 20 / w", "[combat]") {
    const Vec3 v = aimedThrowVelocity(1.0F, 0.157F);
    CHECK(v.y == Approx(20.0F * std::cos(0.157F)));
    CHECK(v.z == Approx(20.0F * std::sin(0.157F)));
}
