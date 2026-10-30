// SPDX-License-Identifier: GPL-3.0-or-later
#include "platform/asset_convert.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string_view>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <rw.h>

#include "core/error.h"
#include "graphics/asset_store.h"
#include "graphics/mesh_data.h"
#include "graphics/texture_data.h"
#include "platform/character_skin.h"
#include "platform/render_engine.h"
#include "platform/texture_dictionary.h"
#include "support/rw_fixtures.h"

// These tests run librw on its NULL device (RenderBackend::Null): no window, no GPU, as on CI. Every geometry and
// raster is built here by hand, through librw's own calls; nothing comes from the game (LEGAL.md).

using coney::graphics::MeshData;
using coney::graphics::TextureData;
using coney::platform::meshFromRw;
using coney::platform::textureFromRw;

namespace {

// Starts the headless renderer, which every test here needs for librw.
std::unique_ptr<coney::platform::RenderEngine> startHeadless() {
    auto engine = coney::platform::RenderEngine::start(coney::platform::RenderBackend::Null, {});
    REQUIRE(engine.has_value());
    return std::move(*engine);
}

// Writes `text` into librw's fixed-size texture name, cut to fit and always terminated.
void setName(rw::Texture& texture, std::string_view text) {
    const std::size_t length = std::min(text.size(), sizeof(texture.name) - 1);
    std::memcpy(texture.name, text.data(), length);
    texture.name[length] = '\0';
}

// A material of `colour`, textured with an empty texture named `texture` when it is not empty. The caller destroys it.
rw::Material* material(rw::RGBA colour, const char* texture) {
    rw::Material* made = rw::Material::create();
    made->color = colour;
    if (*texture != '\0') {
        rw::Texture* named = rw::Texture::create(nullptr);
        setName(*named, texture);
        made->setTexture(named);
        named->destroy(); // the material holds its own reference
    }
    return made;
}

// Four vertices in a unit square with normals, prelighting and two texture-coordinate sets, as two triangles, each of
// its own material: the first red and textured "brick", the second blue and untextured. `flags` adds TRISTRIP.
rw::Geometry* square(rw::uint32 flags) {
    rw::Geometry* geometry = rw::Geometry::create(
        4, 2, flags | rw::Geometry::POSITIONS | rw::Geometry::NORMALS | rw::Geometry::PRELIT | rw::Geometry::TEXTURED2);
    rw::MorphTarget& target = geometry->morphTargets[0];
    for (rw::int32 i = 0; i < 4; ++i) {
        const auto f = static_cast<float>(i);
        target.vertices[i] = rw::makeV3d(static_cast<float>(i & 1), static_cast<float>(i >> 1), 0.5F * f);
        target.normals[i] = rw::makeV3d(0.0F, -1.0F, 0.0F);
        geometry->colors[i] = rw::makeRGBA(static_cast<rw::uint8>(10 * i), 20, 30, static_cast<rw::uint8>(200 + i));
        geometry->texCoords[0][i] = rw::TexCoords{0.25F * f, 1.0F - 0.25F * f};
        geometry->texCoords[1][i] = rw::TexCoords{2.0F + f, -f};
    }
    geometry->triangles[0].v[0] = 0;
    geometry->triangles[0].v[1] = 1;
    geometry->triangles[0].v[2] = 2;
    geometry->triangles[0].matId = 0;
    geometry->triangles[1].v[0] = 2;
    geometry->triangles[1].v[1] = 1;
    geometry->triangles[1].v[2] = 3;
    geometry->triangles[1].matId = 1;
    for (rw::Material* m :
         {material(rw::makeRGBA(255, 0, 0, 255), "brick"), material(rw::makeRGBA(0, 0, 255, 128), "")}) {
        geometry->matList.appendMaterial(m);
        m->destroy(); // the list holds its own reference
    }
    geometry->buildMeshes();
    geometry->calculateBoundingSphere();
    return geometry;
}

// The first texture of a dictionary read from `stream`, kept alive by `dictionary`.
rw::Texture* firstTexture(const coney::test::Bytes& stream,
                          std::unique_ptr<coney::platform::TextureDictionary>& dictionary) {
    auto read = coney::platform::TextureDictionary::read(stream.span());
    REQUIRE(read.has_value());
    dictionary = std::make_unique<coney::platform::TextureDictionary>(std::move(*read));
    REQUIRE(dictionary->textures().size() == 1);
    return dictionary->textures()[0];
}

// What librw reads of `raster`'s level 0, as the GL3 path's images are made: its image expanded to 32 bits, RGBA rows.
std::vector<std::uint8_t> librwLevel0(rw::Raster* raster) {
    rw::Image* image = raster->toImage();
    REQUIRE(image != nullptr);
    image->convertTo32();
    std::vector<std::uint8_t> texels;
    for (rw::int32 y = 0; y < image->height; ++y) {
        const rw::uint8* row = image->pixels + static_cast<std::ptrdiff_t>(y) * image->stride;
        texels.insert(texels.end(), row, row + static_cast<std::ptrdiff_t>(image->width) * 4);
    }
    image->destroy();
    return texels;
}

} // namespace

