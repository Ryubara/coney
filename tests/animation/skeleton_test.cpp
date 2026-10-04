// SPDX-License-Identifier: GPL-3.0-or-later
#include "animation/skeleton.h"

#include <cmath>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "animation/anim_pose.h"

using Catch::Approx;
using coney::anim::Quat;
using coney::anim::Vec3;

namespace {

// A rotation of `angle` radians about z.
Quat aboutZ(float angle) { return Quat{0.0F, 0.0F, std::sin(angle / 2.0F), std::cos(angle / 2.0F)}; }

} // namespace

TEST_CASE("the pose's bones chain their offsets and rotations through the parent table", "[skeleton]") {
    CHECK(coney::anim::poseBoneParent(0) == -1);
    CHECK(coney::anim::poseBoneParent(3) == 1);
    CHECK(coney::anim::poseBoneParent(33) == 32);

    coney::anim::Skeleton skeleton;
    skeleton.offsets[1] = Vec3{0, 0, 1}; // pelvis
    skeleton.offsets[2] = Vec3{0, 0, 0};
    skeleton.offsets[28] = Vec3{1, 0, 0}; // a leg under bone 2
    skeleton.offsets[29] = Vec3{1, 0, 0};
    coney::anim::Pose pose;
    pose.rotations[28] = aboutZ(1.5707963F); // turns bone 29's offset to +y
    const auto bones = coney::anim::boneTransforms(skeleton, pose);
    CHECK(bones[28].t.x == Approx(1.0F));
    CHECK(bones[28].t.z == Approx(1.0F));
    CHECK(bones[29].t.x == Approx(1.0F));
    CHECK(bones[29].t.y == Approx(1.0F));

    // With a root translation the pelvis takes it, its z raised by the skeleton's offset.
    pose.hasRootTranslation = true;
    pose.rootTranslation = Vec3{0.5F, 0, 2};
    skeleton.rootHeightOffset = 0.25F;
    const auto moved = coney::anim::boneTransforms(skeleton, pose);
    CHECK(moved[1].t.x == Approx(0.5F));
    CHECK(moved[1].t.z == Approx(2.25F));
}
