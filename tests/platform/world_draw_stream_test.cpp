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
#include <span>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "core/name_hash.h"
#include "fileio/disc.h"
#include "fileio/wad.h"
#include "graphics/asset_store.h"
#include "graphics/frame_description.h"
#include "graphics/mesh_data.h"
#include "graphics/texture_data.h"
#include "platform/draw_stream.h"
#include "platform/render_engine.h"
#include "platform/scene_assets.h"
#include "platform/world_renderer.h"
#include "platform/world_set.h"
#include "support/fixtures.h"
#include "support/optional_value.h"
#include "support/rw_fixtures.h"
#include "support/world_fixtures.h"
#include "world/sector_budget.h"
#include "world/world_streamer.h"

using coney::graphics::CullFaces;
using coney::graphics::Draw3d;
using coney::graphics::FrameDescription;
using coney::graphics::Material3d;
using coney::graphics::MeshData;
using coney::graphics::MeshId;
using coney::graphics::ScenePass;
using coney::graphics::TextureData;
using coney::graphics::TextureId;
using coney::platform::DrawStream;
using coney::platform::RenderBackend;
using coney::platform::RenderEngine;
using coney::platform::SceneAssets;
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

// The headless renderer the scene draws on, at the original's frame size.
std::unique_ptr<RenderEngine> startHeadless() {
    coney::platform::WindowDesc desc;
    desc.width = 640;
    desc.height = 448;
    auto engine = RenderEngine::start(RenderBackend::Null, desc);
    REQUIRE(engine.has_value());
    return std::move(*engine);
}

// Draws the world scene headless on a fresh renderer, recording its one frame, and returns the stream.
DrawStream drawWorldScene(const WorldScene& scene) {
    auto engine = startHeadless();
    const SceneDisc disc;
    coney::world::SectorBudget budget(10'000);
    const std::vector<std::string> names{"scene"};
    auto set = WorldSet::load(*disc.wad, names, budget, false, engine->sceneAssets());
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
        engine->requestDrawStream(0);
        renderer.render(*engine, **set, nullptr, sceneView(), 1000.0F, scene.drawMs);
        stream = engine->drawStream();
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

TEST_CASE("a loaded sector's mesh and textures are held by id until its part goes", "[platform][world][scene-assets]") {
    // Expected: docs/engine/renderer.md#frame-description: WorldSet holds a part's dictionary textures and each
    // sector's mesh in the engine's scene assets while the part is loaded; the mesh is the strip's four vertices
    // (meshFromRw, docs/engine/asset-formats.md), its one material's texture the 2 x 2 "brick" of its part.
    auto engine = startHeadless();
    const SceneDisc disc;
    coney::world::SectorBudget budget(10'000);
    const std::vector<std::string> names{"scene"};
    const SceneAssets& assets = engine->sceneAssets();
    {
        auto set = WorldSet::load(*disc.wad, names, budget, false, engine->sceneAssets());
        REQUIRE(set.has_value());
        coney::world::StreamedWorld& world = *(*set)->worlds()[0];
        auto requested = coney::world::requestPart(world, 0, 1, budget, **set, 100);
        REQUIRE(requested.has_value());
        REQUIRE(*requested);

        const WorldSet::SectorMesh* mesh = (*set)->sectorMesh(0, 0);
        REQUIRE(mesh != nullptr);
        CHECK(mesh->mesh.valid());
        CHECK_FALSE(mesh->dualLayer.valid());       // no MatFX dual material
        CHECK((*set)->sectorMesh(0, 1) == nullptr); // part 2 is not loaded
        CHECK(assets.atomic(mesh->mesh) == (*set)->atomic(0, 0));
        const MeshData* data = assets.store().mesh(mesh->mesh);
        REQUIRE(data != nullptr);
        CHECK(data->vertices.size() == 4);
        const std::span<const TextureId> textures = assets.materialTextures(mesh->mesh);
        REQUIRE(textures.size() == 1);
        const TextureData* texture = assets.store().texture(textures[0]);
        REQUIRE(texture != nullptr);
        CHECK(texture->name == "brick");
        REQUIRE_FALSE(texture->levels.empty());
        CHECK(texture->levels[0].width == 2);

        // The part goes, and its mesh and texture with it.
        const MeshId meshId = mesh->mesh;
        const TextureId textureId = textures[0];
        (*set)->unloadPart(0, 1);
        world.markPartUnloaded(1);
        budget.release(300);
        CHECK((*set)->sectorMesh(0, 0) == nullptr);
        CHECK(assets.store().mesh(meshId) == nullptr);
        CHECK(assets.store().texture(textureId) == nullptr);
    }
    CHECK(assets.store().meshCount() == 0);
    CHECK(assets.store().textureCount() == 0);
}

TEST_CASE("the world scene's sectors are drawn from the frame description", "[platform][world][frame-description]") {
    // Expected: docs/engine/renderer.md#frame-description: each sector is one 3D draw of its mesh by id with its
    // material's texture id, placed at its sector's origin (WorldSet places each atomic there), faded in (alpha 255
    // once the second after its part loaded is over: World_RenderSectorAtomic, docs/research/world.md#a-frame), with
    // texture levels by distance (docs/research/rendering.md#world), in the world pass's states and lit by the world
    // ambient at least (the light manager as it starts, docs/research/world.md#lighting).
    auto engine = startHeadless();
    const SceneDisc disc;
    coney::world::SectorBudget budget(10'000);
    const std::vector<std::string> names{"scene"};
    auto set = WorldSet::load(*disc.wad, names, budget, false, engine->sceneAssets());
    REQUIRE(set.has_value());
    coney::world::StreamedWorld& world = *(*set)->worlds()[0];
    for (const std::uint32_t part : {1U, 2U}) {
        auto requested = coney::world::requestPart(world, 0, part, budget, **set, 100);
        REQUIRE(requested.has_value());
        REQUIRE(*requested);
    }
    {
        WorldRenderer renderer;
        renderer.recordHeadless(true);
        engine->requestDrawStream(0);
        renderer.render(*engine, **set, nullptr, sceneView(), 1000.0F, 2000);
    }
    const FrameDescription& frame = engine->frame();
    const ScenePass* scene = nullptr;
    for (const auto& pass : frame.passes()) {
        if (const auto* found = std::get_if<ScenePass>(&pass); found != nullptr) {
            scene = found;
        }
    }
    REQUIRE(scene != nullptr);
    REQUIRE(scene->draws.size() == 2);
    for (const Draw3d& draw : scene->draws) {
        const bool first = draw.mesh == (*set)->sectorMesh(0, 0)->mesh;
        CHECK((first || draw.mesh == (*set)->sectorMesh(0, 1)->mesh));
        CHECK(draw.transform.position[0] == (first ? 5.0F : 15.0F));
        CHECK(draw.transform.position[1] == 5.0F);
        CHECK(draw.material == Material3d{.fade = 255, .distanceLevels = true});
        CHECK(draw.states.depthTest);
        CHECK(draw.states.depthWrite);
        CHECK(draw.states.cull == CullFaces::Back);
        CHECK(draw.states.fog);
        CHECK(draw.states.alphaBlend);
        REQUIRE(frame.texturesOf(draw).size() == 1);
        const TextureData* texture = engine->sceneAssets().store().texture(frame.texturesOf(draw)[0]);
        REQUIRE(texture != nullptr);
        CHECK(texture->name == (first ? "brick" : "slate"));
        CHECK_FALSE(frame.lightsOf(draw).empty());
    }
}
