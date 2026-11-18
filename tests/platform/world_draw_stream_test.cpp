// SPDX-License-Identifier: GPL-3.0-or-later
// The world scene of the draw-stream hashes (docs/engine/renderer.md#comparing): a Coney-made streamed world of two
// textured sectors, loaded through WorldSet and drawn headless by WorldRenderer while the draw layer records. No game
// data: every vertex, texel and name is made here.
//
// The expected hash is Coney's own: it was recorded from this scene when the world's atomic renders were routed
// through the draw layer (item 7's first commit), and the move of the sectors onto the frame description must leave it
// as it is. A change to it fails CI until the pull request explains it and updates the hash. The original has no
// counterpart.
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "core/name_hash.h"
#include "fileio/disc.h"
#include "fileio/wad.h"
#include "platform/draw_stream.h"
#include "platform/render_engine.h"
#include "platform/world_renderer.h"
#include "platform/world_set.h"
#include "support/fixtures.h"
#include "support/optional_value.h"
#include "support/rw_fixtures.h"
#include "support/world_fixtures.h"
#include "world/sector_budget.h"
#include "world/world_streamer.h"

using coney::platform::DrawStream;
using coney::platform::RenderBackend;
using coney::platform::RenderEngine;
using coney::platform::WorldRenderer;
using coney::platform::WorldSet;
using coney::platform::WorldView;
using coney::test::Bytes;
using coney::test::TempDir;

