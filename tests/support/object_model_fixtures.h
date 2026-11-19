// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Synthetic Object List models and the archive that holds them, built byte by byte in the tests. Nothing here comes
// from the game (LEGAL.md, "No game data"). The layouts follow docs/research/level-loading.md#the-object-list (the
// global resource's chunk 0x46 and a record's model and dictionary resources), the level file's chunk 0x47 (a
// RenderWare clump, docs/research/level-loading.md#the-level-object) and RenderWare's binary stream as librw reads it.

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "core/chunk_types.h"
#include "core/name_hash.h"
#include "fileio/disc.h"
#include "fileio/wad.h"
#include "graphics/rw_stream.h"
#include "support/fixtures.h"
#include "support/rw_fixtures.h"
#include "support/world_fixtures.h"
#include "world/world_streams.h"

namespace coney::test {

/// A standalone atomic section: a four-vertex strip (two triangles, positions scaled by 0.5, so 0..2 in x and y) with
/// one untextured material, as a model's part holds one before its dictionary's first texture is linked to it.
inline Bytes untexturedStrip() {
    AtomicFields fields;
    fields.meshChains.push_back(
        ps2MeshChain({{ps2Vertex(0, 0, 0), ps2Vertex(4, 0, 0), ps2Vertex(0, 4, 0), ps2Vertex(4, 4, 0)}}, true));
    fields.meshCounts.push_back(4);
    fields.triangles = 2;
    return nativeAtomic(fields);
}

/// A RenderWare clump section (0x10) of `atomics` atomics, each with a geometry of its own (untexturedStrip()'s), all
/// on one frame at the origin: a world object's model for one atomic, a car's for kCarParts.
inline Bytes stripClump(std::size_t atomics) {
    const Bytes standalone = untexturedStrip();
    auto parts = world::inspectAtomicSection(standalone.span());
    REQUIRE(parts.has_value());
    Bytes frames;
    frames.u32(1);
    for (const float v : {1.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F}) {
        f32(frames, v);
    }
    frames.u32(0xFFFFFFFF).u32(0);
    Bytes frameList = rwSection(0x01, frames);
    frameList.append(rwEmptyExtension().span());
    Bytes geometryList = rwSection(0x01, Bytes{}.u32(static_cast<std::uint32_t>(atomics)));
    for (std::size_t i = 0; i < atomics; ++i) {
        geometryList.append(parts->geometry);
    }
    Bytes clump = rwSection(0x01, Bytes{}.u32(static_cast<std::uint32_t>(atomics)).u32(0).u32(0));
    clump.append(rwSection(0x0E, frameList).span()).append(rwSection(0x1A, geometryList).span());
    for (std::size_t i = 0; i < atomics; ++i) {
        Bytes atomic = rwSection(0x01, Bytes{}.u32(0).u32(static_cast<std::uint32_t>(i)).u32(5).u32(0));
        atomic.append(parts->extension);
        clump.append(rwSection(0x14, atomic).span());
    }
    clump.append(rwEmptyExtension().span());
    return rwSection(0x10, clump);
}

/// A flat chunk container of one chunk of `type` holding `body`.
inline Bytes oneChunk(std::uint32_t type, const Bytes& body) {
    Bytes container;
    const auto size = static_cast<std::uint32_t>(body.size());
    container.header(1, size, 0, 0);
    container.header(type, size, 0, 0).append(body.span());
    return container;
}

/// A texture resource: a chunk 0x0B holding a struct section, then a dictionary of one 2 x 2 texture named `name`.
inline Bytes dictionaryResource(std::string_view name) {
    Bytes body;
    body.u32(graphics::kRwStruct).u32(4).u32(graphics::kRwLibraryStamp).u32(0);
    body.append(texDictionary({rgbaTexture2x2(name)}).span());
    return oneChunk(chunk::kTextureDictionaryTid, body);
}

/// One Object List model: the record's name, and the hashes of its model and its texture dictionary resources.
struct ObjectModelFixture {
    std::string name;
    std::uint32_t modelHash = 0;
    std::uint32_t texturesHash = 0;
    std::size_t atomics = 1; ///< The model's clump: 1 for a world object, kCarParts for a car.
    std::string texture;     ///< The name of its dictionary's one texture.
};

/// The Object List chunk (0x46) of `models`: a count, 12 header bytes, then each record's nine words.
inline Bytes objectListChunk(const std::vector<ObjectModelFixture>& models) {
    Bytes chunk;
    chunk.header(static_cast<std::uint32_t>(models.size()), 0, 0, 0);
    for (const ObjectModelFixture& model : models) {
        chunk.u32(crc32(model.name)).u32(0).u32(model.modelHash).u32(model.texturesHash).u32(0).u32(100).u32(100);
        chunk.u32(100).u32(0);
    }
    return chunk;
}

/// The archive's entries for `models`: the global resource `warriors.glr` holding their Object List, and each one's
/// model (chunk 0x47, its clump) and texture dictionary under the decimal names of their hashes.
inline std::vector<std::pair<std::string, Bytes>> objectModelEntries(const std::vector<ObjectModelFixture>& models) {
    std::vector<std::pair<std::string, Bytes>> entries;
    entries.emplace_back("warriors.glr", oneChunk(0x46, objectListChunk(models)));
    for (const ObjectModelFixture& model : models) {
        entries.emplace_back(std::to_string(model.modelHash), oneChunk(0x47, stripClump(model.atomics)));
        entries.emplace_back(std::to_string(model.texturesHash), dictionaryResource(model.texture));
    }
    return entries;
}

/// A synthetic disc whose archive holds `entries`, each under `./ee_files/<name>`, on 2048-byte boundaries.
inline std::vector<std::byte> archiveDisc(const std::vector<std::pair<std::string, Bytes>>& entries) {
    Bytes wad;
    Bytes dir;
    dir.header(static_cast<std::uint32_t>(entries.size()), 0, 0, 0);
    for (const auto& [name, data] : entries) {
        dir.u32(static_cast<std::uint32_t>(wad.size()))
            .u32(static_cast<std::uint32_t>(data.size()))
            .u32(nameHash(std::string("./ee_files/") + name));
        wad.append(data.span());
        wad.padTo((wad.size() + 2047) / 2048 * 2048);
    }
    return buildIso({{"WARRIORS.DIR;1", dir.data()}, {"WARRIORS.WAD;1", wad.data()}});
}

/// An archive opened from a synthetic disc written into a folder of its own. Held by pointer: clang-tidy's
/// optional-access check does not see the REQUIRE that ends the test when opening fails.
struct ArchiveFixture {
    TempDir folder;
    std::unique_ptr<io::Wad> wad;
    explicit ArchiveFixture(const std::vector<std::pair<std::string, Bytes>>& entries) {
        const auto image = folder.write("disc.iso", archiveDisc(entries));
        auto disc = io::Disc::open(image);
        REQUIRE(disc.has_value());
        auto opened = io::Wad::open(std::move(*disc));
        REQUIRE(opened.has_value());
        wad = std::make_unique<io::Wad>(std::move(*opened));
    }
};

} // namespace coney::test
