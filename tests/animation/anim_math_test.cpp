// SPDX-License-Identifier: GPL-3.0-or-later
#include "animation/anim_math.h"

#include <cmath>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using Catch::Approx;
using coney::anim::Mat34;
using coney::anim::Quat;
using coney::anim::Vec3;

namespace {

// A rotation of `angle` radians about the unit axis (x, y, z).
Quat axisAngle(float x, float y, float z, float angle) {
    const float s = std::sin(angle / 2.0F);
    return Quat{x * s, y * s, z * s, std::cos(angle / 2.0F)};
}

constexpr float kQuarterTurn = 1.5707963F;

} // namespace

TEST_CASE("a quaternion turns into its rotation matrix and back", "[anim_math]") {
    // A quarter turn about z takes x to y.
    const Mat34 m = coney::anim::matrixFromQuat(axisAngle(0, 0, 1, kQuarterTurn));
    const Vec3 turned = coney::anim::transformDirection(m, Vec3{1, 0, 0});
    CHECK(turned.x == Approx(0.0F).margin(1e-6));
    CHECK(turned.y == Approx(1.0F));
    // Round trips through the matrix for rotations in every branch of the extraction.
    for (const Quat q : {axisAngle(1, 0, 0, 2.5F), axisAngle(0, 1, 0, 3.0F), axisAngle(0, 0, 1, 2.9F),
                         axisAngle(0.6F, 0.0F, 0.8F, 0.7F)}) {
        const Quat back = coney::anim::quatFromMatrix(coney::anim::matrixFromQuat(q));
        CHECK(std::abs(coney::anim::dot(back, q)) == Approx(1.0F));
    }
}

TEST_CASE("nlerp takes the short way and slerp keeps a constant speed", "[anim_math]") {
    const Quat a{};
    const Quat b = axisAngle(0, 0, 1, kQuarterTurn);
    const Quat negated{-b.x, -b.y, -b.z, -b.w}; // the same rotation as b
    const Quat viaNlerp = coney::anim::nlerp(a, negated, 0.5F);
    const Quat expected = axisAngle(0, 0, 1, kQuarterTurn / 2.0F);
    CHECK(coney::anim::dot(viaNlerp, expected) == Approx(1.0F));

    // A third of the way by slerp is a third of the angle; nlerp would be off.
    const Quat third = coney::anim::slerp(a, b, 1.0F / 3.0F);
    CHECK(coney::anim::dot(third, axisAngle(0, 0, 1, kQuarterTurn / 3.0F)) == Approx(1.0F));
    // Nearly equal rotations fall back to nlerp without dividing by a tiny sine.
    const Quat close = coney::anim::slerp(a, axisAngle(0, 0, 1, 1e-4F), 0.5F);
    CHECK(close.w == Approx(1.0F));
}

TEST_CASE("transforms compose in order and a rigid one inverts", "[anim_math]") {
    const Mat34 turn = coney::anim::transform(axisAngle(0, 0, 1, kQuarterTurn), Vec3{0, 0, 0});
    const Mat34 move = coney::anim::transform(Quat{}, Vec3{1, 0, 0});
    // Move, then turn: the origin goes to (1, 0, 0), then to (0, 1, 0).
    const Vec3 p = coney::anim::transformPoint(coney::anim::multiply(turn, move), Vec3{0, 0, 0});
    CHECK(p.x == Approx(0.0F).margin(1e-6));
    CHECK(p.y == Approx(1.0F));
    const Mat34 both = coney::anim::multiply(move, turn);
    const Mat34 identity = coney::anim::multiply(coney::anim::inverseRigid(both), both);
    CHECK(coney::anim::maxDifference(identity, Mat34{}) < 1e-6F);
    CHECK(coney::anim::lerp(Vec3{0, 0, 0}, Vec3{2, 4, 6}, 0.25F) == Vec3{0.5F, 1.0F, 1.5F});
}
