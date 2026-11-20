// SPDX-License-Identifier: GPL-3.0-or-later
// The human scene of the draw-stream hashes (docs/engine/renderer.md#comparing): Coney-made characters, posed and drawn
// headless by PlayLighting::drawHumanPasses among the objects of a WorldRenderer frame while the draw layer records. No
// game data: every vertex, bone and texel is made here.
//
// The expected hash is Coney's own: it was recorded from this scene when the humans' atomic renders were routed through
// the draw layer (item 9's first commit), and the move of the humans onto the frame description must leave it as it
// is. A change to it fails CI until the pull request explains it and updates the hash. The original has no
// counterpart.
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <rw.h>

#include "animation/anim_math.h"
#include "animation/anim_pose.h"
#include "animation/skeleton.h"
#include "characters/character_model.h"
#include "characters/character_rig.h"
#include "graphics/frame_description.h"
#include "graphics/lighting_frame.h"
#include "platform/character_mesh.h"
#include "platform/draw_calls.h"
#include "platform/draw_stream.h"
#include "platform/perf_probes.h"
#include "platform/play_lighting.h"
#include "platform/render_engine.h"
#include "platform/scene_assets.h"
#include "platform/scene_lighting.h"
#include "platform/world_renderer.h"
#include "platform/world_set.h"
#include "support/character_fixtures.h"
#include "support/object_model_fixtures.h"
#include "support/optional_value.h"
#include "world/sector_budget.h"

using coney::anim::Vec3;
using coney::platform::CharacterMesh;
using coney::platform::DrawKind;
using coney::platform::DrawKindScope;
using coney::platform::DrawStream;
using coney::platform::PlayLighting;
using coney::platform::RenderBackend;
using coney::platform::RenderEngine;
using coney::platform::WorldRenderer;
using coney::platform::WorldSet;
using coney::platform::WorldView;

