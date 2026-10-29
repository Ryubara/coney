// SPDX-License-Identifier: GPL-3.0-or-later
#include <algorithm>
#include <cstdint>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <rw.h>

#include "platform/render_engine.h"
#include "platform/world_atomic.h"
#include "support/world_fixtures.h"

using coney::ErrorCode;
using coney::platform::RenderBackend;
using coney::platform::RenderEngine;
using coney::platform::WorldAtomic;
using coney::test::AtomicFields;
using coney::test::nativeAtomic;
using coney::test::ps2MeshChain;
using coney::test::ps2Vertex;

namespace {

// Two meshes: a five-vertex strip in two batches (the second repeating two vertices) whose first triangle is
// degenerate, and a plain four-vertex strip. Scales 0.5 and 0.25, the game's second pipeline.
AtomicFields twoMeshAtomic() {
    AtomicFields fields;
    const auto a = ps2Vertex(0, 0, 0);
    const auto b = ps2Vertex(4, 0, 0);
    const auto c = ps2Vertex(0, 4, 0);
    const auto d = ps2Vertex(4, 4, 0);
    fields.meshChains.push_back(ps2MeshChain({{a, a, b, c}, {b, c, d}}, true));
    fields.meshCounts.push_back(5);
    fields.meshChains.push_back(ps2MeshChain({{a, b, c, d}}, false));
    fields.meshCounts.push_back(4);
    fields.triangles = 4; // strip one: (a a b) is degenerate, then (a b c), (b c d); strip two: 2
    return fields;
}

} // namespace

TEST_CASE("a part atomic reads with librw, keeps its scales and gets the game's pipeline", "[world_atomic]") {
    // Expected: world.md#part-file, confirmed (code) (the frame at the sector's origin; the 0x3F0 scales and the
    // pipeline ids are the synthetic atomic's own)
    auto engine = RenderEngine::start(RenderBackend::Null, {});
    REQUIRE(engine.has_value());
    auto atomic = WorldAtomic::read(nativeAtomic(twoMeshAtomic()).span(), {10.0F, 20.0F, 30.0F});
    REQUIRE(atomic.has_value());
    const coney::world::AtomicPluginData scales = coney::platform::atomicPluginData(atomic->atomic());
    CHECK(scales.positionScale == 0.5F);
    CHECK(scales.secondScale == 0.25F);
    REQUIRE(atomic->atomic()->pipeline != nullptr);
    CHECK(atomic->atomic()->pipeline->pluginData == coney::platform::kGameAtomicPipelineB);
    CHECK((atomic->atomic()->geometry->flags & rw::Geometry::NATIVE) != 0);
    CHECK(atomic->atomic()->getFrame()->getLTM()->pos.y == 20.0F);
}

TEST_CASE("unpacking turns the PS2 data into scaled librw geometry with its triangles", "[world_atomic]") {
    // Expected: world.md#ps2-world-geometry, inferred (positions times 0x3F0 +0x00, strips to triangles; the colour
    // doubling is Coney's choice, world.md#coneys-implementation)
    auto engine = RenderEngine::start(RenderBackend::Null, {});
    REQUIRE(engine.has_value());
    auto atomic = WorldAtomic::read(nativeAtomic(twoMeshAtomic()).span(), {});
    REQUIRE(atomic.has_value());
    atomic->unpack();
    const rw::Geometry* geometry = atomic->atomic()->geometry;
    CHECK((geometry->flags & rw::Geometry::NATIVE) == 0);
    CHECK(atomic->atomic()->pipeline == nullptr);
    CHECK(geometry->numVertices == 4); // a, b, c, d, shared by both meshes
    CHECK(geometry->numTriangles == 4);
    float maxX = 0.0F;
    for (rw::int32 i = 0; i < geometry->numVertices; ++i) {
        maxX = std::max(maxX, geometry->morphTargets[0].vertices[i].x);
    }
    CHECK(maxX == 2.0F); // 4 * 0.5
    REQUIRE(geometry->colors != nullptr);
    CHECK(geometry->colors[0].alpha == 0x80);
    CHECK(geometry->colors[0].red == 20); // the GS's 0x80 is full brightness: the packed 10 is doubled
    CHECK(geometry->colors[0].blue == 60);
    REQUIRE(geometry->texCoords[0] != nullptr);
    CHECK(geometry->meshHeader->getMeshes()[1].numIndices == 4);
}