TEST_CASE("the mesh format takes a geometry's vertices, attributes and meshes as librw holds them", "[asset_convert]") {
    // Expected: formats/renderware.md#clump, confirmed (code): what librw holds of the hand-built geometry
    auto engine = startHeadless();
    rw::Geometry* geometry = square(0);
    auto mesh = meshFromRw(*geometry);
    REQUIRE(mesh.has_value());
    CHECK(coney::graphics::validateMesh(*mesh).has_value());
    CHECK(mesh->attributes == coney::graphics::MeshAttributes{true, true, 2, false});
    CHECK(mesh->topology == coney::graphics::MeshTopology::TriangleList);
    REQUIRE(mesh->vertices.size() == 4);
    for (std::size_t i = 0; i < 4; ++i) {
        const auto& v = mesh->vertices[i];
        const auto f = static_cast<float>(i);
        CHECK(v.position == std::array<float, 3>{static_cast<float>(i & 1), static_cast<float>(i >> 1), 0.5F * f});
        CHECK(v.normal == std::array<float, 3>{0.0F, -1.0F, 0.0F});
        CHECK(v.prelit ==
              coney::graphics::Rgba{static_cast<std::uint8_t>(10 * i), 20, 30, static_cast<std::uint8_t>(200 + i)});
        CHECK(v.texCoords[0] == std::array<float, 2>{0.25F * f, 1.0F - 0.25F * f});
        CHECK(v.texCoords[1] == std::array<float, 2>{2.0F + f, -f});
        CHECK(v.weights == std::array<float, 4>{});
    }
    // One sub-mesh per librw mesh, in the mesh header's order, over the header's indices.
    REQUIRE(mesh->subMeshes.size() == 2);
    CHECK(mesh->indices == std::vector<std::uint16_t>{0, 1, 2, 2, 1, 3});
    CHECK(mesh->subMeshes[0] == coney::graphics::SubMesh{0, 3, 0});
    CHECK(mesh->subMeshes[1] == coney::graphics::SubMesh{3, 3, 1});
    REQUIRE(mesh->materials.size() == 2);
    CHECK(mesh->materials[0] == coney::graphics::MeshMaterial{{255, 0, 0, 255}, "brick"});
    CHECK(mesh->materials[1] == coney::graphics::MeshMaterial{{0, 0, 255, 128}, ""});
    const rw::Sphere& sphere = geometry->morphTargets[0].boundingSphere;
    CHECK(mesh->bounds.centre == std::array<float, 3>{sphere.center.x, sphere.center.y, sphere.center.z});
    CHECK(mesh->bounds.radius == sphere.radius);
    CHECK(mesh->bounds.radius > 0.0F);
    geometry->destroy();
}

TEST_CASE("the mesh format keeps a strip geometry's strips as librw built them", "[asset_convert]") {
    // Expected: formats/renderware.md#native-geometry, confirmed (code): strips stay strips, the header's indices as
    // they are
    auto engine = startHeadless();
    rw::Geometry* geometry = square(rw::Geometry::TRISTRIP);
    REQUIRE((geometry->meshHeader->flags & rw::MeshHeader::TRISTRIP) != 0);
    auto mesh = meshFromRw(*geometry);
    REQUIRE(mesh.has_value());
    CHECK(mesh->topology == coney::graphics::MeshTopology::TriangleStrip);
    rw::Mesh* meshes = geometry->meshHeader->getMeshes();
    REQUIRE(mesh->subMeshes.size() == geometry->meshHeader->numMeshes);
    std::vector<std::uint16_t> expected;
    for (std::size_t m = 0; m < mesh->subMeshes.size(); ++m) {
        CHECK(mesh->subMeshes[m].firstIndex == expected.size());
        CHECK(mesh->subMeshes[m].indexCount == meshes[m].numIndices);
        expected.insert(expected.end(), meshes[m].indices, meshes[m].indices + meshes[m].numIndices);
    }
    CHECK(mesh->indices == expected);
    geometry->destroy();
}

