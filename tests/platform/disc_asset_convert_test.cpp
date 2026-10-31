// SPDX-License-Identifier: GPL-3.0-or-later

// A check against the player's own disc: every model and texture of one level's streamed worlds converted into Coney's
// own mesh and texture formats (platform/asset_convert.h) and put in an asset store. The models are the part atomics
// of each world, unpacked into plain geometry as the GL3 path draws them; the textures are every dictionary of the
// world's stream and its parts. Each conversion is checked against what librw holds (vertex and index counts, level
// 0's texels against TextureDictionary::toImages()). It runs only when the environment variable CONEY_DISC names the
// disc and skips otherwise, so CI never needs the game. It prints counts and hashes only, never data (LEGAL.md).

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <expected>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include <SDL3/SDL_stdinc.h>
#include <catch2/catch_test_macros.hpp>
#include <rw.h>

#include "core/chunk_system.h"
#include "fileio/disc.h"
#include "fileio/wad.h"
#include "graphics/asset_store.h"
#include "graphics/mesh_data.h"
#include "graphics/texture_data.h"
#include "platform/asset_convert.h"
#include "platform/render_engine.h"
#include "platform/texture_dictionary.h"
#include "platform/world_atomic.h"
#include "platform/world_set.h"
#include "world/world_streams.h"

