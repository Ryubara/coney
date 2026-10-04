// SPDX-License-Identifier: GPL-3.0-or-later
#include <catch2/catch_test_macros.hpp>

#include "support/world_fixtures.h"
#include "world/world_streams.h"

using coney::ErrorCode;
using coney::test::AtomicFields;
using coney::test::Bytes;
using coney::test::nativeAtomic;
using coney::test::partFile;
using coney::test::ps2MeshChain;
using coney::test::ps2Vertex;
using coney::test::worldSector;
using coney::test::worldStream;

namespace {

// A one-mesh atomic: a four-vertex strip (two triangles).
AtomicFields stripAtomic() {
    AtomicFields fields;
    fields.meshChains.push_back(
        ps2MeshChain({{ps2Vertex(0, 0, 0), ps2Vertex(2, 0, 0), ps2Vertex(0, 2, 0), ps2Vertex(2, 2, 0)}}, true));
    fields.meshCounts.push_back(4);
    fields.triangles = 2;
    return fields;
}

} // namespace

TEST_CASE("a world stream's sectors are read with their boxes and plugin data", "[world_streams]") {
    // Two sectors under one plane: the first without an atomic, the second in part 1, corners given max first.
    const Bytes stream = worldStream(1,
                                     {worldSector({0, 0, 0}, {0, 0, 0}, -1, 0, {0, 0, 0}),
                                      worldSector({10, 20, 30}, {-10, -20, -30}, 0, 1, {1, 2, 3})},
                                     1, 2);
    auto world = coney::world::inspectWorldStream(stream.span());
    REQUIRE(world.has_value());
    CHECK(world->partCount == 1);
    CHECK(world->dictionaryOffset == 4);
    CHECK(world->planeSectors == 1);
    // The BSP: one plane splitting x at 0, the first sector on its left, the second on its right.
    REQUIRE(world->planes.size() == 1);
    CHECK_FALSE(world->root.leaf);
    CHECK(world->root.index == 0);
    CHECK(world->planes[0].axis == 0);
    CHECK(world->planes[0].value == 0.0F);
    CHECK(world->planes[0].left.leaf);
    CHECK(world->planes[0].left.index == 0);
    CHECK(world->planes[0].right.leaf);
    CHECK(world->planes[0].right.index == 1);
    REQUIRE(world->sectors.size() == 2);
    REQUIRE(world->sectors[1].plugin.has_value());
    CHECK(world->sectors[1].plugin.value_or(coney::world::SectorPluginData{}).streamedIndex == 0);
    CHECK(world->sectors[1].plugin.value_or(coney::world::SectorPluginData{}).part == 1);
    CHECK(world->sectors[1].plugin.value_or(coney::world::SectorPluginData{}).origin.z == 3.0F);
    CHECK(world->sectors[1].box.min.y == -20.0F);
    CHECK(world->sectors[1].box.max.y == 20.0F);
    CHECK(world->sectors[0]
              .plugin.value_or(coney::world::SectorPluginData{.streamedIndex = 7, .part = 0, .origin = {}})
              .streamedIndex == -1);
    CHECK(world->worldBox.max.x == 100.0F);
}

TEST_CASE("a world whose header miscounts its sectors, or that is cut off, is refused", "[world_streams]") {
    const Bytes stream = worldStream(1, {worldSector({0, 0, 0}, {1, 1, 1}, 0, 1, {0, 0, 0})}, 0, 2);
    auto miscounted = coney::world::inspectWorldStream(stream.span());
    REQUIRE_FALSE(miscounted.has_value());
    CHECK(miscounted.error().code == ErrorCode::Invalid);

    const Bytes good = worldStream(1, {worldSector({0, 0, 0}, {1, 1, 1}, 0, 1, {0, 0, 0})}, 0, 1);
    auto cut = coney::world::inspectWorldStream(good.span().first(good.size() - 10));
    REQUIRE_FALSE(cut.has_value());
    CHECK(cut.error().code == ErrorCode::Truncated);
}

TEST_CASE("plugin data must have its exact size", "[world_streams]") {
    Bytes short16;
    short16.fill(16, 0);
    CHECK_FALSE(coney::world::readSectorPluginData(short16.span()).has_value());
    CHECK_FALSE(coney::world::readAtomicPluginData(short16.span()).has_value());
    Bytes twelve;
    coney::test::f32(coney::test::f32(twelve, 0.125F), 2.0F).u32(7);
    auto data = coney::world::readAtomicPluginData(twelve.span());
    REQUIRE(data.has_value());
    CHECK(data->positionScale == 0.125F);
    CHECK(data->secondScale == 2.0F);
    CHECK(data->word == 7);
}

TEST_CASE("a part file's records and atomic sections are found and checked", "[world_streams]") {
    const Bytes atomic = nativeAtomic(stripAtomic());
    const Bytes file = partFile(0xABCD, {{5, atomic}, {6, atomic}});
    auto part = coney::world::inspectPartFile(file.span());
    REQUIRE(part.has_value());
    CHECK(part->nameHash == 0xABCD);
    REQUIRE(part->atomics.size() == 2);
    CHECK(part->atomics[1].streamedIndex == 6);
    CHECK(part->atomics[1].bytes == atomic.size());

    auto info =
        coney::world::inspectAtomicSection(file.span().subspan(part->atomics[0].offset, part->atomics[0].bytes));
    REQUIRE(info.has_value());
    CHECK(info->triangleStrips);
    CHECK(info->triangleCount == 2);
    CHECK(info->materialCount == 1);
    REQUIRE(info->meshes.size() == 1);
    CHECK(info->meshes[0].indexCount == 4);
    CHECK(info->pipelinePlugin == 3);
    CHECK(info->pipeline == 0x30083U);
    REQUIRE(info->plugin.has_value());
    CHECK(info->plugin.value_or(coney::world::kDefaultAtomicPluginData).positionScale == 0.5F);

    Bytes trailing = file;
    trailing.u32(0);
    CHECK(coney::world::inspectPartFile(trailing.span()).error().code == ErrorCode::Invalid);
}

TEST_CASE("an atomic librw would misread is refused before it gets there", "[world_streams]") {
    AtomicFields badMaterial = stripAtomic();
    badMaterial.materialIndex = 1; // the list holds one material
    CHECK(coney::world::inspectAtomicSection(nativeAtomic(badMaterial).span()).error().code == ErrorCode::Invalid);

    AtomicFields notNative = stripAtomic();
    notNative.geometryFlags = 0x1D;
    CHECK(coney::world::inspectAtomicSection(nativeAtomic(notNative).span()).error().code == ErrorCode::Invalid);
}
