// SPDX-License-Identifier: GPL-3.0-or-later
#include "animation/anim_pose.h"

#include <array>
#include <cmath>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using Catch::Approx;
using coney::anim::AnimClip;
using coney::anim::kPoseBones;
using coney::anim::PositionKey;
using coney::anim::Quat;
using coney::anim::RotationKey;
using coney::anim::Vec3;

namespace {

// A rotation of `angle` radians about z.
Quat aboutZ(float angle) { return Quat{0.0F, 0.0F, std::sin(angle / 2.0F), std::cos(angle / 2.0F)}; }

// A one-second clip: the pelvis translation moving from (0, 0, 1) at frame 0 to (3, 0, 1) at frame 30, bone 5 turning
// from 0 to 90 degrees about z by frame 30, no section A.
AnimClip testClip() {
    AnimClip clip;
    clip.name = "test";
    clip.duration = 1.0F;
    clip.boneMask = 1ULL << 5U;
    clip.rootTranslation = {PositionKey{0, Vec3{0, 0, 1}}, PositionKey{30, Vec3{3, 0, 1}}};
    clip.rotations = {{RotationKey{0, Quat{}}, RotationKey{30, aboutZ(1.5707963F)}}};
    clip.rotationBones = {5};
    return clip;
}

} // namespace

TEST_CASE("a channel is lerped between its keys and holds before the first and after the last", "[anim_pose]") {
    const std::vector<PositionKey> channel{{0, Vec3{0, 0, 0}}, {10, Vec3{10, 0, 0}}, {20, Vec3{10, 20, 0}}};
    CHECK(coney::anim::sampleChannel(channel, 5.0F).x == Approx(5.0F));
    CHECK(coney::anim::sampleChannel(channel, 15.0F).y == Approx(10.0F));
    CHECK(coney::anim::sampleChannel(channel, 20.0F).y == Approx(20.0F));
    CHECK(coney::anim::sampleChannel(channel, 99.0F).y == Approx(20.0F)); // the last key holds
    CHECK(coney::anim::sampleChannel(channel, -3.0F).x == Approx(0.0F));

    // Rotations are nlerped, the short way: a key stored as -q blends like q.
    const Quat q = aboutZ(1.0F);
    const std::vector<RotationKey> rotations{{0, Quat{}}, {10, Quat{-q.x, -q.y, -q.z, -q.w}}};
    const Quat half = coney::anim::sampleChannel(rotations, 5.0F);
    CHECK(coney::anim::dot(half, aboutZ(0.5F)) == Approx(1.0F).margin(1e-3));
}

TEST_CASE("a pose takes the clip's channels and the bind rotations elsewhere", "[anim_pose]") {
    const AnimClip clip = testClip();
    std::array<Quat, kPoseBones> bind{};
    bind[7] = aboutZ(0.3F);
    const coney::anim::Pose pose = coney::anim::samplePose(clip, 0.5F, bind);
    CHECK(pose.hasRootTranslation);
    CHECK_FALSE(pose.hasRootVelocity);
    CHECK(pose.rootTranslation.x == Approx(1.5F)); // frame 15 of 30
    CHECK(coney::anim::dot(pose.rotations[5], aboutZ(0.785398F)) == Approx(1.0F).margin(1e-3));
    CHECK(pose.rotations[7] == bind[7]);
    CHECK(pose.rotations[0] == Quat{});
    // Times outside the clip are clamped to it.
    CHECK(coney::anim::samplePose(clip, 5.0F, bind).rootTranslation.x == Approx(3.0F));
}

TEST_CASE("a partial blend only touches the subtree of its bone", "[anim_pose]") {
    coney::anim::Pose a;
    coney::anim::Pose b;
    for (Quat& q : b.rotations) {
        q = aboutZ(1.0F);
    }
    b.rootTranslation = Vec3{2, 0, 0};
    // Bone 28 is the top of a leg: 28, 29 and 30 move; its parent 2 and the other leg (31) do not.
    const coney::anim::Pose leg = coney::anim::blendPoses(a, b, 0.5F, 28);
    CHECK(coney::anim::dot(leg.rotations[30], aboutZ(0.5F)) == Approx(1.0F));
    CHECK(coney::anim::dot(leg.rotations[28], aboutZ(0.5F)) == Approx(1.0F));
    CHECK(leg.rotations[2] == Quat{});
    CHECK(leg.rotations[31] == Quat{});
    CHECK(leg.rootTranslation.x == 0.0F);
    // The whole skeleton, translations too.
    const coney::anim::Pose whole = coney::anim::blendPoses(a, b, 0.25F);
    CHECK(whole.rootTranslation.x == Approx(0.5F));
    CHECK(coney::anim::dot(whole.rotations[0], aboutZ(0.25F)) == Approx(1.0F));
}

TEST_CASE("the cursor advances by the step and hands back the overshoot at the end", "[anim_pose]") {
    const AnimClip clip = testClip();
    coney::anim::AnimCursor cursor(clip);
    CHECK(cursor.advance(1.0F / 30.0F) == 0.0F);
    CHECK(cursor.frame() == Approx(1.0F));
    CHECK(cursor.advance(0.9F, 1.0F) == 0.0F);
    const float overshoot = cursor.advance(0.2F);
    CHECK(overshoot == Approx(0.2F + 0.9F + 1.0F / 30.0F - 1.0F));
    CHECK(cursor.time() == 1.0F);
    cursor.restart(overshoot);
    CHECK(cursor.time() == Approx(overshoot));
    // The rate scales the step.
    coney::anim::AnimCursor slow(clip);
    (void)slow.advance(0.5F, 0.85F);
    CHECK(slow.time() == Approx(0.425F));
}
