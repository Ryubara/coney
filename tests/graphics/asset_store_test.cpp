// SPDX-License-Identifier: GPL-3.0-or-later
#include "graphics/asset_store.h"

#include <cstdint>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "core/error.h"
#include "graphics/mesh_data.h"
#include "graphics/texture_data.h"

using coney::graphics::AssetStore;
using coney::graphics::MeshData;
using coney::graphics::MeshId;
using coney::graphics::TextureData;
using coney::graphics::TextureId;

namespace {

// A triangle with one material: the smallest mesh that holds together.
MeshData triangle() {
    MeshData mesh;
    mesh.vertices.resize(3);
    mesh.vertices[1].position = {1.0F, 0.0F, 0.0F};
    mesh.vertices[2].position = {0.0F, 1.0F, 0.0F};
    mesh.indices = {0, 1, 2};
    mesh.materials.push_back({});
    mesh.subMeshes.push_back({0, 3, 0});
    return mesh;
}

// A 2 x 2 RGBA texture with two levels (2 x 2, then 1 x 1).
TextureData twoLevels() {
    TextureData texture;
    texture.name = "t";
    texture.levels.push_back({2, 2, std::vector<std::uint8_t>(16, 7)});
    texture.levels.push_back({1, 1, std::vector<std::uint8_t>(4, 9)});
    return texture;
}

} // namespace

TEST_CASE("the asset store finds each mesh and texture by the id it handed out", "[asset_store]") {
    // Expected: unverified, #1198 (Coney's own store; the original keeps RenderWare objects by pointer)
    AssetStore store;
    auto first = store.addMesh(triangle());
    MeshData moved = triangle();
    moved.vertices[0].position = {0.0F, 0.0F, 3.0F};
    auto second = store.addMesh(moved);
    auto texture = store.addTexture(twoLevels());
    REQUIRE(first.has_value());
    REQUIRE(second.has_value());
    REQUIRE(texture.has_value());
    CHECK(first->valid());
    CHECK(*first != *second);
    CHECK(texture->valid());
    REQUIRE(store.mesh(*first) != nullptr);
    REQUIRE(store.mesh(*second) != nullptr);
    CHECK(*store.mesh(*first) == triangle());
    CHECK(store.mesh(*second)->vertices[0].position[2] == 3.0F);
    REQUIRE(store.texture(*texture) != nullptr);
    CHECK(store.texture(*texture)->levels.size() == 2);
    CHECK(store.meshCount() == 2);
    CHECK(store.textureCount() == 1);
    // Id 0 is no asset.
    CHECK(store.mesh(MeshId{}) == nullptr);
    CHECK(store.texture(TextureId{}) == nullptr);
}

TEST_CASE("the asset store never hands out a removed id again", "[asset_store]") {
    // Expected: unverified, #1198 (Coney's own store: a stale id finds nothing, never another asset)
    AssetStore store;
    auto first = store.addMesh(triangle());
    REQUIRE(first.has_value());
    CHECK(store.removeMesh(*first));
    CHECK_FALSE(store.removeMesh(*first));
    CHECK(store.mesh(*first) == nullptr);
    auto next = store.addMesh(triangle());
    REQUIRE(next.has_value());
    CHECK(*next != *first);
    CHECK(store.mesh(*first) == nullptr);

    auto texture = store.addTexture(twoLevels());
    REQUIRE(texture.has_value());
    CHECK(store.removeTexture(*texture));
    CHECK_FALSE(store.removeTexture(*texture));
    auto again = store.addTexture(twoLevels());
    REQUIRE(again.has_value());
    CHECK(*again != *texture);
    CHECK(store.meshCount() == 1);
    CHECK(store.textureCount() == 1);
}