TEST_CASE("the mesh format takes each vertex's bones and weights from the skin", "[asset_convert]") {
    // Expected: characters.md#character-geometry, confirmed (code): four weights with their bones per vertex
    auto engine = startHeadless();
    rw::Geometry* geometry = square(0);
    coney::platform::SkinVertexData skin;
    for (std::uint8_t i = 0; i < 4; ++i) {
        skin.weights.push_back({0.5F, 0.25F, 0.25F, 0.0F});
        skin.indices.push_back({i, static_cast<std::uint8_t>(i + 1), 7, 0});
    }
    auto mesh = meshFromRw(*geometry, &skin);
    REQUIRE(mesh.has_value());
    CHECK(mesh->attributes.skinned);
    REQUIRE(mesh->vertices.size() == 4);
    CHECK(mesh->vertices[2].bones == std::array<std::uint8_t, 4>{2, 3, 7, 0});
    CHECK(mesh->vertices[2].weights == std::array<float, 4>{0.5F, 0.25F, 0.25F, 0.0F});

    skin.weights.pop_back();
    auto wrong = meshFromRw(*geometry, &skin);
    REQUIRE_FALSE(wrong.has_value());
    CHECK(wrong.error().code == coney::ErrorCode::Invalid);
    geometry->destroy();
}

TEST_CASE("the mesh format refuses geometry that is still PS2 native", "[asset_convert]") {
    // Expected: formats/renderware.md#native-geometry, confirmed (code): native geometry holds no plain vertices
    auto engine = startHeadless();
    rw::Geometry* geometry = rw::Geometry::create(3, 1, rw::Geometry::POSITIONS | rw::Geometry::NATIVE);
    auto mesh = meshFromRw(*geometry);
    REQUIRE_FALSE(mesh.has_value());
    CHECK(mesh.error().code == coney::ErrorCode::Invalid);
    geometry->destroy();
}

TEST_CASE("the texture format reads a PS2 32-bit raster's texels as librw does", "[asset_convert]") {
    // Expected: formats/renderware.md#palette, confirmed (code): alpha 0x80 is 255 and 0x40 is 127, as
    // TextureDictionary::toImages() reads the same fixture
    auto engine = startHeadless();
    std::unique_ptr<coney::platform::TextureDictionary> dictionary;
    rw::Texture* texture = firstTexture(coney::test::texDictionary({coney::test::rgbaTexture2x2("wall")}), dictionary);
    auto converted = textureFromRw(*texture);
    REQUIRE(converted.has_value());
    CHECK(coney::graphics::validateTexture(*converted).has_value());
    CHECK(converted->name == "wall");
    CHECK(converted->format == coney::graphics::TextureFormat::Rgba8);
    REQUIRE(converted->levels.size() == 1);
    CHECK(converted->levels[0].width == 2);
    CHECK(converted->levels[0].height == 2);
    const std::vector<std::uint8_t> expected{255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 255, 255, 255, 127};
    CHECK(converted->levels[0].texels == expected);
    auto images = dictionary->toImages();
    REQUIRE(images.has_value());
    CHECK((*images)[0].pixels == converted->levels[0].texels);
    // The fixture's filter word: linear, wrap in U and V; its K value 0xFC0 is -4.0 (graphics/texture_lod.h).
    CHECK(converted->sampler == coney::graphics::TextureSampler{});
    REQUIRE(converted->lod.has_value());
    CHECK(converted->lod->k == -4.0F);
    CHECK(converted->lod->l == 0);

    texture->setFilter(rw::Texture::LINEARMIPLINEAR);
    texture->setAddressU(rw::Texture::CLAMP);
    texture->setAddressV(rw::Texture::MIRROR);
    auto resampled = textureFromRw(*texture);
    REQUIRE(resampled.has_value());
    CHECK(resampled->sampler == coney::graphics::TextureSampler{coney::graphics::TextureFilter::LinearMipLinear,
                                                                coney::graphics::TextureAddress::Clamp,
                                                                coney::graphics::TextureAddress::Mirror});
}

TEST_CASE("the texture format expands a PS2 8-bit palette raster as librw does", "[asset_convert]") {
    // Expected: formats/renderware.md#palette, confirmed (code): each texel its palette colour, alpha scaled from the
    // PS2's 0 to 128
    auto engine = startHeadless();
    constexpr rw::int32 kSide = 16;
    rw::Image* image = rw::Image::create(kSide, kSide, 8);
    image->allocate();
    for (int i = 0; i < 256; ++i) {
        image->palette[i * 4 + 0] = static_cast<rw::uint8>(i);
        image->palette[i * 4 + 1] = static_cast<rw::uint8>(255 - i);
        image->palette[i * 4 + 2] = static_cast<rw::uint8>(i * 7);
        image->palette[i * 4 + 3] = (i & 1) != 0 ? 255 : 0;
    }
    for (rw::int32 y = 0; y < kSide; ++y) {
        for (rw::int32 x = 0; x < kSide; ++x) {
            image->pixels[y * image->stride + x] = static_cast<rw::uint8>((x * 13 + y * 5) & 0xFF);
        }
    }
    rw::Raster* raster = rw::Raster::create(kSide, kSide, 8, rw::Raster::PAL8 | rw::Raster::C8888 | rw::Raster::TEXTURE,
                                            rw::PLATFORM_PS2);
    REQUIRE(raster != nullptr);
    // The platform is named: librw takes it from the argument (default: the running device), not from the raster.
    REQUIRE(raster->setFromImage(image, rw::PLATFORM_PS2) != nullptr);
    rw::Texture* texture = rw::Texture::create(raster);
    setName(*texture, "sign");

    auto converted = textureFromRw(*texture);
    REQUIRE(converted.has_value());
    REQUIRE(converted->levels.size() == 1);
    const coney::graphics::TextureLevel& level = converted->levels[0];
    CHECK(level.width == kSide);
    CHECK(level.height == kSide);
    std::vector<std::uint8_t> expected;
    for (rw::int32 y = 0; y < kSide; ++y) {
        for (rw::int32 x = 0; x < kSide; ++x) {
            const int index = image->pixels[y * image->stride + x];
            expected.insert(expected.end(), image->palette + index * 4, image->palette + index * 4 + 4);
        }
    }
    CHECK(level.texels == expected);
    // The raster is left as it was: librw reads the same texels after the conversion, and a second conversion matches.
    CHECK(librwLevel0(raster) == expected);
    auto again = textureFromRw(*texture);
    REQUIRE(again.has_value());
    CHECK(again->levels == converted->levels);
    image->destroy();
    texture->destroy();
}