namespace {

// The level converted: the tutorial (docs/research/scripting.md#level99-lessons), whose world most disc tests load.
constexpr const char* kLevel = "level99";

// Totals over the level.
struct Totals {
    std::uint64_t worlds = 0;
    std::uint64_t entries = 0;           // WAD entries read: world streams and parts
    std::uint64_t atomics = 0;           // part atomics read with librw and unpacked
    std::uint64_t meshes = 0;            // ... converted and stored
    std::uint64_t vertices = 0;          // vertices of the stored meshes
    std::uint64_t indices = 0;           // indices of the stored meshes
    std::uint64_t subMeshes = 0;         // sub-meshes (draws) of the stored meshes
    std::uint64_t textures = 0;          // textures read
    std::uint64_t converted = 0;         // ... converted and stored
    std::uint64_t levels = 0;            // mip levels of the stored textures
    std::uint64_t texels = 0;            // texels of every stored level
    std::uint64_t meshMismatches = 0;    // mesh conversions that disagree with what librw holds
    std::uint64_t textureMismatches = 0; // texture conversions whose level 0 is not librw's image
    std::uint64_t failures = 0;          // reads or conversions that failed
    std::uint64_t meshHash = 0;          // the stored meshes' hashes, folded in load order
    std::uint64_t textureHash = 0;       // the stored textures' hashes, folded in load order
};

// Folds `hash` into `into`, so the printed total depends on every hash and their order.
void fold(std::uint64_t& into, std::uint64_t hash) { into = (into ^ hash) * 0x100000001b3ULL; }

// Reads a whole WAD entry by name; nothing when there is no such entry.
std::optional<std::vector<std::byte>> readEntry(const coney::io::Wad& wad, const std::string& name) {
    auto entry = wad.lookup(name);
    if (!entry) {
        return std::nullopt;
    }
    auto stream = wad.openEntry(**entry);
    if (!stream) {
        return std::nullopt;
    }
    std::vector<std::byte> bytes(static_cast<std::size_t>(stream->size()));
    if (!stream->read(bytes)) {
        return std::nullopt;
    }
    return bytes;
}

// Whether `mesh` has what librw holds of `geometry`: its vertex count and, mesh by mesh, its index counts.
bool sameAsLibrw(const coney::graphics::MeshData& mesh, const rw::Geometry& geometry) {
    if (mesh.vertices.size() != static_cast<std::size_t>(geometry.numVertices) ||
        mesh.subMeshes.size() != geometry.meshHeader->numMeshes ||
        mesh.indices.size() != geometry.meshHeader->totalIndices) {
        return false;
    }
    const rw::Mesh* meshes = geometry.meshHeader->getMeshes();
    for (std::size_t m = 0; m < mesh.subMeshes.size(); ++m) {
        if (mesh.subMeshes[m].indexCount != meshes[m].numIndices) {
            return false;
        }
    }
    return true;
}

// Converts every texture of `entry`'s dictionaries into `store`, checking level 0 against librw's own images. The
// conversion comes first and librw's read second: librw's read scrambles a narrow palette raster
// (docs/engine/asset-formats.md#scrambling-read), so a match shows both that the conversion read the raster as
// loaded and that it left it so.
void convertTextures(const coney::io::Wad& wad, const coney::io::WadEntry& entry,
                     const coney::chunk::ChunkHandlerTable& table, coney::graphics::AssetStore& store, Totals& totals) {
    auto dictionaries = coney::platform::loadTextureDictionaries(wad, entry, table);
    if (!dictionaries) {
        if (dictionaries.error().code != coney::ErrorCode::NotFound) {
            ++totals.failures;
        }
        return;
    }
    for (coney::platform::TextureDictionary& dictionary : *dictionaries) {
        const std::vector<rw::Texture*> textures = dictionary.textures();
        std::vector<std::expected<coney::graphics::TextureData, coney::Error>> converted;
        converted.reserve(textures.size());
        for (rw::Texture* texture : textures) {
            converted.push_back(coney::platform::textureFromRw(*texture));
        }
        auto images = dictionary.toImages();
        if (!images) {
            ++totals.failures;
            continue;
        }
        for (std::size_t i = 0; i < textures.size(); ++i) {
            ++totals.textures;
            auto& texture = converted[i];
            if (!texture) {
                ++totals.failures;
                continue;
            }
            if (texture->levels.empty() || texture->levels[0].texels != (*images)[i].pixels) {
                ++totals.textureMismatches;
            }
            const std::uint64_t hash = coney::graphics::hashTexture(*texture);
            std::uint64_t levels = 0;
            std::uint64_t texels = 0;
            for (const coney::graphics::TextureLevel& level : texture->levels) {
                ++levels;
                texels += std::uint64_t{level.width} * level.height;
            }
            if (!store.addTexture(std::move(*texture))) {
                ++totals.failures;
                continue;
            }
            ++totals.converted;
            totals.levels += levels;
            totals.texels += texels;
            fold(totals.textureHash, hash);
        }
    }
}

// Reads each atomic of one part file with librw, unpacks it and converts its geometry into `store`.
void convertPart(const std::vector<std::byte>& file, coney::graphics::AssetStore& store, Totals& totals) {
    auto part = coney::world::inspectPartFile(file);
    if (!part) {
        ++totals.failures;
        return;
    }
    for (const coney::world::PartAtomic& record : part->atomics) {
        const auto section = std::span<const std::byte>(file).subspan(record.offset, record.bytes);
        auto atomic = coney::platform::WorldAtomic::read(section, coney::world::Vec3{});
        if (!atomic) {
            ++totals.failures;
            continue;
        }
        atomic->unpack();
        ++totals.atomics;
        const rw::Geometry& geometry = *atomic->atomic()->geometry;
        auto mesh = coney::platform::meshFromRw(geometry);
        if (!mesh) {
            ++totals.failures;
            continue;
        }
        if (!sameAsLibrw(*mesh, geometry)) {
            ++totals.meshMismatches;
        }
        const std::uint64_t hash = coney::graphics::hashMesh(*mesh);
        const std::size_t vertices = mesh->vertices.size();
        const std::size_t indices = mesh->indices.size();
        const std::size_t subMeshes = mesh->subMeshes.size();
        if (!store.addMesh(std::move(*mesh))) {
            ++totals.failures;
            continue;
        }
        ++totals.meshes;
        totals.vertices += vertices;
        totals.indices += indices;
        totals.subMeshes += subMeshes;
        fold(totals.meshHash, hash);
    }
}

} // namespace

