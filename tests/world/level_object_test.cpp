// SPDX-License-Identifier: GPL-3.0-or-later
#include "world/level_object.h"

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "core/chunk_system.h"
#include "core/error.h"
#include "fileio/stream.h"
#include "support/fixtures.h"
#include "support/world_fixtures.h"
#include "world/world_streams.h"

using coney::ErrorCode;
using coney::test::Bytes;
using coney::test::f32;
using coney::test::rwSection;
using coney::world::Vec3;

namespace {

// The occluders chunk: count, padding, then records whose four points are given in game axes.
Bytes occludersChunk(const std::vector<std::array<Vec3, 4>>& walls) {
    Bytes chunk;
    chunk.u32(static_cast<std::uint32_t>(walls.size())).padTo(16);
    for (const auto& wall : walls) {
        chunk.fill(0x30, 0); // three planes, computed per viewport
        for (const Vec3& p : wall) {
            f32(f32(f32(f32(chunk, p.x), p.y), p.z), 1.0F);
        }
        chunk.fill(0x10, 0); // the rest, with the active flag
    }
    return chunk;
}

// A path data chunk with the given counts, its records zero, and `extra` bytes after them.
Bytes pathChunk(std::uint32_t paths, std::uint32_t b, std::uint16_t c, std::uint16_t a, std::uint32_t d,
                std::size_t extra) {
    Bytes chunk;
    chunk.u32(paths).u32(b).u16(c).u16(a).u32(d);
    chunk.fill(std::size_t{a} * 16 + std::size_t{b} * 16 + std::size_t{paths} * 0x50 + std::size_t{c} * 32 +
                   std::size_t{d} * 8 + extra,
               0);
    return chunk;
}

// An empty collision mesh's six chunks: one cell, no triangles.
struct EmptyCollision {
    Bytes header, lists, grid, triangles, vertices, checked;
};
EmptyCollision emptyCollision() {
    EmptyCollision c;
    c.header.fill(0x50, 0);
    f32(f32(f32(f32(c.header, 0.5F), 0.5F), 0.5F), 1.0F);
    f32(f32(f32(f32(c.header, 0.5F), 0.5F), 0.5F), 1.0F);
    c.header.u16(1).u16(1).u16(1).u16(0).u32(0).u32(1).u32(0).u16(0).u16(0).u32(0).u32(0).u32(0).u32(0);
    f32(c.header, 0.0F).u32(0);
    c.lists.u16(0).u16(0);
    c.grid.u32(0);
    return c;
}

// A stand-in for the platform's objects, telling which chunk it came from.
class FakeObject final : public coney::chunk::LoadedObject {
  public:
    explicit FakeObject(std::string tag) : m_tag(std::move(tag)) {}
    [[nodiscard]] std::string_view describe() const override { return m_tag; }

