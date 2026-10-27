// SPDX-License-Identifier: GPL-3.0-or-later
#include "platform/character_mesh.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <span>
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

using coney::test::bentMatrices;
using coney::test::placedTheOldWay;
using coney::test::worldNormal;
using coney::test::worldVertex;

TEST_CASE("posing a mesh whose weights sum below one places it after the blend", "[character_mesh]") {
    // Expected: characters.md#character-geometry, confirmed (code): stored weights lose their low bits and are used as
    // stored, so a vertex's weights may sum below 1 (on the disc, 71,380 of 150,509 vertices; the smallest sum
    // 0.999908); the placement then moves the blended vertex whole, as the per-vertex placement did, and the blended
    // normal is normalised, whatever its weights sum to, then turned by the placement
    auto engine = coney::platform::RenderEngine::start(coney::platform::RenderBackend::Null, {});
    REQUIRE(engine.has_value());
    auto model = coney::characters::decodeCharacterModel(
        coney::test::characterClump(coney::test::testCharacterFields()).span(), coney::test::testBoneOffsets().span());
    REQUIRE(model.has_value());
    // Every weight a thousandth short (more than the disc's, to show at a test's margin), far from the origin.
    for (coney::characters::SkinVertex& vertex : model->vertices) {
        for (float& weight : vertex.weights) {
            weight *= 0.999F;
        }
    }
    coney::platform::CharacterMesh mesh(*model, nullptr);
    const std::vector<coney::anim::Mat34> matrices = bentMatrices(*model);
    std::vector<coney::anim::Vec3> positions(model->vertices.size());
    std::vector<coney::anim::Vec3> normals(model->vertices.size());
    coney::characters::skinVertices(*model, matrices, positions, normals);
    const coney::anim::Vec3 feet{900.0F, -1200.0F, 3.0F};
    mesh.pose(matrices, coney::platform::characterPlacement(feet, 2.0F, 0.1F));
    for (rw::Atomic* atomic : {mesh.atomic(), mesh.bloodAtomic()}) {
        for (std::size_t i = 0; i < positions.size(); ++i) {
            const coney::anim::Vec3 expected = placedTheOldWay(positions[i], feet, 2.0F, 0.1F);
            const rw::V3d got = worldVertex(atomic, i);
            CHECK(got.x == Catch::Approx(expected.x).margin(1e-3));
            CHECK(got.y == Catch::Approx(expected.y).margin(1e-3));
            CHECK(got.z == Catch::Approx(expected.z).margin(1e-3));
        }
    }
    // The normals, posed again near the origin where a direction (a point less the placed origin) keeps its precision.
    const coney::anim::Vec3 nearFeet{4.0F, 5.0F, 0.0F};
    const float heading = -1.1F;
    const float lean = 0.15F;
    mesh.pose(matrices, coney::platform::characterPlacement(nearFeet, heading, lean));
    const coney::anim::Vec3 base = placedTheOldWay(coney::anim::Vec3{}, nearFeet, heading, lean);
    for (rw::Atomic* atomic : {mesh.atomic(), mesh.bloodAtomic()}) {
        for (std::size_t i = 0; i < normals.size(); ++i) {
            const coney::anim::Vec3 tip = placedTheOldWay(normals[i], nearFeet, heading, lean);
            const rw::V3d got = worldNormal(atomic, i);
            CHECK(got.x == Catch::Approx(tip.x - base.x).margin(1e-4));
            CHECK(got.y == Catch::Approx(tip.y - base.y).margin(1e-4));
            CHECK(got.z == Catch::Approx(tip.z - base.z).margin(1e-4));
            CHECK(std::sqrt((got.x * got.x) + (got.y * got.y) + (got.z * got.z)) == Catch::Approx(1.0F).margin(1e-4));
        }
    }
}

TEST_CASE("the bone palette holds each skinning matrix column by column up to 64 bones", "[character_mesh]") {
    // Expected: ../guides/performance.md#gpu-skinning, Coney's own (the shader's mat4 array, column-major as OpenGL
    // takes it; the original skins on the vector unit)
    std::vector<coney::anim::Mat34> matrices(70);
    for (std::size_t i = 0; i < matrices.size(); ++i) {
        const auto f = static_cast<float>(i);
        matrices[i] = coney::anim::Mat34{{f + 0.1F, f + 0.2F, f + 0.3F},
                                         {f + 0.4F, f + 0.5F, f + 0.6F},
                                         {f + 0.7F, f + 0.8F, f + 0.9F},
                                         {f, -f, 2.0F * f}};
    }
    coney::platform::BonePalette palette{};
    palette.fill(-7.0F);
    coney::platform::fillBonePalette(matrices, palette);
    for (std::size_t i = 0; i < coney::platform::kMaxSkinBones; ++i) {
        const coney::anim::Mat34& m = matrices[i];
        const std::array<float, coney::platform::kBoneFloats> expected{
            m.x.x, m.x.y, m.x.z, 0.0F, m.y.x, m.y.y, m.y.z, 0.0F, m.z.x, m.z.y, m.z.z, 0.0F, m.t.x, m.t.y, m.t.z, 1.0F};
        for (std::size_t k = 0; k < expected.size(); ++k) {
            CHECK(palette[(i * coney::platform::kBoneFloats) + k] == expected[k]);
        }
    }
    // Two matrices only: the rest of the palette is left alone.
    palette.fill(-7.0F);
    coney::platform::fillBonePalette(std::span(matrices).first(2), palette);
    CHECK(palette[coney::platform::kBoneFloats + 12] == matrices[1].t.x);
    CHECK(palette[2 * coney::platform::kBoneFloats] == -7.0F);
}

TEST_CASE("the skinned vertex data packs four weights and four bone indices per vertex", "[character_mesh]") {
    // Expected: characters.md#character-geometry, confirmed (code): up to four weights per vertex, each with its node
    // index, 0 in an unused slot
    std::vector<coney::characters::SkinVertex> vertices(3);
    vertices[0].weights = {0.6F, 0.4F, 0.0F, 0.0F};
    vertices[0].bones = {3, 7, 0, 0};
    vertices[1].weights = {1.0F, 0.0F, 0.0F, 0.0F};
    vertices[1].bones = {31, 0, 0, 0};
    vertices[2].weights = {0.25F, 0.25F, 0.3F, 0.2F};
    vertices[2].bones = {1, 2, 4, 8};
    const coney::platform::SkinVertexData data = coney::platform::packSkinVertices(vertices);
    REQUIRE(data.weights.size() == vertices.size());
    REQUIRE(data.indices.size() == vertices.size());
    for (std::size_t i = 0; i < vertices.size(); ++i) {
        for (std::size_t w = 0; w < 4; ++w) {
            CHECK(data.weights[i][w] == vertices[i].weights[w]);
            CHECK(data.indices[i][w] == vertices[i].bones[w]);
        }
    }
}

TEST_CASE("a mesh is skinned on the GPU only with the pipeline running and at most 64 nodes", "[character_mesh]") {
    // Expected: ../guides/performance.md#gpu-skinning, Coney's own (the shader's palette holds 64 matrices)
    using coney::platform::Skinning;
    CHECK(coney::platform::skinsOnGpu(Skinning::Gpu, true, 32));
    CHECK(coney::platform::skinsOnGpu(Skinning::Gpu, true, 64));
    CHECK_FALSE(coney::platform::skinsOnGpu(Skinning::Gpu, true, 65));
    CHECK_FALSE(coney::platform::skinsOnGpu(Skinning::Gpu, false, 32));
    CHECK_FALSE(coney::platform::skinsOnGpu(Skinning::Cpu, true, 32));
}
