// SPDX-License-Identifier: GPL-3.0-or-later
// The world scene of the draw-stream hashes (docs/engine/renderer.md#comparing): a Coney-made streamed world of two
// textured sectors, loaded through WorldSet and drawn headless by WorldRenderer while the draw layer records. No game
// data: every vertex, texel and name is made here.
//
// The expected hash is Coney's own: it was recorded from this scene when the world's atomic renders were routed
// through the draw layer (item 7's first commit), and the move of the sectors onto the frame description must leave it
// as it is. A change to it fails CI until the pull request explains it and updates the hash. The original has no
// counterpart.
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
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
#include "platform/level_file.h"
#include "platform/mesh_view_hook.h"
#include "platform/perf_probes.h"
#include "platform/render_engine.h"
#include "platform/scene_assets.h"
#include "platform/texture_dictionary.h"
#include "platform/world_atomic.h"
#include "platform/world_renderer.h"
#include "platform/world_set.h"
#include "support/fixtures.h"
#include "support/mesh_view_hide_hook.h"
#include "support/optional_value.h"
#include "support/rw_fixtures.h"
#include "support/world_fixtures.h"
#include "world/level_object.h"
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

// The scene's sectors drawn on `engine`, recorded as the play mode records a frame with `hook` offered to the lighting
// rig and the renderer (`passes`: the draws go through the rig for the hook's own passes, as on GL3), inside the
// scenery's draw scope; how many sector draws the frame description holds then.
std::size_t sectorDraws(RenderEngine& engine, bool headless, coney::platform::MeshViewHook* hook, bool passes) {
    const SceneDisc disc;
    coney::world::SectorBudget budget(10'000);
    const std::vector<std::string> names{"scene"};
    auto set = WorldSet::load(*disc.wad, names, budget, false, engine.sceneAssets());
    REQUIRE(set.has_value());
    coney::world::StreamedWorld& world = *(*set)->worlds()[0];
    for (const std::uint32_t part : {1U, 2U}) {
        auto requested = coney::world::requestPart(world, 0, part, budget, **set, 100);
        REQUIRE(requested.has_value());
        REQUIRE(*requested);
    }
    {
        WorldRenderer renderer;
        renderer.recordHeadless(headless);
        renderer.lighting().setMeshViews(hook, passes);
        engine.hideKindsOf(hook);
        {
            const coney::platform::DrawKindScope kind(coney::platform::DrawKind::Scenery);
            renderer.render(engine, **set, nullptr, sceneView(), 1000.0F, 2000);
        }
        engine.hideKindsOf(nullptr);
        renderer.lighting().setMeshViews(nullptr);
    }
    std::size_t draws = 0;
    for (const auto& pass : engine.frame().passes()) {
        if (const auto* found = std::get_if<ScenePass>(&pass); found != nullptr) {
            draws += found->draws.size();
        }
    }
    return draws;
}

} // namespace