  private:
    std::string m_tag;
};

// The tag of a fake object, or "" for anything else.
std::string tagOf(const coney::chunk::LoadedObject* object) {
    const auto* fake = dynamic_cast<const FakeObject*>(object);
    return fake == nullptr ? std::string() : std::string(fake->describe());
}

// Pushes a FakeObject under `result`, tagged `prefix` and the chunk's first byte: what the fake readers below do.
std::expected<void, coney::Error> pushFake(coney::io::Stream& chunk, const coney::chunk::ChunkHeader& header,
                                           coney::chunk::ChunkStacks& stacks, std::uint32_t result,
                                           std::string_view prefix) {
    std::vector<std::byte> first(1);
    if (auto read = chunk.read(first); !read) {
        return std::unexpected(read.error());
    }
    stacks.pushChunk(coney::chunk::ChunkData{
        .type = result,
        .id = header.id,
        .bytes = {},
        .object = std::make_unique<FakeObject>(std::string(prefix) + std::to_string(std::to_integer<int>(first[0])))});
    return {};
}

// Stand-ins for the platform's readers of 0x2A, 0x47 and 0x15.
std::expected<void, coney::Error> readFakeDictionary(coney::io::Stream& chunk, const coney::chunk::ChunkHeader& header,
                                                     coney::chunk::ChunkStacks& stacks) {
    return pushFake(chunk, header, stacks, 0x0B, "dictionary ");
}
std::expected<void, coney::Error> readFakeModel(coney::io::Stream& chunk, const coney::chunk::ChunkHeader& header,
                                                coney::chunk::ChunkStacks& stacks) {
    return pushFake(chunk, header, stacks, 0x41, "model ");
}
std::expected<void, coney::Error> readFakeWorld(coney::io::Stream& chunk, const coney::chunk::ChunkHeader& header,
                                                coney::chunk::ChunkStacks& stacks) {
    return pushFake(chunk, header, stacks, 0x42, "world ");
}

// The links recordLink() has made, as "model + dictionary" tags.
std::vector<std::string>& recordedLinks() {
    static std::vector<std::string> links;
    return links;
}

// A link that records the pair it was given.
std::expected<void, coney::Error> recordLink(coney::chunk::LoadedObject& model,
                                             coney::chunk::LoadedObject& dictionary) {
    recordedLinks().push_back(tagOf(&model) + " + " + tagOf(&dictionary));
    return {};
}

// A level file with the disc's 18 chunks in the disc's order; each RenderWare chunk is one byte naming it.
Bytes levelFile() {
    const EmptyCollision c = emptyCollision();
    const Bytes occluders = occludersChunk({{Vec3{1, 2, 3}, Vec3{4, 5, 6}, Vec3{1, 2, 4}, Vec3{4, 5, 7}}});
    const Bytes paths = pathChunk(1, 2, 3, 4, 5, 32);
    Bytes one;
    one.u8(0);
    const auto marker = [](int value) {
        Bytes b;
        b.u8(static_cast<std::uint8_t>(value)).padTo(16);
        return b;
    };
    std::vector<std::pair<std::uint32_t, Bytes>> chunks{{0x53, occluders},
                                                        {0x2A, marker(1)},
                                                        {0x47, marker(1)},
                                                        {0x2A, marker(2)},
                                                        {0x47, marker(2)},
                                                        {0x2A, marker(3)},
                                                        {0x47, marker(3)},
                                                        {0x2A, marker(4)},
                                                        {0x15, marker(4)},
                                                        {0x40, paths},
                                                        {0x52, c.checked},
                                                        {0x07, c.vertices},
                                                        {0x04, c.triangles},
                                                        {0x05, c.grid},
                                                        {0x06, c.lists},
                                                        {0x03, c.header},
                                                        {0x17, Bytes{}.fill(48, 0)},
                                                        {0x51, Bytes{}.fill(32, 7)}};
    Bytes file;
    file.header(static_cast<std::uint32_t>(chunks.size()), 0, 0, 0);
    for (const auto& [type, data] : chunks) {
        file.header(type, static_cast<std::uint32_t>(data.size()), 0, 0).append(data.span());
    }
    return file;
}

} // namespace

TEST_CASE("occluders are read with their corners turned into RenderWare's axes", "[level_object]") {
    const Bytes chunk = occludersChunk({{Vec3{1, 2, 3}, Vec3{4, 5, 6}, Vec3{1, 2, 4}, Vec3{4, 5, 7}}});
    auto occluders = coney::world::readOccluders(chunk.span());
    REQUIRE(occluders.has_value());
    REQUIRE(occluders->size() == 1);
    const Vec3 p0 = (*occluders)[0].corners[0];
    CHECK(p0.x == 1.0F);
    CHECK(p0.y == 3.0F);  // game z
    CHECK(p0.z == -2.0F); // minus game y
    CHECK((*occluders)[0].corners[3].y == 7.0F);
    Bytes cut = occludersChunk({});
    cut.patchU32(0, 2);
    CHECK(coney::world::readOccluders(cut.span()).error().code == ErrorCode::Truncated);
}

TEST_CASE("path data's counted records must fit in the chunk, which may hold more", "[level_object]") {
    const Bytes chunk = pathChunk(2, 3, 4, 5, 6, 48);
    auto header = coney::world::inspectPathData(chunk.span());
    REQUIRE(header.has_value());
    CHECK(header->paths == 2);
    CHECK(header->aCount == 5);
    CHECK(header->cCount == 4);
    CHECK(header->recordBytes == 16 + 5 * 16 + 3 * 16 + 2 * 0x50 + 4 * 32 + 6 * 8);
    Bytes shortChunk = pathChunk(2, 3, 4, 5, 6, 0);
    shortChunk.patchU32(0, 3); // one more path than there is room for
    CHECK(coney::world::inspectPathData(shortChunk.span()).error().code == ErrorCode::Truncated);
}