TEST_CASE("every model and texture of the disc's level99 converts into Coney's mesh and texture formats",
          "[disc][asset_convert]") {
    // Expected: formats/renderware.md#ps2-raster, confirmed (code): every texture and atomic converts, and level 0 and
    // the counts are what librw reads
    const char* discPath = SDL_getenv("CONEY_DISC");
    if (discPath == nullptr || *discPath == '\0') {
        SKIP("CONEY_DISC is not set: no disc to check");
    }
    auto disc = coney::io::Disc::open(discPath);
    REQUIRE(disc.has_value());
    auto wad = coney::io::Wad::open(std::move(*disc));
    REQUIRE(wad.has_value());
    auto engine = coney::platform::RenderEngine::start(coney::platform::RenderBackend::Null, {});
    REQUIRE(engine.has_value());
    coney::chunk::ChunkHandlerTable table = coney::chunk::ChunkHandlerTable::withDefaults();
    coney::platform::addTextureDictionaryHandlers(table);
    auto worlds = coney::platform::worldNamesFor(*wad, kLevel);
    REQUIRE(worlds.has_value());

    coney::graphics::AssetStore store;
    Totals totals;
    for (const std::string& name : *worlds) {
        ++totals.worlds;
        const std::string streamName = name + "_sec.wld";
        auto stream = readEntry(*wad, streamName);
        if (!stream) {
            ++totals.failures;
            continue;
        }
        auto world = coney::world::inspectWorldStream(*stream);
        REQUIRE(world.has_value());
        std::vector<std::string> files{streamName};
        for (std::uint32_t i = 1; i <= world->partCount; ++i) {
            files.push_back(std::format("{}_ms{}.sec", name, i));
        }
        for (const std::string& file : files) {
            auto entry = wad->lookup(file);
            auto bytes = readEntry(*wad, file);
            if (!entry || !bytes) {
                ++totals.failures;
                continue;
            }
            ++totals.entries;
            convertTextures(*wad, **entry, table, store, totals);
            if (file != streamName) {
                convertPart(*bytes, store, totals);
            }
        }
    }

    std::printf("%s: worlds %llu, entries %llu\n", kLevel, static_cast<unsigned long long>(totals.worlds),
                static_cast<unsigned long long>(totals.entries));
    std::printf("  meshes: %llu of %llu atomics, vertices %llu, indices %llu, sub-meshes %llu, hash %016llx\n",
                static_cast<unsigned long long>(totals.meshes), static_cast<unsigned long long>(totals.atomics),
                static_cast<unsigned long long>(totals.vertices), static_cast<unsigned long long>(totals.indices),
                static_cast<unsigned long long>(totals.subMeshes), static_cast<unsigned long long>(totals.meshHash));
    std::printf("  textures: %llu of %llu, levels %llu, texels %llu, hash %016llx\n",
                static_cast<unsigned long long>(totals.converted), static_cast<unsigned long long>(totals.textures),
                static_cast<unsigned long long>(totals.levels), static_cast<unsigned long long>(totals.texels),
                static_cast<unsigned long long>(totals.textureHash));
    std::printf("  mismatches with librw: meshes %llu, textures %llu; failures: %llu\n",
                static_cast<unsigned long long>(totals.meshMismatches),
                static_cast<unsigned long long>(totals.textureMismatches),
                static_cast<unsigned long long>(totals.failures));
    CHECK(totals.atomics > 0);
    CHECK(totals.textures > 0);
    CHECK(totals.meshes == totals.atomics);
    CHECK(totals.converted == totals.textures);
    CHECK(store.meshCount() == totals.meshes);
    CHECK(store.textureCount() == totals.converted);
    CHECK(totals.meshMismatches == 0);
    CHECK(totals.textureMismatches == 0);
    CHECK(totals.failures == 0);
}