TEST_CASE("the world scene's draw stream has its expected hash", "[platform][draw-stream][world]") {
    // Expected: ../engine/renderer.md#comparing, Coney's own recording: taken from this scene when the world's
    // renders were routed through the draw layer; the move onto the frame description keeps it. Taken again when the
    // headless layer's unset states began to read back as librw's GL3 device starts them (#1205): the alpha test the
    // sectors record is then GEQUAL, as live.
    const DrawStream stream = drawWorldScene(WorldScene{});
    // Both sectors, one draw each: no dual layer (their material has no MatFX), no background (no level).
    CHECK(stream.counts().atomicDraws == 2);
    CHECK(stream.counts().vertices == 8); // each sector's four strip vertices
    CHECK(stream.hash() == 0x0d90e4b7a17f8c7fULL);
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

TEST_CASE("the world scene's sectors are seen through the world pass's camera",
          "[platform][world][frame-description]") {
    // Expected: ../engine/renderer.md#frame-description: the scene pass carries the camera placeWorldCamera() gives
    // librw (platform/world_renderer.cpp): the view's position and axes, its view window, the near clip, the draw
    // distance as the far clip and the fog from the level's fog start of it; the view is the test's own (sceneView()).
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
    float fogStart = 0.0F;
    {
        WorldRenderer renderer;
        renderer.recordHeadless(true);
        renderer.render(*engine, **set, nullptr, sceneView(), 1000.0F, 2000);
        fogStart = renderer.lighting().fog().start;
    }
    const ScenePass* scene = nullptr;
    for (const auto& pass : engine->frame().passes()) {
        if (const auto* found = std::get_if<ScenePass>(&pass); found != nullptr) {
            scene = found;
        }
    }
    REQUIRE(scene != nullptr);
    CHECK(scene->draws.size() == 2);
    REQUIRE(scene->camera.has_value());
    const coney::graphics::SceneCamera& camera = coney::test::got(scene->camera);
    CHECK(camera.frame.position == std::array<float, 3>{10.0F, 5.0F, -20.0F});
    CHECK(camera.frame.right == std::array<float, 3>{-1.0F, 0.0F, 0.0F}); // the pose's: the screen's right
    CHECK(camera.frame.up == std::array<float, 3>{0.0F, 1.0F, 0.0F});
    CHECK(camera.frame.at == std::array<float, 3>{0.0F, 0.0F, 1.0F});
    CHECK(camera.halfWidth == 0.6F);
    CHECK(camera.halfHeight == 0.45F);
    CHECK(camera.nearClip == 0.5F);
    CHECK(camera.farClip == 100.0F);
    CHECK(camera.fogStart == 100.0F * fogStart);
}

TEST_CASE("on the GPU backend the world scene's sectors are drawn from the frame description",
          "[platform][world][frame-description][window]") {
    // Expected: ../engine/gpu-renderer.md#scene: with the GPU backend, which has no librw camera, the world pass still
    // adds each sector to the frame description, through its camera, for the GPU renderer to draw. Needs SDL's video
    // and a GPU device, and skips where there is none.
    coney::platform::WindowDesc desc;
    desc.activate = false;
    desc.hidden = true;
    desc.width = 320;
    desc.height = 224;
    auto started = RenderEngine::start(RenderBackend::Gpu, desc);
    if (!started || (*started)->backend() != RenderBackend::Gpu) {
        SKIP("no display or no GPU device here");
    }
    RenderEngine& engine = **started;
    const SceneDisc disc;
    coney::world::SectorBudget budget(10'000);
    const std::vector<std::string> names{"scene"};
    auto set = WorldSet::load(*disc.wad, names, budget, false, engine.sceneAssets());
    REQUIRE(set.has_value());
    coney::world::StreamedWorld& world = *(*set)->worlds()[0];
    for (const std::uint32_t part : {1U, 2U}) {
        auto requested = coney::world::requestPart(world, 0, part, budget, **set, 100);
        REQUIRE(requested.has_value());
        REQUIRE(*requested);
    }
    {
        WorldRenderer renderer;
        renderer.render(engine, **set, nullptr, sceneView(), 1000.0F, 2000);
    }
    const ScenePass* scene = nullptr;
    for (const auto& pass : engine.frame().passes()) {
        if (const auto* found = std::get_if<ScenePass>(&pass); found != nullptr) {
            scene = found;
        }
    }
    REQUIRE(scene != nullptr);
    CHECK(scene->draws.size() == 2);
    CHECK(scene->camera.has_value());
}

TEST_CASE("the scene assets count what they converted and hold", "[platform][world][scene-assets]") {
    // Expected: docs/engine/gpu-renderer.md#scene-assets: the conversion's cost is the textures and meshes converted,
    // the bytes the store holds of them (the CPU copy beside librw's) and the most it held at once. The scene's part 1
    // is one 2 x 2 RGBA texture (16 bytes at level 0, so at least that) and one four-vertex mesh; part 2 the same
    // again.
    auto engine = startHeadless();
    const SceneDisc disc;
    coney::world::SectorBudget budget(10'000);
    const std::vector<std::string> names{"scene"};
    const SceneAssets& assets = engine->sceneAssets();
    auto set = WorldSet::load(*disc.wad, names, budget, false, engine->sceneAssets());
    REQUIRE(set.has_value());
    coney::world::StreamedWorld& world = *(*set)->worlds()[0];
    for (const std::uint32_t part : {1U, 2U}) {
        auto requested = coney::world::requestPart(world, 0, part, budget, **set, 100);
        REQUIRE(requested.has_value());
        REQUIRE(*requested);
    }
    const SceneAssets::Totals loaded = assets.totals();
    CHECK(loaded.textures == 2);
    CHECK(loaded.meshes == 2);
    // The floors are computed in the byte counters' own 64-bit type, so no int product is widened.
    constexpr std::uint64_t parts = 2;
    constexpr std::uint64_t textureFloor = parts * std::uint64_t{16};
    constexpr std::uint64_t meshFloor = parts * std::uint64_t{4} * sizeof(coney::graphics::MeshVertex);
    CHECK(loaded.textureBytes >= textureFloor);
    CHECK(loaded.meshBytes >= meshFloor);
    CHECK(loaded.peakBytes == loaded.textureBytes + loaded.meshBytes);

    // One part goes: what is held falls by what it held, the counts of what was converted and the peak stay.
    (*set)->unloadPart(0, 1);
    world.markPartUnloaded(1);
    budget.release(300);
    const SceneAssets::Totals unloaded = assets.totals();
    CHECK(unloaded.textures == 2);
    CHECK(unloaded.meshes == 2);
    CHECK(unloaded.textureBytes < loaded.textureBytes);
    CHECK(unloaded.meshBytes < loaded.meshBytes);
    CHECK(unloaded.peakBytes == loaded.peakBytes);
}

TEST_CASE("scene assets that do not convert hand out mesh ids for the atomics and hold no data",
          "[platform][world][scene-assets]") {
    // Expected: docs/engine/gpu-renderer.md#scene-assets: the GL3 renderer draws the librw atomic a mesh id names and
    // reads none of the converted data, so with no conversion a sector still has its mesh id and atomic, and nothing of
    // it is converted or held (no second CPU copy of the textures and vertices).
    auto engine = startHeadless();
    const SceneDisc disc;
    coney::world::SectorBudget budget(10'000);
    const std::vector<std::string> names{"scene"};
    SceneAssets plain(false);
    CHECK_FALSE(plain.converting());
    auto set = WorldSet::load(*disc.wad, names, budget, false, plain);
    REQUIRE(set.has_value());
    coney::world::StreamedWorld& world = *(*set)->worlds()[0];
    for (const std::uint32_t part : {1U, 2U}) {
        auto requested = coney::world::requestPart(world, 0, part, budget, **set, 100);
        REQUIRE(requested.has_value());
        REQUIRE(*requested);
    }
    const WorldSet::SectorMesh* first = (*set)->sectorMesh(0, 0);
    const WorldSet::SectorMesh* second = (*set)->sectorMesh(0, 1);
    REQUIRE(first != nullptr);
    REQUIRE(second != nullptr);
    CHECK(first->mesh.valid());
    CHECK(first->mesh != second->mesh);
    CHECK(plain.atomic(first->mesh) == (*set)->atomic(0, 0));
    CHECK(plain.atomic(second->mesh) == (*set)->atomic(0, 1));
    CHECK(plain.store().meshCount() == 0);
    CHECK(plain.store().textureCount() == 0);
    const SceneAssets::Totals totals = plain.totals();
    CHECK(totals.textures == 0);
    CHECK(totals.meshes == 0);
    CHECK(totals.peakBytes == 0);

    // The part goes, and the mesh id's atomic with it.
    const coney::graphics::MeshId gone = first->mesh;
    (*set)->unloadPart(0, 1);
    world.markPartUnloaded(1);
    budget.release(300);
    CHECK(plain.atomic(gone) == nullptr);
}

TEST_CASE("only GL3 does not convert the scene assets", "[platform][scene-assets]") {
    // Expected: docs/engine/gpu-renderer.md#scene-assets: nothing reads the converted data under GL3, which draws the
    // librw atomics; the NULL backend's tests and the GPU renderer read it.
    CHECK_FALSE(RenderEngine::convertsSceneAssets(RenderBackend::OpenGl));
    CHECK(RenderEngine::convertsSceneAssets(RenderBackend::Null));
    CHECK(RenderEngine::convertsSceneAssets(RenderBackend::Gpu));
    auto engine = startHeadless();
    CHECK(engine->sceneAssets().converting());
}

namespace {

// A level model or the level world as the level file's readers make one (platform/level_file.cpp): a strip atomic
// with no light flag and no texture named, read in the game's layout, placed at `at` and unpacked, linked to a
// dictionary of one 2 x 2 texture (LevelObject_LinkModel) when `dictionary` is given.
std::unique_ptr<coney::chunk::LoadedObject> levelModel(coney::world::Vec3 at, std::string_view what,
                                                       coney::chunk::LoadedObject* dictionary) {
    coney::test::AtomicFields fields;
    fields.meshChains.push_back(
        coney::test::ps2MeshChain({{coney::test::ps2Vertex(-2, -2, 0), coney::test::ps2Vertex(2, -2, 0),
                                    coney::test::ps2Vertex(-2, 2, 0), coney::test::ps2Vertex(2, 2, 0)}},
                                  true));
    fields.meshCounts.push_back(4);
    fields.triangles = 2;
    const Bytes section = coney::test::nativeAtomic(fields);
    auto atomic = coney::platform::WorldAtomic::read(section.span(), coney::world::Vec3{});
    REQUIRE(atomic.has_value());
    const coney::world::FrameMatrix frame{.position = at};
    atomic->setTransform(frame);
    atomic->unpack();
    auto model = std::make_unique<coney::platform::LevelAtomicObject>(std::move(*atomic), frame, what);
    if (dictionary != nullptr) {
        REQUIRE(coney::platform::linkLevelModel(*model, *dictionary).has_value());
    }
    return model;
}

// A dictionary of one 2 x 2 texture named `name`, as the level file's chunk 0x0B holds one.
std::unique_ptr<coney::chunk::LoadedObject> levelDictionary(const std::string& name) {
    const Bytes stream = coney::test::texDictionary({coney::test::rgbaTexture2x2(name)});
    auto dictionary = coney::platform::TextureDictionary::read(stream.span());
    REQUIRE(dictionary.has_value());
    return std::make_unique<coney::platform::TextureDictionaryObject>(std::move(*dictionary));
}

// A Coney-made level object with the four parts the background draws: the sky box and the cloud box round the origin,
// the skyline out at 100 along z, the level world (the light glows) beside the sectors; no collision, paths or
// occluders.
std::unique_ptr<coney::world::LevelObject> backgroundLevel() {
    auto level = std::make_unique<coney::world::LevelObject>();
    const auto addModel = [](coney::world::LevelModel& model, const std::string& name, coney::world::Vec3 at) {
        model.dictionary = levelDictionary(name);
        model.model = levelModel(at, "level model", model.dictionary.get());
    };
    addModel(level->skyBox, "sky", coney::world::Vec3{});
    addModel(level->cloudBox, "clouds", coney::world::Vec3{});
    addModel(level->skyline, "skyline", coney::world::Vec3{10.0F, 5.0F, 100.0F});
    level->levelWorld = levelModel(coney::world::Vec3{10.0F, 5.0F, 5.0F}, "level world", nullptr);
    return level;
}

// The meshes of a level object's part, held by holdLevelMeshes().
coney::platform::ObjectMesh meshesOf(const std::unique_ptr<coney::chunk::LoadedObject>& model) {
    const auto* object = dynamic_cast<const coney::platform::LevelAtomicObject*>(model.get());
    REQUIRE(object != nullptr);
    return object->meshes();
}

// The scene passes of `frame`, in their order.
std::vector<const ScenePass*> scenePassesOf(const FrameDescription& frame) {
    std::vector<const ScenePass*> scenes;
    for (const auto& pass : frame.passes()) {
        if (const auto* found = std::get_if<ScenePass>(&pass); found != nullptr) {
            scenes.push_back(found);
        }
    }
    return scenes;
}

// The world scene's sectors, both parts loaded at 100 ms, on `engine`'s scene assets.
std::unique_ptr<WorldSet> loadedScene(RenderEngine& engine, const SceneDisc& disc, coney::world::SectorBudget& budget) {
    const std::vector<std::string> names{"scene"};
    auto set = WorldSet::load(*disc.wad, names, budget, false, engine.sceneAssets());
    REQUIRE(set.has_value());
    coney::world::StreamedWorld& world = *(*set)->worlds()[0];
    for (const std::uint32_t part : {1U, 2U}) {
        auto requested = coney::world::requestPart(world, 0, part, budget, **set, 100);
        REQUIRE(requested.has_value());
        REQUIRE(*requested);
    }
    return std::move(*set);
}

// The scene's sectors and the level's background (the sky, the clouds, the skyline, the glows) drawn on `engine` as the
// play mode draws them: with `hook` offered to the lighting rig and the renderer (`passes`: as sectorDraws()), inside
// the scenery's draw scope; how many draws the frame description holds then.
std::size_t levelDraws(RenderEngine& engine, bool headless, coney::platform::MeshViewHook* hook, bool passes) {
    const SceneDisc disc;
    coney::world::SectorBudget budget(10'000);
    std::unique_ptr<WorldSet> set = loadedScene(engine, disc, budget);
    std::unique_ptr<coney::world::LevelObject> level = backgroundLevel();
    coney::platform::holdLevelMeshes(*level, engine.sceneAssets());
    {
        WorldRenderer renderer;
        renderer.recordHeadless(headless);
        renderer.lighting().setMeshViews(hook, passes);
        engine.hideKindsOf(hook);
        {
            const coney::platform::DrawKindScope kind(coney::platform::DrawKind::Scenery);
            renderer.render(engine, *set, level.get(), sceneView(), 1000.0F, 2000);
        }
        engine.hideKindsOf(nullptr);
        renderer.lighting().setMeshViews(nullptr);
    }
    std::size_t draws = 0;
    for (const ScenePass* pass : scenePassesOf(engine.frame())) {
        draws += pass->draws.size();
    }
    return draws;
}

} // namespace

TEST_CASE("the level's background is recorded before the world, through its own cameras",
          "[platform][world][frame-description]") {
    // Expected: docs/research/level-loading.md#render-order (LevelObject_RenderBackground 0x0040d0a8, confirmed
    // (code)): the sky box, then the cloud box, round the camera with its translation zeroed (near 0.05, far 5), Z
    // write and fog off and nothing culled; then the skyline in place from 39 (the nearest missing scenery is farther)
    // to 560, Z write on and fog off; then Z alone cleared; then the world pass (WorldManager_Render 0x0040e8d8,
    // docs/research/world.md#a-frame): the level world's glows first, nothing culled, Z test and write and fog on,
    // then the sectors. Each background part is lit (the world's ambient, docs/research/lighting.md
    // #coneys-implementation) and keeps its material colours. Coney's frame description carries them as scene passes
    // (docs/engine/gpu-renderer.md#scene).
    auto engine = startHeadless();
    const SceneDisc disc;
    coney::world::SectorBudget budget(10'000);
    std::unique_ptr<WorldSet> set = loadedScene(*engine, disc, budget);
    std::unique_ptr<coney::world::LevelObject> level = backgroundLevel();
    coney::platform::holdLevelMeshes(*level, engine->sceneAssets());
    {
        WorldRenderer renderer;
        renderer.recordHeadless(true);
        renderer.render(*engine, *set, level.get(), sceneView(), 1000.0F, 2000);
    }
    const FrameDescription& frame = engine->frame();
    const std::vector<const ScenePass*> scenes = scenePassesOf(frame);
    REQUIRE(scenes.size() == 3);

    // The sky and the clouds round the camera.
    const ScenePass& sky = *scenes[0];
    REQUIRE(sky.camera.has_value());
    const coney::graphics::SceneCamera& skyCamera = coney::test::got(sky.camera);
    CHECK(skyCamera.frame.position == std::array<float, 3>{0.0F, 0.0F, 0.0F});
    CHECK(skyCamera.frame.at == std::array<float, 3>{0.0F, 0.0F, 1.0F});
    CHECK(skyCamera.nearClip == coney::platform::kSkyNearClip);
    CHECK(skyCamera.farClip == coney::platform::kSkyFarClip);
    CHECK_FALSE(sky.clearDepth);
    REQUIRE(sky.draws.size() == 2);
    CHECK(sky.draws[0].mesh == meshesOf(level->skyBox.model).mesh);
    CHECK(sky.draws[1].mesh == meshesOf(level->cloudBox.model).mesh);
    for (const Draw3d& draw : sky.draws) {
        CHECK(draw.mesh.valid());
        CHECK_FALSE(draw.states.depthWrite);
        CHECK_FALSE(draw.states.fog);
        CHECK(draw.states.cull == CullFaces::None);
        CHECK(draw.material == Material3d{.colour = coney::graphics::MaterialColour::Own, .lit = true});
        CHECK_FALSE(frame.lightsOf(draw).empty());
    }
    REQUIRE(frame.texturesOf(sky.draws[0]).size() == 1);
    const TextureData* skyTexture = engine->sceneAssets().store().texture(frame.texturesOf(sky.draws[0])[0]);
    REQUIRE(skyTexture != nullptr);
    CHECK(skyTexture->name == "sky");

    // The skyline in place, out to 560.
    const ScenePass& skyline = *scenes[1];
    REQUIRE(skyline.camera.has_value());
    const coney::graphics::SceneCamera& skylineCamera = coney::test::got(skyline.camera);
    CHECK(skylineCamera.frame.position == std::array<float, 3>{10.0F, 5.0F, -20.0F});
    CHECK(skylineCamera.nearClip == coney::platform::kSkylineNearClip);
    CHECK(skylineCamera.farClip == coney::platform::kSkylineFarClip);
    CHECK_FALSE(skyline.clearDepth);
    REQUIRE(skyline.draws.size() == 1);
    CHECK(skyline.draws[0].mesh == meshesOf(level->skyline.model).mesh);
    CHECK(skyline.draws[0].states.depthWrite);
    CHECK_FALSE(skyline.draws[0].states.fog);
    CHECK(skyline.draws[0].material.lit);

    // Z cleared, then the world pass through its own camera: the glows, then both sectors.
    const ScenePass& world = *scenes[2];
    CHECK(world.clearDepth);
    REQUIRE(world.camera.has_value());
    CHECK(coney::test::got(world.camera).farClip == 100.0F);
    REQUIRE(world.draws.size() == 3);
    const Draw3d& glows = world.draws[0];
    CHECK(glows.mesh == meshesOf(level->levelWorld).mesh);
    CHECK(glows.states.cull == CullFaces::None);
    CHECK(glows.states.depthWrite);
    CHECK(glows.states.fog);
    CHECK(glows.material.lit);
    const auto sector = [&set](const Draw3d& draw) {
        return draw.mesh == set->sectorMesh(0, 0)->mesh || draw.mesh == set->sectorMesh(0, 1)->mesh;
    };
    CHECK(sector(world.draws[1]));
    CHECK(sector(world.draws[2]));

    // The level goes, and its meshes and textures with it: the sectors' stay.
    level.reset();
    CHECK(engine->sceneAssets().store().meshCount() == 2);
    CHECK(engine->sceneAssets().store().textureCount() == 2);
}

TEST_CASE("the world scene with a level's background keeps its draw stream", "[platform][draw-stream][world]") {
    // Expected: ../engine/renderer.md#comparing, Coney's own recording: taken from this scene before the background
    // moved onto the frame description, when the world renderer drew its models straight through the lighting rig;
    // the GL3 renderer drawing them from the description makes the same calls, so the hash stays.
    auto engine = startHeadless();
    const SceneDisc disc;
    coney::world::SectorBudget budget(10'000);
    std::unique_ptr<WorldSet> set = loadedScene(*engine, disc, budget);
    std::unique_ptr<coney::world::LevelObject> level = backgroundLevel();
    coney::platform::holdLevelMeshes(*level, engine->sceneAssets());
    std::optional<DrawStream> stream;
    {
        WorldRenderer renderer;
        renderer.recordHeadless(true);
        engine->requestDrawStream(0);
        renderer.render(*engine, *set, level.get(), sceneView(), 1000.0F, 2000);
        stream = engine->drawStream();
    }
    REQUIRE(stream.has_value());
    const DrawStream& got = coney::test::got(stream);
    // The sky, the clouds, the skyline, the glows and both sectors.
    CHECK(got.counts().atomicDraws == 6);
    CHECK(got.hash() == 0x1f8347c5f40c2e0dULL);
}

TEST_CASE("on the GPU backend the level's background is recorded before the world's sectors",
          "[platform][world][frame-description][window]") {
    // Expected: ../engine/gpu-renderer.md#scene and docs/research/level-loading.md#render-order: with the GPU backend
    // the background's models go into the frame description too, before the world pass, whose pass clears Z first.
    // Needs SDL's video and a GPU device, and skips where there is none.
    coney::platform::WindowDesc desc;
    desc.activate = false;
    desc.hidden = true;
    desc.width = 320;
    desc.height = 224;
    auto started = RenderEngine::start(RenderBackend::Gpu, desc);
    if (!started || (*started)->backend() != RenderBackend::Gpu) {
        SKIP("no display or no GPU device here");
    }
    RenderEngine& engine = **started;
    const SceneDisc disc;
    coney::world::SectorBudget budget(10'000);
    std::unique_ptr<WorldSet> set = loadedScene(engine, disc, budget);
    std::unique_ptr<coney::world::LevelObject> level = backgroundLevel();
    coney::platform::holdLevelMeshes(*level, engine.sceneAssets());
    {
        WorldRenderer renderer;
        renderer.render(engine, *set, level.get(), sceneView(), 1000.0F, 2000);
    }
    const std::vector<const ScenePass*> scenes = scenePassesOf(engine.frame());
    REQUIRE(scenes.size() == 3);
    CHECK(scenes[0]->draws.size() == 2);
    CHECK(scenes[1]->draws.size() == 1);
    CHECK(scenes[2]->clearDepth);
    CHECK(scenes[2]->draws.size() == 3);
}

TEST_CASE("hiding the scenery leaves the sectors out of the frame description, and no other kind does",
          "[platform][world][frame-description][world-page]") {
    // Expected: docs/engine/gpu-renderer.md#scene: the World page's Streamed world switch hides the sectors
    // alone: the renderer leaves out a recorded draw of a kind the hook hides (RenderEngine::hideKindsOf()), and a hook
    // that hides another kind, or none, leaves them as recorded. Coney's own debug tooling (the original has no page);
    // the NULL backend records as the GPU backend does, from the same call.
    using coney::platform::DrawKind;
    using coney::test::HideHook;
    auto engine = startHeadless();
    CHECK(sectorDraws(*engine, true, nullptr, true) == 2);
    HideHook scenery({DrawKind::Scenery});
    CHECK(sectorDraws(*engine, true, &scenery, false) == 0);
    HideHook humans({DrawKind::Humans});
    CHECK(sectorDraws(*engine, true, &humans, false) == 2);
    HideHook objects({DrawKind::Objects});
    CHECK(sectorDraws(*engine, true, &objects, false) == 2);
    HideHook both({DrawKind::Humans, DrawKind::Objects});
    CHECK(sectorDraws(*engine, true, &both, false) == 2);
}

TEST_CASE("a mesh view's passes do not take the sectors out of a renderer that cannot draw them",
          "[platform][world][frame-description][world-page]") {
    // Expected: docs/engine/gpu-renderer.md#scene: mesh passes are GL3's (librw draws them); a renderer that
    // records draws keeps recording the shaded sector while a view is on (the play mode says so with passes off, see
    // drawsMeshViewPassesOn()), instead of sending it through librw, which draws nothing there.
    using coney::test::HideHook;
    auto engine = startHeadless();
    HideHook viewing({}, true);
    CHECK(sectorDraws(*engine, true, &viewing, false) == 2);
    // The passes asked for (GL3): the sectors go through the rig for the hook, so nothing is recorded.
    CHECK(sectorDraws(*engine, true, &viewing, true) == 0);
}

TEST_CASE("only the GPU backend leaves the mesh views' passes to GL3", "[platform][world-page]") {
    // Expected: docs/engine/gpu-renderer.md#scene: GL3 draws the passes through librw, the NULL backend keeps
    // asking the hook (the tests see it wired), the GPU renderer has no librw camera to draw them with.
    CHECK(coney::platform::drawsMeshViewPassesOn(RenderBackend::OpenGl));
    CHECK(coney::platform::drawsMeshViewPassesOn(RenderBackend::Null));
    CHECK_FALSE(coney::platform::drawsMeshViewPassesOn(RenderBackend::Gpu));
}

TEST_CASE("on the GPU backend hiding the scenery leaves out the sectors and a mesh view leaves them in",
          "[platform][world][frame-description][world-page][window]") {
    // Expected: docs/engine/gpu-renderer.md#scene: #1545: on --renderer gpu the Streamed world switch hides
    // the sectors, and no other switch and no mesh view does. Needs SDL's video and a GPU device, and skips where
    // there is none.
    using coney::platform::DrawKind;
    using coney::test::HideHook;
    coney::platform::WindowDesc desc;
    desc.activate = false;
    desc.hidden = true;
    desc.width = 320;
    desc.height = 224;
    auto started = RenderEngine::start(RenderBackend::Gpu, desc);
    if (!started || (*started)->backend() != RenderBackend::Gpu) {
        SKIP("no display or no GPU device here");
    }
    RenderEngine& engine = **started;
    const bool passes = coney::platform::drawsMeshViewPassesOn(engine.backend());
    CHECK(sectorDraws(engine, false, nullptr, passes) == 2);
    HideHook scenery({DrawKind::Scenery});
    CHECK(sectorDraws(engine, false, &scenery, passes) == 0);
    HideHook others({DrawKind::Humans, DrawKind::Objects});
    CHECK(sectorDraws(engine, false, &others, passes) == 2);
    HideHook viewing({}, true);
    CHECK(sectorDraws(engine, false, &viewing, passes) == 2);
}

TEST_CASE("the renderer hides the kinds a hook hides, and no hook hides none", "[platform][world-page]") {
    // Expected: docs/engine/gpu-renderer.md#scene: RenderEngine::hideKindsOf() reads the World page's scenery, humans
    // and objects from the mesh views' hook (MeshViewHook::hides()) and forgets them for no hook; the effects and the
    // other kinds draw no recorded 3D model and are never hidden here.
    using coney::platform::DrawKind;
    using coney::test::HideHook;
    auto engine = startHeadless();
    for (const DrawKind kind : {DrawKind::Scenery, DrawKind::Humans, DrawKind::Objects}) {
        CHECK_FALSE(engine->kindHidden(kind));
    }
    HideHook humans({DrawKind::Humans, DrawKind::Effects});
    engine->hideKindsOf(&humans);
    CHECK_FALSE(engine->kindHidden(DrawKind::Scenery));
    CHECK(engine->kindHidden(DrawKind::Humans));
    CHECK_FALSE(engine->kindHidden(DrawKind::Objects));
    CHECK_FALSE(engine->kindHidden(DrawKind::Effects));
    HideHook all({DrawKind::Scenery, DrawKind::Humans, DrawKind::Objects});
    engine->hideKindsOf(&all);
    for (const DrawKind kind : {DrawKind::Scenery, DrawKind::Humans, DrawKind::Objects}) {
        CHECK(engine->kindHidden(kind));
    }
    engine->hideKindsOf(nullptr);
    for (const DrawKind kind : {DrawKind::Scenery, DrawKind::Humans, DrawKind::Objects}) {
        CHECK_FALSE(engine->kindHidden(kind));
    }
}

TEST_CASE("hiding the scenery hides the level's background with the sectors, and no other kind does",
          "[platform][world][frame-description][world-page]") {
    // Expected: docs/guides/debug-menu.md#world: the World page's Streamed world switch is the scenery kind, which the
    // background (the sky box, the clouds, the skyline and the light glows) is drawn in with the sectors, so off it
    // hides all six draws of the level scene (docs/engine/gpu-renderer.md#scene); the Humans and Objects switches
    // leave them. Coney's own debug tooling.
    using coney::platform::DrawKind;
    using coney::test::HideHook;
    auto engine = startHeadless();
    CHECK(levelDraws(*engine, true, nullptr, true) == 6);
    CHECK(levelDraws(*engine, true, nullptr, false) == 6);
    HideHook scenery({DrawKind::Scenery});
    CHECK(levelDraws(*engine, true, &scenery, false) == 0);
    HideHook others({DrawKind::Humans, DrawKind::Objects});
    CHECK(levelDraws(*engine, true, &others, false) == 6);
    HideHook viewing({}, true);
    CHECK(levelDraws(*engine, true, &viewing, false) == 6);
}

TEST_CASE("on GL3 a hook that only hides a kind sends the background through the rig too",
          "[platform][world][frame-description][world-page]") {
    // Expected: docs/guides/debug-menu.md#world: GL3 keeps the path it had before the part switches reached the GPU
    // renderer: with any hook set the background's models, like the sectors', go through the lighting rig for the
    // hook (SceneLighting::meshViewsOn()), so the frame description holds none of the level scene's six draws, a part
    // hidden or not.
    using coney::platform::DrawKind;
    using coney::test::HideHook;
    auto engine = startHeadless();
    HideHook scenery({DrawKind::Scenery});
    CHECK(levelDraws(*engine, true, &scenery, true) == 0);
    HideHook humans({DrawKind::Humans});
    CHECK(levelDraws(*engine, true, &humans, true) == 0);
    HideHook nothing;
    CHECK(levelDraws(*engine, true, &nothing, true) == 0);
}