namespace {

// One part atomic: a four-vertex strip, two triangles, positions scaled by 0.5, its one material textured `texture`.
Bytes texturedStrip(const std::string& texture) {
    coney::test::AtomicFields fields;
    fields.meshChains.push_back(
        coney::test::ps2MeshChain({{coney::test::ps2Vertex(0, 0, 0), coney::test::ps2Vertex(4, 0, 0),
                                    coney::test::ps2Vertex(0, 4, 0), coney::test::ps2Vertex(4, 4, 0)}},
                                  true));
    fields.meshCounts.push_back(4);
    fields.triangles = 2;
    fields.texture = texture;
    return coney::test::nativeAtomic(fields);
}

// A synthetic disc holding one streamed world, `scene`: two sectors side by side (x 0..10 and 10..20), each the only
// sector of its part, each part with a texture dictionary of one 2 x 2 texture its atomic's material names.
std::vector<std::byte> buildSceneDisc() {
    std::vector<Bytes> leaves{coney::test::worldSector({0, 0, 0}, {10, 10, 10}, 0, 1, {5, 5, 5}),
                              coney::test::worldSector({10, 0, 0}, {20, 10, 10}, 1, 2, {15, 5, 5})};
    const Bytes stream = coney::test::worldStream(2, leaves, 1, 2);
    const Bytes part1 = coney::test::partFile(0, {{0, texturedStrip("brick")}},
                                              coney::test::texDictionary({coney::test::rgbaTexture2x2("brick")}));
    const Bytes part2 = coney::test::partFile(0, {{1, texturedStrip("slate")}},
                                              coney::test::texDictionary({coney::test::rgbaTexture2x2("slate")}));
    Bytes manifest;
    manifest.u32(static_cast<std::uint32_t>(stream.size())).u32(1000);
    manifest.u32(2).u32(static_cast<std::uint32_t>(part1.size())).u32(300);
    manifest.u32(static_cast<std::uint32_t>(part2.size())).u32(400);

    Bytes wad;
    Bytes dir;
    dir.header(4, 0, 0, 0);
    for (const auto& [name, data] :
         {std::pair<const char*, const Bytes*>{"scene_sec.mem", &manifest}, std::pair{"scene_sec.wld", &stream},
          std::pair{"scene_ms1.sec", &part1}, std::pair{"scene_ms2.sec", &part2}}) {
        dir.u32(static_cast<std::uint32_t>(wad.size()))
            .u32(static_cast<std::uint32_t>(data->size()))
            .u32(coney::nameHash(std::string("./ee_files/") + name));
        wad.append(data->span());
        wad.padTo((wad.size() + 2047) / 2048 * 2048);
    }
    return coney::test::buildIso({{"WARRIORS.DIR;1", dir.data()}, {"WARRIORS.WAD;1", wad.data()}});
}

// The scene's archive, opened from the synthetic disc written into a folder of its own. Held by pointer: clang-tidy's
// optional-access check does not see the REQUIRE that ends the test when opening fails.
struct SceneDisc {
    TempDir folder;
    std::unique_ptr<coney::io::Wad> wad;
    SceneDisc() {
        const auto image = folder.write("disc.iso", buildSceneDisc());
        auto disc = coney::io::Disc::open(image);
        REQUIRE(disc.has_value());
        auto opened = coney::io::Wad::open(std::move(*disc));
        REQUIRE(opened.has_value());
        wad = std::make_unique<coney::io::Wad>(std::move(*opened));
    }
};

// When the scene's parts load and when it is drawn: both parts at 100 ms (each fades in until 1100 ms), drawn at
// `drawMs`.
struct WorldScene {
    std::uint64_t drawMs = 2000; // both sectors faded in
};

// The camera the scene is drawn through: 20 units in front of both sectors, looking at them along RenderWare's z.
WorldView sceneView() {
    WorldView view;
    view.pose.position = {10.0F, 5.0F, -20.0F};
    view.halfWidth = 0.6F;
    view.halfHeight = 0.45F;
    view.nearClip = 0.5F;
    view.drawDistance = 100.0F;
    return view;
}

// Draws the world scene headless on a fresh renderer, recording its one frame, and returns the stream.
DrawStream drawWorldScene(const WorldScene& scene) {
    coney::platform::WindowDesc desc;
    desc.width = 640;
    desc.height = 448;
    auto engine = RenderEngine::start(RenderBackend::Null, desc);
    REQUIRE(engine.has_value());
    const SceneDisc disc;
    coney::world::SectorBudget budget(10'000);
    const std::vector<std::string> names{"scene"};
    auto set = WorldSet::load(*disc.wad, names, budget, false);
    REQUIRE(set.has_value());
    coney::world::StreamedWorld& world = *(*set)->worlds()[0];
    for (const std::uint32_t part : {1U, 2U}) {
        auto requested = coney::world::requestPart(world, 0, part, budget, **set, 100);
        REQUIRE(requested.has_value());
        REQUIRE(*requested);
    }
    std::optional<DrawStream> stream;
    {
        WorldRenderer renderer;
        renderer.recordHeadless(true);
        (*engine)->requestDrawStream(0);
        renderer.render(**engine, **set, nullptr, sceneView(), 1000.0F, scene.drawMs);
        stream = (*engine)->drawStream();
    }
    REQUIRE(stream.has_value());
    return coney::test::got(stream);
}

} // namespace

TEST_CASE("the world scene's draw stream has its expected hash", "[platform][draw-stream][world]") {
    // Expected: recorded from this scene when the world's renders were routed through the draw layer
    // (docs/engine/renderer.md#comparing); the move onto the frame description keeps it.
    const DrawStream stream = drawWorldScene(WorldScene{});
    // Both sectors, one draw each: no dual layer (their material has no MatFX), no background (no level).
    CHECK(stream.counts().atomicDraws == 2);
    CHECK(stream.counts().vertices == 8); // each sector's four strip vertices
    CHECK(stream.hash() == 0x09fed123f922c187ULL);
}

TEST_CASE("the world scene hashes the same twice and sees a sector's fade", "[platform][draw-stream][world]") {
    // Expected: docs/engine/renderer.md#comparing: a recording depends on the scene alone, and an atomic draw holds
    // its materials' colours, whose alpha is the sector's fade-in (World_RenderSectorAtomic,
    // docs/research/world.md#a-frame).
    const std::uint64_t faded = drawWorldScene(WorldScene{}).hash();
    CHECK(drawWorldScene(WorldScene{}).hash() == faded);
    CHECK(drawWorldScene(WorldScene{.drawMs = 600}).hash() != faded); // half way through the fade
}