TEST_CASE("the asset store refuses a mesh or texture that does not hold together", "[asset_store]") {
    // Expected: unverified, #1198 (Coney's own store: a renderer uploads what it finds without checking again)
    AssetStore store;
    MeshData pastTheVertices = triangle();
    pastTheVertices.indices[2] = 3;
    MeshData pastTheIndices = triangle();
    pastTheIndices.subMeshes[0].indexCount = 4;
    MeshData noMaterial = triangle();
    noMaterial.subMeshes[0].material = 1;
    MeshData threeSets = triangle();
    threeSets.attributes.texCoordSets = 3;
    for (const MeshData& mesh : {pastTheVertices, pastTheIndices, noMaterial, threeSets}) {
        auto added = store.addMesh(mesh);
        REQUIRE_FALSE(added.has_value());
        CHECK(added.error().code == coney::ErrorCode::Invalid);
    }

    TextureData noLevels = twoLevels();
    noLevels.levels.clear();
    TextureData shortTexels = twoLevels();
    shortTexels.levels[0].texels.pop_back();
    TextureData wrongHalf = twoLevels();
    wrongHalf.levels[1] = {2, 1, std::vector<std::uint8_t>(8, 0)};
    TextureData empty = twoLevels();
    empty.levels.resize(1);
    empty.levels[0] = {0, 2, {}};
    // 2^31 x 2^31 texels: four bytes each wrap a 64-bit byte count to 0, which no texels would match.
    TextureData huge = twoLevels();
    huge.levels.resize(1);
    huge.levels[0] = {0x80000000U, 0x80000000U, {}};
    // A 1 x 1 texture has one level; 33 would also shift a 32-bit side by 32.
    TextureData pastOneTexel = twoLevels();
    pastOneTexel.levels.assign(33, {1, 1, std::vector<std::uint8_t>(4, 0)});
    for (const TextureData& texture : {noLevels, shortTexels, wrongHalf, empty, huge, pastOneTexel}) {
        auto added = store.addTexture(texture);
        REQUIRE_FALSE(added.has_value());
        CHECK(added.error().code == coney::ErrorCode::Invalid);
    }
    CHECK(store.meshCount() == 0);
    CHECK(store.textureCount() == 0);
}

TEST_CASE("the mesh format's hash tells apart meshes that differ in one attribute", "[asset_store]") {
    // Expected: unverified, #1198 (Coney's own hash, printed by the disc check in place of the data)
    const MeshData base = triangle();
    CHECK(coney::graphics::hashMesh(base) == coney::graphics::hashMesh(triangle()));
    MeshData colour = base;
    colour.vertices[1].prelit.g = 0;
    MeshData uv = base;
    uv.vertices[2].texCoords[1] = {0.5F, 0.0F};
    MeshData weights = base;
    weights.vertices[0].weights[3] = 0.25F;
    MeshData texture = base;
    texture.materials[0].texture = "brick";
    MeshData strip = base;
    strip.topology = coney::graphics::MeshTopology::TriangleStrip;
    for (const MeshData& changed : {colour, uv, weights, texture, strip}) {
        CHECK(coney::graphics::hashMesh(changed) != coney::graphics::hashMesh(base));
    }
}

TEST_CASE("the texture format's hash and texel size", "[asset_store]") {
    // Expected: unverified, #1198 (Coney's own format: RGBA, four bytes a texel)
    CHECK(coney::graphics::bytesPerTexel(coney::graphics::TextureFormat::Rgba8) == 4);
    const TextureData base = twoLevels();
    CHECK(coney::graphics::hashTexture(base) == coney::graphics::hashTexture(twoLevels()));
    TextureData texel = base;
    texel.levels[1].texels[2] = 0;
    TextureData sampler = base;
    sampler.sampler.addressV = coney::graphics::TextureAddress::Clamp;
    TextureData lod = base;
    lod.lod = coney::graphics::TextureLod{-4.0F, 0};
    TextureData name = base;
    name.name = "u";
    for (const TextureData& changed : {texel, sampler, lod, name}) {
        CHECK(coney::graphics::hashTexture(changed) != coney::graphics::hashTexture(base));
    }
}
