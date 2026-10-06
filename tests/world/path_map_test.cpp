// SPDX-License-Identifier: GPL-3.0-or-later
// The path data decoded (docs/research/level-loading.md#path-data): the records handed out in order, the inside test
// by slab lists and without, and the walkable-line test across neighbouring polygons. Synthetic chunks and maps only.
#include "world/path_map.h"

#include <cstdint>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "core/error.h"
#include "support/fixtures.h"
#include "support/path_fixtures.h"
#include "support/world_fixtures.h"

using coney::anim::Vec3;
using coney::test::Bytes;
using coney::test::f32;
using coney::world::PathMap;

namespace {

// A vertex record: x, y and 8 bytes the decoder does not read.
void vertex(Bytes& chunk, float x, float y) { f32(f32(chunk, x), y).fill(8, 0); }

// A chunk of one 4 × 2 rectangle (anticlockwise) at the origin whose 16 slabs all list its two upright edges (1 and
// 3); a second, list-less rectangle [4, 6] × [0, 2] owning no nodes; two route nodes in the first, linked both ways
// (the second link with the avoid bit and flag 0x80). The A record's +0x08 holds a value the decoder must ignore.
Bytes twoRectangles() {
    Bytes chunk;
    chunk.u32(2).u32(8).u16(2).u16(1).u32(2).fill(0x10, 0);
    chunk.u16(2).fill(6, 0).u32(0x1234).u32(0); // the A record: 2 nodes
    vertex(chunk, 0.0F, 0.0F);
    vertex(chunk, 4.0F, 0.0F);
    vertex(chunk, 4.0F, 2.0F);
    vertex(chunk, 0.0F, 2.0F);
    vertex(chunk, 4.0F, 0.0F);
    vertex(chunk, 6.0F, 0.0F);
    vertex(chunk, 6.0F, 2.0F);
    vertex(chunk, 4.0F, 2.0F);
    // The first path: 4 vertices on the graph, its box, every slab at list 0, flags 0, an A record.
    chunk.u16(4).u16(1).u32(0);
    f32(f32(f32(f32(chunk, 0.0F), 4.0F), 0.0F), 2.0F).fill(0x10, 0);
    for (int slab = 0; slab < 16; ++slab) {
        chunk.u16(0);
    }
    chunk.u32(0).u32(1);
    // The second: no lists, flag 2, no A record.
    chunk.u16(4).u16(0).u32(0);
    f32(f32(f32(f32(chunk, 4.0F), 6.0F), 0.0F), 2.0F).fill(0x10, 0);
    for (int slab = 0; slab < 16; ++slab) {
        chunk.u16(0xffff);
    }
    chunk.u32(2).u32(0);
    // Two nodes of one edge each.
    f32(f32(f32(f32(chunk, 1.0F), 1.0F), 0.5F), 1.0F).u32(0).u16(1).fill(10, 0);
    f32(f32(f32(f32(chunk, 3.0F), 1.0F), 0.5F), 1.0F).u32(0).u16(1).fill(10, 0);
    chunk.u32(1).u32(1);
    chunk.u32(0).u32(0x80000080U);
    // The edge lists: 1, 3, end; the tail.
    chunk.u16(1).u16(3).u16(0xffff).fill(4, 0);
    return chunk.padTo((chunk.size() + 15) / 16 * 16);
}

} // namespace

TEST_CASE("the path data decodes with its vertices, nodes and edges handed out in order", "[world][paths]") {
    const Bytes chunk = twoRectangles();
    auto map = PathMap::decode(chunk.data());
    REQUIRE(map.has_value());
    REQUIRE(map->polygons().size() == 2);
    const coney::world::PathPolygon& first = map->polygons()[0];
    CHECK(first.vertexCount == 4);
    CHECK(first.graph == 1);
    CHECK(first.hasNodes);
    CHECK(first.firstNode == 0); // in order: the A record's +0x08 is not read
    CHECK(first.nodeCount == 2);
    CHECK(map->polygons()[1].firstVertex == 4);
    CHECK(map->polygons()[1].flags == 2);
    CHECK_FALSE(map->polygons()[1].hasNodes);
    REQUIRE(map->nodes().size() == 2);
    CHECK(map->nodes()[1].position == Vec3{3.0F, 1.0F, 0.5F});
    REQUIRE(map->edgesOf(0).size() == 1);
    CHECK(map->edgesOf(0)[0].to == 1);
    CHECK(map->edgesOf(0)[0].flags == 1);
    CHECK_FALSE(map->edgesOf(0)[0].avoid);
    CHECK(map->edgesOf(1)[0].flags == 0x80);
    CHECK(map->edgesOf(1)[0].avoid);
    CHECK(map->edgeLists().size() == 3);
}