TEST_CASE("the texture format reads every mip level of a PS2 raster once", "[asset_convert]") {
    // Expected: formats/renderware.md#gs-packets, confirmed (code): one packet per level, each half the one before
    auto engine = startHeadless();
    constexpr rw::int32 kSide = 16;
    rw::Raster* raster = rw::Raster::create(
        kSide, kSide, 32, rw::Raster::C8888 | rw::Raster::MIPMAP | rw::Raster::TEXTURE, rw::PLATFORM_PS2);
    REQUIRE(raster != nullptr);
    const rw::int32 levels = raster->getNumLevels();
    REQUIRE(levels > 1);
    // Each level's texels: red the level, green the texel's number, blue 9, PS2 alpha 0x80 (opaque).
    for (rw::int32 level = 0; level < levels; ++level) {
        rw::uint8* texels = raster->lock(level, rw::Raster::LOCKWRITE | rw::Raster::LOCKNOFETCH);
        const rw::int32 count = raster->width * raster->height;
        for (rw::int32 i = 0; i < count; ++i) {
            texels[i * 4 + 0] = static_cast<rw::uint8>(level);
            texels[i * 4 + 1] = static_cast<rw::uint8>(i);
            texels[i * 4 + 2] = 9;
            texels[i * 4 + 3] = 0x80;
        }
        raster->unlock(level);
    }
    rw::Texture* texture = rw::Texture::create(raster);

    auto converted = textureFromRw(*texture);
    REQUIRE(converted.has_value());
    CHECK(coney::graphics::validateTexture(*converted).has_value());
    REQUIRE(converted->levels.size() == static_cast<std::size_t>(levels));
    for (std::size_t level = 0; level < converted->levels.size(); ++level) {
        const coney::graphics::TextureLevel& got = converted->levels[level];
        const auto side = static_cast<std::uint32_t>(kSide) >> level;
        CHECK(got.width == side);
        CHECK(got.height == side);
        std::vector<std::uint8_t> expected;
        for (std::uint32_t i = 0; i < side * side; ++i) {
            expected.insert(expected.end(), {static_cast<std::uint8_t>(level), static_cast<std::uint8_t>(i), 9, 255});
        }
        CHECK(got.texels == expected);
    }
    texture->destroy();
}

TEST_CASE("the mesh and texture formats go into the asset store as converted", "[asset_convert]") {
    // Expected: unverified, #1198 (Coney's own store, holding what the conversion made)
    auto engine = startHeadless();
    rw::Geometry* geometry = square(0);
    std::unique_ptr<coney::platform::TextureDictionary> dictionary;
    rw::Texture* texture = firstTexture(coney::test::texDictionary({coney::test::rgbaTexture2x2("wall")}), dictionary);
    auto mesh = meshFromRw(*geometry);
    auto converted = textureFromRw(*texture);
    REQUIRE(mesh.has_value());
    REQUIRE(converted.has_value());
    coney::graphics::AssetStore store;
    auto meshId = store.addMesh(*mesh);
    auto textureId = store.addTexture(*converted);
    REQUIRE(meshId.has_value());
    REQUIRE(textureId.has_value());
    REQUIRE(store.mesh(*meshId) != nullptr);
    CHECK(coney::graphics::hashMesh(*store.mesh(*meshId)) == coney::graphics::hashMesh(*mesh));
    REQUIRE(store.texture(*textureId) != nullptr);
    CHECK(coney::graphics::hashTexture(*store.texture(*textureId)) == coney::graphics::hashTexture(*converted));
    geometry->destroy();
}