TEST_CASE("a clump of one atomic becomes a standalone atomic with its frames combined", "[level_object]") {
    coney::test::AtomicFields fields;
    fields.meshChains.push_back(Bytes{}.fill(32, 0));
    fields.meshCounts.push_back(3);
    fields.triangles = 1;
    const Bytes standalone = coney::test::nativeAtomic(fields);
    auto parts = coney::world::inspectAtomicSection(standalone.span());
    REQUIRE(parts.has_value());

    // Frame 0: the root, turned (x, y, z) -> rows (1,0,0), (0,0,-1), (0,1,0), at (10, 20, 30); frame 1 under it, at
    // (1, 0, 0). The atomic names frame 1.
    Bytes frames;
    frames.u32(2);
    for (const float v : {1.0F, 0.0F, 0.0F, 0.0F, 0.0F, -1.0F, 0.0F, 1.0F, 0.0F, 10.0F, 20.0F, 30.0F}) {
        f32(frames, v);
    }
    frames.u32(0xFFFFFFFF).u32(0);
    for (const float v : {1.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 1.0F, 1.0F, 0.0F, 0.0F}) {
        f32(frames, v);
    }
    frames.u32(0).u32(0);
    Bytes frameList = rwSection(0x01, frames);
    frameList.append(coney::test::rwEmptyExtension().span()).append(coney::test::rwEmptyExtension().span());
    Bytes geometryList = rwSection(0x01, Bytes{}.u32(1));
    geometryList.append(parts->geometry);
    Bytes atomic = rwSection(0x01, Bytes{}.u32(1).u32(0).u32(5).u32(0));
    atomic.append(parts->extension);
    Bytes clump = rwSection(0x01, Bytes{}.u32(1).u32(0).u32(0));
    clump.append(rwSection(0x0E, frameList).span())
        .append(rwSection(0x1A, geometryList).span())
        .append(rwSection(0x14, atomic).span())
        .append(coney::test::rwEmptyExtension().span());
    const Bytes rwClump = rwSection(0x10, clump);

    auto model = coney::world::extractClumpModel(rwClump.span());
    REQUIRE(model.has_value());
    auto rearranged = coney::world::inspectAtomicSection(model->atomicSection);
    REQUIRE(rearranged.has_value());
    CHECK(rearranged->triangleCount == 1);
    CHECK(rearranged->pipeline == 0x30083U);
    CHECK(model->frame.position.x == 11.0F);
    CHECK(model->frame.position.y == 20.0F);
    CHECK(model->frame.position.z == 30.0F);
    CHECK(model->frame.up.z == -1.0F);
    CHECK(model->frame.at.y == 1.0F);

    // Two atomics are not a model.
    Bytes two = rwClump;
    two.patchU32(24, 2);
    CHECK(!coney::world::extractClumpModel(two.span()).has_value());
}