TEST_CASE("path data whose records disagree is refused", "[world][paths]") {
    const Bytes chunk = twoRectangles();
    // The second node's edge leads to node 5 of 2.
    std::vector<std::byte> bytes(chunk.data().begin(), chunk.data().end());
    const std::size_t edges = 0x20 + 16 + 8 * 16 + 2 * 0x50 + 2 * 32;
    bytes[edges + 8] = std::byte{5};
    auto map = PathMap::decode(bytes);
    REQUIRE_FALSE(map.has_value());
    CHECK(map.error().code == coney::ErrorCode::Invalid);
}

TEST_CASE("the inside test counts the winding of the slab's edges, or of every edge without lists", "[world][paths]") {
    const Bytes chunk = twoRectangles();
    auto map = PathMap::decode(chunk.data());
    REQUIRE(map.has_value());
    const coney::world::PathPolygon& listed = map->polygons()[0];
    const coney::world::PathPolygon& plain = map->polygons()[1];
    CHECK(map->inside(listed, 1.0F, 1.0F));
    CHECK(map->inside(listed, 3.9F, 1.99F));
    CHECK_FALSE(map->inside(listed, 4.5F, 1.0F));
    CHECK_FALSE(map->inside(listed, 1.0F, 2.5F));
    CHECK(map->inside(plain, 5.0F, 1.0F));
    CHECK_FALSE(map->inside(plain, 3.0F, 1.0F));
    CHECK(map->polygonAt(5.0F, 1.0F) == 1U);
    CHECK_FALSE(map->polygonAt(5.0F, 1.0F, 2).has_value()); // its flag 2 excluded
    CHECK(map->nearestPolygon(7.0F, 1.0F, 1.5F) == 1U);
    CHECK_FALSE(map->nearestPolygon(8.0F, 1.0F, 1.5F).has_value());
}

TEST_CASE("a polygon wound the other way contains nothing", "[world][paths]") {
    coney::test::PathBuilder builder;
    builder.rectangle(0.0F, 2.0F, 0.0F, 2.0F);
    PathMap map = builder.build();
    CHECK(map.inside(map.polygons()[0], 1.0F, 1.0F));
    // The same square clockwise.
    std::vector<Vec3> vertices{{0.0F, 0.0F, 0.0F}, {0.0F, 2.0F, 0.0F}, {2.0F, 2.0F, 0.0F}, {2.0F, 0.0F, 0.0F}};
    std::vector<coney::world::PathPolygon> polygons{map.polygons()[0]};
    auto clockwise = PathMap::make(vertices, polygons, {}, {});
    REQUIRE(clockwise.has_value());
    CHECK_FALSE(clockwise->inside(clockwise->polygons()[0], 1.0F, 1.0F));
}

TEST_CASE("a line is walkable while it stays inside the polygons, across their borders", "[world][paths]") {
    const PathMap map = coney::test::uCorridors();
    CHECK(map.walkable({1.0F, 1.0F, 0.0F}, {9.5F, 1.0F, 0.0F}));        // along the bottom
    CHECK(map.walkable({1.0F, 1.0F, 0.0F}, {9.5F, 1.5F, 0.0F}));        // into the side's overlap
    CHECK(map.walkable({9.0F, 1.0F, 0.0F}, {9.0F, 9.5F, 0.0F}));        // up the side, into the top
    CHECK_FALSE(map.walkable({1.0F, 1.0F, 0.0F}, {1.0F, 9.0F, 0.0F}));  // across the U's gap
    CHECK_FALSE(map.walkable({1.0F, 1.0F, 0.0F}, {9.0F, 5.0F, 0.0F}));  // cuts the inner corner
    CHECK_FALSE(map.walkable({1.0F, 1.0F, 0.0F}, {12.0F, 1.0F, 0.0F})); // off the end
}

TEST_CASE("the walkable-line test leaves out polygons with flag 8 or the caller's mask", "[world][paths]") {
    coney::test::PathBuilder builder;
    builder.rectangle(0.0F, 4.0F, 0.0F, 2.0F);
    builder.rectangle(4.0F, 8.0F, 0.0F, 2.0F, 1, 0x20);
    builder.rectangle(8.0F, 12.0F, 0.0F, 2.0F, 1, coney::world::kPathPolygonExcluded);
    const PathMap map = builder.build();
    CHECK(map.walkable({1.0F, 1.0F, 0.0F}, {7.0F, 1.0F, 0.0F}));
    CHECK_FALSE(map.walkable({1.0F, 1.0F, 0.0F}, {7.0F, 1.0F, 0.0F}, 0x20));
    CHECK_FALSE(map.walkable({1.0F, 1.0F, 0.0F}, {9.0F, 1.0F, 0.0F}));
}