namespace {

// One human of the scene: where his feet stand (game axes), which way he faces, and how he is drawn: as the player
// (the shadow dimming), with the blood layer or without (a puppet's).
struct SceneHuman {
    Vec3 feet;
    float heading = 0.0F;
    bool player = false;
    bool blood = true;
};

// The scene's humans: the player and a fighter with the blood layer near the camera, a puppet without it, and a
// fighter 65 m away, half faded out (graphics::humanDistanceFade()).
std::vector<SceneHuman> sceneHumans() {
    return {SceneHuman{.feet = {0.0F, 0.0F, 0.0F}, .heading = 0.0F, .player = true},
            SceneHuman{.feet = {2.0F, 1.0F, 0.0F}, .heading = 1.5F},
            SceneHuman{.feet = {-2.0F, 0.5F, 0.0F}, .heading = -0.7F, .blood = false},
            SceneHuman{.feet = {0.0F, -55.0F, 0.0F}, .heading = 3.0F}};
}

// The camera the scene is drawn through: behind the humans (game -y is RenderWare's +z), looking along RenderWare's z.
WorldView sceneView() {
    WorldView view;
    view.pose.position = {0.0F, 1.0F, -10.0F};
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

// What a test reads of the drawn scene before the humans go: the renderer (its frame description and scene assets),
// the meshes in the scene's order and the lighting the scene was drawn with (its lighting frame).
using SceneInspection = std::function<void(const RenderEngine&, const std::vector<std::unique_ptr<CharacterMesh>>&,
                                           const coney::platform::SceneLighting&)>;

// Draws the human scene headless on a fresh renderer, recording its one frame: each human posed in the synthetic
// skeleton's bind pose turned by his heading, then drawn as the play mode draws the humans (depth tested and written,
// back faces culled); then hands the renderer to `inspect`, when given, while the meshes are still held.
DrawStream drawHumanScene(const std::vector<SceneHuman>& humans, const SceneInspection& inspect = {}) {
    auto engine = startHeadless();
    const coney::test::ArchiveFixture archive({});
    coney::world::SectorBudget budget(10'000);
    auto set = WorldSet::load(*archive.wad, {}, budget, false, engine->sceneAssets());
    REQUIRE(set.has_value());
    auto model = coney::characters::decodeCharacterModel(
        coney::test::characterClump(coney::test::testCharacterFields()).span(), coney::test::testBoneOffsets().span());
    REQUIRE(model.has_value());
    const coney::anim::Skeleton skeleton = coney::characters::characterSkeleton(*model);
    coney::anim::Pose pose;
    pose.rotations = skeleton.bindRotations;
    const std::vector<coney::anim::Mat34> matrices =
        coney::characters::skinningMatrices(*model, coney::anim::boneTransforms(skeleton, pose));
    // The blood layer's texture: a Coney-made 4 x 4 raster, its size and format all the stream holds of it.
    rw::Texture* blood = rw::Texture::create(coney::platform::draw::createRaster(4, 4, 32, rw::Raster::C8888));
    std::optional<DrawStream> stream;
    {
        // The stand-in lighting (an ambient and a directional light for objects), as a scenery without a level's.
        PlayLighting lighting(*engine, *archive.wad, nullptr, Vec3{0.3F, 0.5F, -0.8F}, [](std::string_view) {});
        std::vector<std::unique_ptr<CharacterMesh>> meshes;
        for (const SceneHuman& human : humans) {
            auto mesh = std::make_unique<CharacterMesh>(*model, nullptr, coney::platform::Skinning::Gpu,
                                                        &engine->sceneAssets());
            mesh->pose(matrices, coney::platform::characterPlacement(human.feet, human.heading, 0.0F));
            meshes.push_back(std::move(mesh));
        }
        WorldRenderer renderer;
        renderer.recordHeadless(true);
        renderer.setLighting(&lighting.scene());
        const auto drawHumans = [&] {
            const DrawKindScope kind(DrawKind::Humans);
            coney::platform::draw::setRenderState(rw::ZTESTENABLE, 1);
            coney::platform::draw::setRenderState(rw::ZWRITEENABLE, 1);
            coney::platform::draw::setRenderState(rw::CULLMODE, rw::CULLBACK);
            for (std::size_t i = 0; i < humans.size(); ++i) {
                lighting.drawHumanPasses(*meshes[i], humans[i].player, humans[i].blood ? blood : nullptr);
            }
        };
        engine->requestDrawStream(0);
        renderer.render(*engine, **set, nullptr, sceneView(), 1000.0F, 2000, drawHumans);
        stream = engine->drawStream();
        if (inspect) {
            inspect(*engine, meshes, lighting.scene());
        }
    }
    blood->destroy();
    REQUIRE(stream.has_value());
    return coney::test::got(stream);
}

} // namespace

TEST_CASE("the human scene's draw stream has its expected hash", "[platform][draw-stream][humans]") {
    // Expected: ../engine/renderer.md#comparing, Coney's own recording: taken from this scene when the humans' renders
    // were routed through the draw layer; the move onto the frame description keeps it.
    const DrawStream stream = drawHumanScene(sceneHumans());
    // Each human one draw of his body, and the three with the blood layer (docs/research/rendering.md#characters) one
    // more: 7 atomic draws of the fixture's 4 vertices each.
    CHECK(stream.counts().atomicDraws == 7);
    CHECK(stream.hash() == 0x8dd4a6c636a76957ULL);
}

TEST_CASE("the human scene hashes the same twice and sees a pose, the blood and the fade",
          "[platform][draw-stream][humans]") {
    // Expected: docs/engine/renderer.md#comparing: a recording depends on the scene alone, and an atomic draw holds
    // its skinned vertices, its frame and its materials' colours: a turned human, a human drawn without his blood
    // layer (docs/research/rendering.md#characters) and one moved into the distance fade
    // (graphics::humanDistanceFade()) each move the hash.
    const std::vector<SceneHuman> humans = sceneHumans();
    const std::uint64_t base = drawHumanScene(humans).hash();
    CHECK(drawHumanScene(humans).hash() == base);
    std::vector<SceneHuman> turned = humans;
    turned[1].heading += 0.5F;
    CHECK(drawHumanScene(turned).hash() != base);
    std::vector<SceneHuman> clean = humans;
    clean[1].blood = false;
    CHECK(drawHumanScene(clean).hash() != base);
    std::vector<SceneHuman> farther = humans;
    farther[3].feet.y -= 3.0F;
    CHECK(drawHumanScene(farther).hash() != base);
}

TEST_CASE("the human scene's humans are drawn from the frame description with their poses",
          "[platform][humans][frame-description]") {
    // Expected: docs/engine/renderer.md#frame-description: each human is one 3D draw of his body's mesh by id, his
    // blood layer its dual layer drawn as the blood pass (docs/research/rendering.md#characters: blended by the blood
    // texture's alpha, no alpha test) where he has one, posed by his skinning matrices (one per skin node, the
    // character's space) at his placement (characterPlacement()), lit by the objects' lights selected once for his
    // sphere (HumanRender_Draw's one call of LightManager_SelectLights, docs/research/lighting.md#select), in the
    // humans' states (Z tested and written, back faces culled: docs/research/graphics.md#human-draw). His colour is
    // his materials' own times the shadow dimming, 1 for every human here (graphics::ShadowDim starts long since lit),
    // and his alpha the distance fade: 255 near, (70 - d) / 10 of it from 60 to 70 m (graphics.md#human-draw, step 4).
    const std::vector<SceneHuman> humans = sceneHumans();
    drawHumanScene(humans, [&](const RenderEngine& engine, const std::vector<std::unique_ptr<CharacterMesh>>& meshes,
                               const coney::platform::SceneLighting& lighting) {
        const coney::graphics::FrameDescription& frame = engine.frame();
        const coney::graphics::ScenePass* scene = nullptr;
        for (const auto& pass : frame.passes()) {
            if (const auto* found = std::get_if<coney::graphics::ScenePass>(&pass); found != nullptr) {
                scene = found;
            }
        }
        REQUIRE(scene != nullptr);
        REQUIRE(scene->draws.size() == humans.size());
        const coney::platform::SceneAssets& assets = engine.sceneAssets();
        const auto& rows = lighting.lightingFrame().selections;
        REQUIRE(rows.size() == humans.size());
        for (std::size_t i = 0; i < humans.size(); ++i) {
            const coney::graphics::Draw3d& draw = scene->draws[i];
            const CharacterMesh& mesh = *meshes[i];
            CHECK(assets.atomic(draw.mesh) == mesh.atomic());
            if (humans[i].blood) {
                CHECK(assets.atomic(draw.dualLayer) == mesh.bloodAtomic());
            } else {
                CHECK_FALSE(draw.dualLayer.valid());
            }
            CHECK(draw.dualPass == coney::graphics::DualPass::Blood);
            CHECK(draw.material.colour == coney::graphics::MaterialColour::Shade);
            CHECK(draw.material.shade == 1.0F);
            CHECK_FALSE(draw.material.distanceLevels);
            CHECK(draw.states.depthTest);
            CHECK(draw.states.depthWrite);
            CHECK(draw.states.cull == coney::graphics::CullFaces::Back);
            // The pose: each skinning matrix as it was given, and the placement as the draw's transform.
            const std::span<const coney::graphics::Transform3d> bones = frame.bonesOf(draw);
            const std::span<const coney::anim::Mat34> skinning = mesh.skinning();
            REQUIRE(bones.size() == skinning.size());
            REQUIRE_FALSE(bones.empty());
            for (std::size_t b = 0; b < bones.size(); ++b) {
                CHECK(bones[b].right == std::array<float, 3>{skinning[b].x.x, skinning[b].x.y, skinning[b].x.z});
                CHECK(bones[b].up == std::array<float, 3>{skinning[b].y.x, skinning[b].y.y, skinning[b].y.z});
                CHECK(bones[b].at == std::array<float, 3>{skinning[b].z.x, skinning[b].z.y, skinning[b].z.z});
                CHECK(bones[b].position == std::array<float, 3>{skinning[b].t.x, skinning[b].t.y, skinning[b].t.z});
            }
            // Game axes (x, y, z) are RenderWare's (x, z, -y): the feet's place.
            CHECK(draw.transform.position[0] == humans[i].feet.x);
            CHECK(draw.transform.position[2] == -humans[i].feet.y);
            // One selection of the objects' lights per human, with point lights (none is hidden).
            CHECK(rows[i].caller == coney::graphics::SelectionCaller::Human);
            CHECK(rows[i].objects);
            CHECK(rows[i].pointLights);
            CHECK(frame.lightsOf(draw).size() == rows[i].count);
            // Near: opaque; the fourth stands about 65 m from the camera, so about half faded.
            if (i < 3) {
                CHECK(draw.material.fade == 255);
            } else {
                CHECK(draw.material.fade > 64);
                CHECK(draw.material.fade < 192);
            }
            // The passes the Entity inspector counts: the body, and the blood layer where there is one.
            CHECK(mesh.drawnPasses() == (humans[i].blood ? 2U : 1U));
        }
    });
}

TEST_CASE("a human's two meshes are held by id while his mesh is", "[platform][humans][scene-assets]") {
    // Expected: docs/engine/renderer.md#frame-description: a character mesh given the scene assets holds its body's
    // atomic and its blood layer's there (two meshes), and drops both when it goes.
    auto engine = startHeadless();
    auto model = coney::characters::decodeCharacterModel(
        coney::test::characterClump(coney::test::testCharacterFields()).span(), coney::test::testBoneOffsets().span());
    REQUIRE(model.has_value());
    {
        const CharacterMesh mesh(*model, nullptr, coney::platform::Skinning::Gpu, &engine->sceneAssets());
        CHECK(engine->sceneAssets().atomic(mesh.meshId()) == mesh.atomic());
        CHECK(engine->sceneAssets().atomic(mesh.bloodMeshId()) == mesh.bloodAtomic());
        CHECK(engine->sceneAssets().store().meshCount() == 2);
    }
    CHECK(engine->sceneAssets().store().meshCount() == 0);
}