TEST_CASE("an atomic for another pipeline or with damaged meshes is refused", "[world_atomic]") {
    // Expected: world.md#part-file, confirmed (code) (only the game's pipelines 0x30082 and 0x30083 are read)
    auto engine = RenderEngine::start(RenderBackend::Null, {});
    REQUIRE(engine.has_value());
    AtomicFields other = twoMeshAtomic();
    other.pipeline = 0x11001;
    CHECK(WorldAtomic::read(nativeAtomic(other).span(), {}).error().code == ErrorCode::Invalid);

    AtomicFields miscounted = twoMeshAtomic();
    miscounted.meshCounts[0] = 6; // the chain decodes to 5
    CHECK(WorldAtomic::read(nativeAtomic(miscounted).span(), {}).error().code == ErrorCode::Invalid);
}

TEST_CASE("cutting moves the marked triangles into an atomic of their own and keeps the rest", "[world_atomic]") {
    // Expected: unverified, #1123 (Coney's own debug tool, docs/guides/running-viewers.md#world-edits)
    auto engine = RenderEngine::start(RenderBackend::Null, {});
    REQUIRE(engine.has_value());
    auto atomic = WorldAtomic::read(nativeAtomic(twoMeshAtomic()).span(), {10.0F, 20.0F, 30.0F});
    REQUIRE(atomic.has_value());
    // Before unpacking there are no plain triangles to cut.
    const std::vector<std::uint8_t> first{1, 0, 0, 0};
    CHECK_FALSE(atomic->cut(first).has_value());
    atomic->unpack();
    // A mask of the wrong length, or one that takes nothing, changes nothing.
    CHECK_FALSE(atomic->cut(std::vector<std::uint8_t>{1, 0}).has_value());
    CHECK_FALSE(atomic->cut(std::vector<std::uint8_t>{0, 0, 0, 0}).has_value());
    CHECK(atomic->atomic()->geometry->numTriangles == 4);

    const auto piece = atomic->cut(first);
    // Read through a pointer, which the optional-access check follows past a failed REQUIRE.
    const WorldAtomic* const pieceAtomic = piece ? &*piece : nullptr;
    REQUIRE(pieceAtomic != nullptr);
    const rw::Geometry* cut = pieceAtomic->atomic()->geometry;
    CHECK(cut->numTriangles == 1);
    CHECK(cut->numVertices == 3); // only the corners it uses
    CHECK((cut->flags & rw::Geometry::NATIVE) == 0);
    REQUIRE(cut->colors != nullptr);
    CHECK(cut->numTexCoordSets == atomic->atomic()->geometry->numTexCoordSets);
    CHECK(atomic->atomic()->geometry->numTriangles == 3);
    // Its own frame, placed where the sector's is.
    CHECK(pieceAtomic->atomic()->getFrame() != atomic->atomic()->getFrame());
    CHECK(pieceAtomic->atomic()->getFrame()->getLTM()->pos.z == 30.0F);

    // Taking the rest leaves the sector with no triangle at all.
    const auto rest = atomic->cut(std::vector<std::uint8_t>{1, 1, 1});
    const WorldAtomic* const restAtomic = rest ? &*rest : nullptr;
    REQUIRE(restAtomic != nullptr);
    CHECK(restAtomic->atomic()->geometry->numTriangles == 3);
    CHECK(atomic->atomic()->geometry->numTriangles == 0);
}
