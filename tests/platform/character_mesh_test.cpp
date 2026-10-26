// SPDX-License-Identifier: GPL-3.0-or-later
#include "platform/character_mesh.h"

#include <cmath>
#include <cstddef>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <rw.h>

#include "animation/anim_pose.h"
#include "animation/skeleton.h"
#include "characters/character_model.h"
#include "characters/character_rig.h"
#include "platform/render_engine.h"
#include "support/character_fixtures.h"
#include "support/character_mesh_fixtures.h"

using coney::test::placedTheOldWay;
using coney::test::worldVertex;

TEST_CASE("a character model becomes librw geometry whose vertices follow the skinning", "[character_mesh]") {
    // Expected: characters.md#character-geometry, confirmed (code): the fixture's vertices, as decoded
    // librw on its NULL device: no window or GPU.
    auto engine = coney::platform::RenderEngine::start(coney::platform::RenderBackend::Null, {});
    REQUIRE(engine.has_value());
    auto model = coney::characters::decodeCharacterModel(
        coney::test::characterClump(coney::test::testCharacterFields()).span(), coney::test::testBoneOffsets().span());
    REQUIRE(model.has_value());
    {
        coney::platform::CharacterMesh mesh(*model, nullptr);
        rw::Geometry* geometry = mesh.atomic()->geometry;
        CHECK(geometry->numVertices == 4);
        CHECK(geometry->numTriangles == 2);
        CHECK(geometry->matList.numMaterials == 1);
        CHECK(geometry->morphTargets[0].vertices[1].x == 1.0F);

        std::vector<coney::anim::Vec3> positions(4, coney::anim::Vec3{0.0F, 0.0F, 5.0F});
        std::vector<coney::anim::Vec3> normals(4, coney::anim::Vec3{0.0F, 0.0F, 1.0F});
        mesh.update(positions, normals);
        CHECK(geometry->morphTargets[0].vertices[3].z == 5.0F);
        CHECK(geometry->morphTargets[0].boundingSphere.center.z == 5.0F);
    }
}

TEST_CASE("the placement turns, leans and moves a point as the per-vertex placement did", "[character_mesh]") {
    // Expected: characters.md#coneys-implementation, Coney's choice of the lean's axis and pivot: the play mode's
    // per-vertex placement before GPU skinning, placedTheOldWay()
    const coney::anim::Vec3 feet{3.0F, -2.0F, 0.5F};
    const float heading = 0.7F;
    const float lean = -0.2F;
    const coney::anim::Mat34 placement = coney::platform::characterPlacement(feet, heading, lean);
    for (const coney::anim::Vec3 v : {coney::anim::Vec3{0.0F, 0.0F, 0.0F}, coney::anim::Vec3{1.0F, 2.0F, 3.0F},
                                      coney::anim::Vec3{-0.5F, 0.25F, 1.8F}}) {
        const coney::anim::Vec3 expected = placedTheOldWay(v, feet, heading, lean);
        const coney::anim::Vec3 got = coney::anim::transformPoint(placement, v);
        CHECK(got.x == Catch::Approx(expected.x).margin(1e-5));
        CHECK(got.y == Catch::Approx(expected.y).margin(1e-5));
        CHECK(got.z == Catch::Approx(expected.z).margin(1e-5));
    }
}

TEST_CASE("posing a mesh on the CPU skins and places every vertex, the blood layer too", "[character_mesh]") {
    // Expected: characters.md#character-geometry, confirmed (code): characters::skinVertices(), then the placement
    auto engine = coney::platform::RenderEngine::start(coney::platform::RenderBackend::Null, {});
    REQUIRE(engine.has_value());
    auto model = coney::characters::decodeCharacterModel(
        coney::test::characterClump(coney::test::testCharacterFields()).span(), coney::test::testBoneOffsets().span());
    REQUIRE(model.has_value());
    // No skinned pipeline without OpenGL: a mesh asking for the GPU is skinned on the CPU.
    coney::platform::CharacterMesh mesh(*model, nullptr, coney::platform::Skinning::Gpu);
    CHECK_FALSE(mesh.gpuSkinned());
    const coney::anim::Skeleton skeleton = coney::characters::characterSkeleton(*model);
    coney::anim::Pose pose;
    pose.rotations = skeleton.bindRotations;
    const auto matrices = coney::characters::skinningMatrices(*model, coney::anim::boneTransforms(skeleton, pose));
    std::vector<coney::anim::Vec3> positions(model->vertices.size());
    std::vector<coney::anim::Vec3> normals(model->vertices.size());
    coney::characters::skinVertices(*model, matrices, positions, normals);
    const coney::anim::Vec3 feet{1.0F, 2.0F, 0.0F};
    mesh.pose(matrices, coney::platform::characterPlacement(feet, 0.5F, 0.0F));
    for (rw::Atomic* atomic : {mesh.atomic(), mesh.bloodAtomic()}) {
        for (std::size_t i = 0; i < positions.size(); ++i) {
            const coney::anim::Vec3 expected = placedTheOldWay(positions[i], feet, 0.5F, 0.0F);
            const rw::V3d got = worldVertex(atomic, i);
            CHECK(got.x == Catch::Approx(expected.x).margin(1e-5));
            CHECK(got.y == Catch::Approx(expected.y).margin(1e-5));
            CHECK(got.z == Catch::Approx(expected.z).margin(1e-5));
        }
        CHECK(atomic->boundingSphere.radius > 0.0F);
    }
}

TEST_CASE("the posed box of the bones' boxes holds every skinned vertex", "[character_mesh]") {
    // Expected: characters.md#character-geometry, confirmed (code): characters::skinVertices() on the same matrices
    auto model = coney::characters::decodeCharacterModel(
        coney::test::characterClump(coney::test::testCharacterFields()).span(), coney::test::testBoneOffsets().span());
    REQUIRE(model.has_value());
    const std::vector<coney::platform::BoneBox> boxes = coney::platform::boneBoxes(*model);
    REQUIRE(boxes.size() == model->clump.skin.inverseBind.size());
    const coney::anim::Skeleton skeleton = coney::characters::characterSkeleton(*model);
    coney::anim::Pose pose;
    pose.rotations = skeleton.bindRotations;
    const auto matrices = coney::characters::skinningMatrices(*model, coney::anim::boneTransforms(skeleton, pose));
    // A turn of 30 degrees on the second node, so the two bones disagree.
    std::vector<coney::anim::Mat34> placed(matrices.begin(), matrices.end());
    const float c = std::cos(0.52F);
    const float s = std::sin(0.52F);
    placed.back() = coney::anim::multiply(
        coney::anim::Mat34{{c, s, 0.0F}, {-s, c, 0.0F}, {0.0F, 0.0F, 1.0F}, {0.2F, 0.0F, 0.0F}}, placed.back());
    std::vector<coney::anim::Vec3> positions(model->vertices.size());
    std::vector<coney::anim::Vec3> normals(model->vertices.size());
    coney::characters::skinVertices(*model, placed, positions, normals);
    const coney::platform::BoneBox box = coney::platform::posedBox(boxes, placed);
    REQUIRE_FALSE(box.empty);
    for (const coney::anim::Vec3& p : positions) {
        CHECK(p.x >= box.min.x - 1e-5F);
        CHECK(p.y >= box.min.y - 1e-5F);
        CHECK(p.z >= box.min.z - 1e-5F);
        CHECK(p.x <= box.max.x + 1e-5F);
        CHECK(p.y <= box.max.y + 1e-5F);
        CHECK(p.z <= box.max.z + 1e-5F);
    }
}
