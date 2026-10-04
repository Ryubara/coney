// SPDX-License-Identifier: GPL-3.0-or-later
#include "characters/character_rig.h"

#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "animation/anim_pose.h"
#include "animation/skeleton.h"
#include "characters/character_model.h"
#include "support/character_fixtures.h"

using Catch::Approx;
using coney::anim::Vec3;
using coney::test::Bytes;
using coney::test::ClumpFields;

TEST_CASE("skinning with the rest pose places every bone's vertices alike, turned into the pose's axes",
          "[character_rig]") {
    auto model = coney::characters::decodeCharacterModel(
        coney::test::characterClump(coney::test::testCharacterFields()).span(), coney::test::testBoneOffsets().span());
    REQUIRE(model.has_value());
    CHECK(coney::characters::poseBoneOfNode(*model, 1) == 3);
    const coney::anim::Skeleton skeleton = coney::characters::characterSkeleton(*model);
    coney::anim::Pose pose;
    pose.rotations = skeleton.bindRotations;
    const auto bones = coney::anim::boneTransforms(skeleton, pose);
    const auto matrices = coney::characters::skinningMatrices(*model, bones);
    REQUIRE(matrices.size() == 2);
    // At rest both skin bones map the model to the pose's axes by the same rotation: (x, y, z) -> (x, -z, y).
    CHECK(coney::anim::maxDifference(matrices[0], coney::characters::kClumpToPose) < 1e-6F);
    CHECK(coney::anim::maxDifference(matrices[1], coney::characters::kClumpToPose) < 1e-6F);
    CHECK(coney::characters::jointMismatch(*model, matrices) == Approx(0.0F).margin(1e-6));

    std::vector<Vec3> positions(model->vertices.size());
    std::vector<Vec3> normals(model->vertices.size());
    coney::characters::skinVertices(*model, matrices, positions, normals);
    // d at (1, 1, 0) in the model's axes is (1, 0, 1) in the pose's.
    CHECK(positions[3].x == Approx(1.0F));
    CHECK(positions[3].y == Approx(0.0F).margin(1e-6));
    CHECK(positions[3].z == Approx(1.0F));
    CHECK(normals[0].y == Approx(-1.0F)); // the normal along z turned to -y

    // Turning bone 3 a quarter about x leaves b, which sits on the joint, where it was, and pulls d's two bones apart.
    pose.rotations[3] = coney::anim::Quat{0.7071068F, 0.0F, 0.0F, 0.7071068F};
    const auto turned = coney::characters::skinningMatrices(*model, coney::anim::boneTransforms(skeleton, pose));
    coney::characters::skinVertices(*model, turned, positions, normals);
    CHECK(positions[1].x == Approx(1.0F));
    CHECK(coney::characters::jointMismatch(*model, turned) > 0.5F);
}
