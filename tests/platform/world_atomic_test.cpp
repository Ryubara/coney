// SPDX-License-Identifier: GPL-3.0-or-later
#include <algorithm>

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
    auto engine = RenderEngine::start(RenderBackend::Null, {});
    REQUIRE(engine.has_value());
    AtomicFields other = twoMeshAtomic();
    other.pipeline = 0x11001;
    CHECK(WorldAtomic::read(nativeAtomic(other).span(), {}).error().code == ErrorCode::Invalid);

    AtomicFields miscounted = twoMeshAtomic();
    miscounted.meshCounts[0] = 6; // the chain decodes to 5
    CHECK(WorldAtomic::read(nativeAtomic(miscounted).span(), {}).error().code == ErrorCode::Invalid);
}