TEST_CASE("a world of one sector becomes a standalone atomic with no pipeline", "[level_object]") {
    // Material list: one material with no texture (as the part atomics' fixture has).
    Bytes materialStruct;
    materialStruct.u32(0).u32(0xFFFFFFFF).u32(0).u32(0);
    f32(f32(f32(materialStruct, 1.0F), 1.0F), 1.0F);
    Bytes material = rwSection(0x01, materialStruct);
    material.append(coney::test::rwEmptyExtension().span());
    Bytes list = rwSection(0x01, Bytes{}.u32(1).u32(0xFFFFFFFF));
    list.append(rwSection(0x07, material).span());
    // Sector: struct, extension with meshes, native data and the sector plugin.
    Bytes sectorStruct;
    sectorStruct.u32(0).u32(1).u32(3);
    for (const float v : {-1.0F, -2.0F, -3.0F, 1.0F, 2.0F, 3.0F}) {
        f32(sectorStruct, v);
    }
    sectorStruct.u32(0).u32(0);
    Bytes native;
    native.u32(4).u32(16).u32(1).fill(16, 0);
    Bytes sectorExtension = rwSection(0x50E, Bytes{}.u32(1).u32(1).u32(3).u32(3).u32(0));
    sectorExtension.append(rwSection(0x510, rwSection(0x01, native)).span())
        .append(rwSection(0x3F1, Bytes{}.u32(0xFFFFFFFF).fill(16, 0)).span());
    Bytes sector = rwSection(0x01, sectorStruct);
    sector.append(rwSection(0x03, sectorExtension).span());
    // World: struct (counts and format), material list, the sector, extension.
    Bytes worldStruct;
    worldStruct.u32(1).fill(12, 0).u32(1).u32(3).u32(0).u32(1).u32(0).u32(0x4101000D).fill(24, 0);
    Bytes body = rwSection(0x01, worldStruct);
    body.append(rwSection(0x08, list).span())
        .append(rwSection(0x09, sector).span())
        .append(coney::test::rwEmptyExtension().span());
    const Bytes world = rwSection(0x0B, body);

    auto model = coney::world::extractLevelWorld(world.span());
    REQUIRE(model.has_value());
    CHECK(model->triangleCount == 1);
    CHECK(model->vertexCount == 3);
    CHECK(model->box.min.z == -3.0F);
    auto atomic = coney::world::inspectAtomicSection(model->atomicSection);
    REQUIRE(atomic.has_value());
    CHECK(!atomic->pipeline.has_value());
    CHECK(atomic->geometryFlags == 0x0101000DU);
    CHECK(atomic->materialCount == 1);
    REQUIRE(atomic->meshes.size() == 1);
    CHECK(atomic->meshes[0].indexCount == 3);

    // A world with a plane is not a level world.
    Bytes planes = world;
    planes.patchU32(12 + 12 + 24, 1); // the plane count in the world struct
    CHECK(!coney::world::extractLevelWorld(planes.span()).has_value());
}

TEST_CASE("a level file builds the level object from all its chunks, models linked in pairs", "[level_object]") {
    coney::chunk::ChunkHandlerTable table = coney::chunk::ChunkHandlerTable::withDefaults();
    table.setHandlers(0x2A, coney::chunk::ChunkHandlers{{}, readFakeDictionary});
    table.setHandlers(0x47, coney::chunk::ChunkHandlers{{}, readFakeModel});
    table.setHandlers(0x15, coney::chunk::ChunkHandlers{{}, readFakeWorld});
    std::vector<std::string>& links = recordedLinks();
    links.clear();
    coney::world::addLevelFileHandlers(table, recordLink);
    const Bytes file = levelFile();
    coney::io::MemoryStream stream(file.span());
    auto level = coney::world::loadLevelFile(stream, table);
    REQUIRE(level.has_value());
    const coney::world::LevelObject& object = **level;
    CHECK(links ==
          std::vector<std::string>{"model 3 + dictionary 3", "model 2 + dictionary 2", "model 1 + dictionary 1"});
    CHECK(tagOf(object.skyline.model.get()) == "model 1");
    CHECK(tagOf(object.skyBox.dictionary.get()) == "dictionary 2");
    CHECK(tagOf(object.cloudBox.model.get()) == "model 3");
    CHECK(tagOf(object.glowDictionary.get()) == "dictionary 4");
    CHECK(tagOf(object.levelWorld.get()) == "world 4");
    REQUIRE(object.collision != nullptr);
    CHECK(object.collision->triangles().empty());
    CHECK(object.occluders.size() == 1);
    CHECK(object.pathHeader.paths == 1);
    CHECK(object.pathData.size() == object.pathHeader.recordBytes + 32);
    CHECK(object.subtitles.size() == 32);

    // A link that fails fails the load.
    coney::world::addLevelFileHandlers(
        table, [](coney::chunk::LoadedObject&, coney::chunk::LoadedObject&) -> std::expected<void, coney::Error> {
            return coney::fail(ErrorCode::Invalid, "no");
        });
    coney::io::MemoryStream again(file.span());
    CHECK(!coney::world::loadLevelFile(again, table).has_value());
}
