// SPDX-License-Identifier: GPL-3.0-or-later
// The object scene of the draw-stream hashes (docs/engine/renderer.md#comparing): Coney-made world objects and a
// parked car, loaded through PlacedObjects and ParkedCars from a synthetic archive and drawn headless by WorldRenderer
// while the draw layer records. No game data: every vertex, texel and name is made here.
//
// The expected hash is Coney's own: it was recorded from this scene when the objects' and cars' atomic renders were
// routed through the draw layer (item 8's first commit), and the move of the objects and cars onto the frame
// description must leave it as it is. A change to it fails CI until the pull request explains it and updates the
// hash. The original has no counterpart.
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "animation/anim_math.h"
#include "core/name_hash.h"
#include "graphics/asset_store.h"
#include "graphics/car_draw.h"
#include "graphics/frame_description.h"
#include "graphics/level_lighting.h"
#include "graphics/light_manager.h"
#include "graphics/lighting_frame.h"
#include "graphics/render_device.h"
#include "graphics/texture_data.h"
#include "platform/draw_stream.h"
#include "platform/object_draw.h"
#include "platform/parked_cars.h"
#include "platform/perf_probes.h"
#include "platform/placed_objects.h"
#include "platform/render_engine.h"
#include "platform/scene_assets.h"
#include "platform/scene_lighting.h"
#include "platform/world_renderer.h"
#include "platform/world_set.h"
#include "support/object_model_fixtures.h"
#include "support/optional_value.h"
#include "world/sector_budget.h"
#include "world_objects/car_types.h"
#include "world_objects/cars.h"
#include "world_objects/object_list.h"

using coney::anim::Quat;
using coney::anim::Vec3;
using coney::graphics::CarPass;
using coney::graphics::CullFaces;
using coney::graphics::Draw3d;
using coney::graphics::FrameDescription;
using coney::graphics::MaterialColour;
using coney::graphics::Rgba;
using coney::graphics::ScenePass;
using coney::graphics::TextureData;
using coney::platform::DrawKind;
using coney::platform::DrawKindScope;
using coney::platform::DrawStream;
using coney::platform::ObjectDraw;
using coney::platform::ParkedCars;
using coney::platform::PlacedObjects;
using coney::platform::RenderBackend;
using coney::platform::RenderEngine;
using coney::platform::WorldRenderer;
using coney::platform::WorldSet;
using coney::platform::WorldView;
using coney::test::ArchiveFixture;
using coney::test::ObjectModelFixture;

namespace {

// The models' resources, by hash: a crate (one atomic) and a coupe (a car's 26 part atomics).
constexpr std::uint32_t kCrateModel = 0x1001;
constexpr std::uint32_t kCrateTextures = 0x1002;
constexpr std::uint32_t kCoupeModel = 0x2001;
constexpr std::uint32_t kCoupeTextures = 0x2002;
// The car's script handle.
constexpr double kCar = 1.0;

// The scene's two Object List models.
std::vector<ObjectModelFixture> sceneModels() {
    return {ObjectModelFixture{.name = "dyn_crate",
                               .modelHash = kCrateModel,
                               .texturesHash = kCrateTextures,
                               .atomics = 1,
                               .texture = "crate"},
            ObjectModelFixture{.name = std::string(coney::world_objects::kCarTypeNames[1]),
                               .modelHash = kCoupeModel,
                               .texturesHash = kCoupeTextures,
                               .atomics = coney::world_objects::kCarParts,
                               .texture = "coupe"}};
}

// What the scene varies: the second crate's tint (0xRRGGBBAA; its alpha under 255 draws it in the translucent pass),
// whether the car is painted, where the car stands along x (metres) and how many point lights, each reaching every
// object, the scene holds (none by default, which is the hash's scene).
struct ObjectScene {
    std::uint32_t tint = 0x80C0FF80U;
    bool painted = true;
    float carX = 6.0F;
    int pointLights = 0;
};

// The camera the scene is drawn through: in front of the objects, looking along RenderWare's z.
WorldView sceneView() {
    WorldView view;
    view.pose.position = {2.0F, 2.0F, -20.0F};
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

// What a test reads of the drawn scene before the objects and cars go: the renderer (its frame description and scene
// assets), the cars and the lighting the scene was drawn with (its lighting frame).
using SceneInspection =
    std::function<void(const RenderEngine&, const coney::world_objects::Cars&, const coney::platform::SceneLighting&)>;

// Draws the object scene headless on a fresh renderer, recording its one frame: two crates and the car's opaque parts
// among the objects (between the worlds, which are none here), the car's glass after them, as the play mode orders
// them (docs/research/graphics.md#car-draw); then hands the renderer to `inspect`, when given, while the objects and
// cars are still held.
DrawStream drawObjectScene(const ObjectScene& scene, const SceneInspection& inspect = {}) {
    auto engine = startHeadless();
    const ArchiveFixture archive(coney::test::objectModelEntries(sceneModels()));
    auto list = coney::world_objects::ObjectList::parse(coney::test::objectListChunk(sceneModels()).span());
    REQUIRE(list.has_value());
    coney::world::SectorBudget budget(10'000);
    auto set = WorldSet::load(*archive.wad, {}, budget, false, engine->sceneAssets());
    REQUIRE(set.has_value());
    coney::world_objects::Cars cars;
    REQUIRE(cars.spawn(sceneModels()[1].name, Vec3{scene.carX, 0.0F, 0.0F}, Quat{}, kCar) != nullptr);
    if (scene.painted) {
        cars.setColour(kCar, {0.25F, 0.5F, 0.75F, 1.0F});
    }
    std::optional<DrawStream> stream;
    {
        PlacedObjects objects(*archive.wad, *list, {}, false, &engine->sceneAssets());
        ParkedCars parked(*archive.wad, cars, false, {}, &engine->sceneAssets());
        // The crate's type, as objects are placed: the CRC-32 of its Object List name.
        const std::uint32_t crate = coney::crc32("dyn_crate");
        objects.place(10.0, crate, Vec3{0.0F, 0.0F, 0.0F}, Quat{});
        objects.place(11.0, crate, Vec3{3.0F, 0.0F, 0.0F}, Quat{}, PlacedObjects::Look{.tint = scene.tint});
        // The scene's own lights, when it has any: point lights in front of the camera, big enough to reach every
        // object, which light the objects (kLightsObjects) and so fill the cap a draw's selection keeps.
        coney::graphics::LevelLighting level;
        coney::platform::SceneLighting lighting(level, std::nullopt, std::nullopt);
        for (int i = 0; i < scene.pointLights; ++i) {
            level.lights.addLight(coney::graphics::LightDescriptor{.type = coney::graphics::LightType::Point,
                                                                   .position = {2.0F, 2.0F, 10.0F},
                                                                   .colour = {0.5F, 0.5F, 0.5F, 1.0F},
                                                                   .radius = 300.0F,
                                                                   .lights = coney::graphics::kLightsObjects});
        }
        WorldRenderer renderer;
        renderer.recordHeadless(true);
        if (scene.pointLights > 0) {
            renderer.setLighting(&lighting);
        }
        const auto render = [&engine, &renderer](const ObjectDraw& draw) {
            coney::platform::drawObject(*engine, renderer.lighting(), draw);
        };
        const auto drawObjects = [&] {
            const DrawKindScope kind(DrawKind::Objects);
            parked.draw(render, CarPass::Opaque);
            objects.draw(PlacedObjects::DrawOptions{.camera = std::nullopt, .render = render});
        };
        const auto afterWorlds = [&] {
            const DrawKindScope kind(DrawKind::Objects);
            parked.draw(render, CarPass::Glass);
        };
        engine->requestDrawStream(0);
        renderer.render(*engine, **set, nullptr, sceneView(), 1000.0F, 2000, drawObjects, {}, afterWorlds);
        stream = engine->drawStream();
        if (inspect) {
            inspect(*engine, cars, renderer.lighting());
        }
    }
    REQUIRE(stream.has_value());
    return coney::test::got(stream);
}

} // namespace

TEST_CASE("the object scene's draw stream has its expected hash", "[platform][draw-stream][objects]") {
    // Expected: ../engine/renderer.md#comparing, Coney's own recording: taken from this scene when the objects' and
    // cars' renders were routed through the draw layer; the move onto the frame description keeps it.
    const DrawStream stream = drawObjectScene(ObjectScene{});
    // Two crates and the car's 26 parts, one draw each (20 opaque, then the 6 glass parts: graphics::kCarGlassMask);
    // no dual layer (no MatFX material) and no world.
    CHECK(stream.counts().atomicDraws == 28);
    CHECK(stream.counts().vertices == 112); // each atomic's four strip vertices
    CHECK(stream.hash() == 0xd5c9dce7264f1f19ULL);
}

TEST_CASE("the object scene hashes the same twice and sees a tint and a paint", "[platform][draw-stream][objects]") {
    // Expected: docs/engine/renderer.md#comparing: a recording depends on the scene alone, and an atomic draw holds
    // its materials' colours: an object's tint replaces the first material's colour (docs/research/objects.md#tint) and
    // a car's paint sets its painted parts' (docs/research/graphics.md#car-draw).
    const std::uint64_t base = drawObjectScene(ObjectScene{}).hash();
    CHECK(drawObjectScene(ObjectScene{}).hash() == base);
    CHECK(drawObjectScene(ObjectScene{.tint = 0xFFFFFFFFU}).hash() != base);
    CHECK(drawObjectScene(ObjectScene{.painted = false}).hash() != base);
}

TEST_CASE("the object scene's objects and cars are drawn from the frame description",
          "[platform][objects][frame-description]") {
    // Expected: docs/engine/renderer.md#frame-description: each car part and world object is one 3D draw of its mesh
    // by id with its dictionary's first texture (docs/research/level-loading.md#the-object-list), lit by the objects'
    // lights, selected once per draw from the objects' lists (docs/research/lighting.md#select), in the order
    // and states each pass draws in: the car's opaque parts with both sides drawn and Z written, the crates opaque
    // first, then the translucent one, back faces culled (PlacedObjects::draw), the glass parts after the worlds
    // without Z (docs/research/graphics.md#car-draw). A part takes the car's paint where the paint mask has it and
    // white elsewhere (graphics::carPartPainted); a crate its tint, the alpha 0x80 of the second's
    // (docs/research/objects.md#tint).
    drawObjectScene(ObjectScene{}, [](const RenderEngine& engine, const coney::world_objects::Cars& cars,
                                      const coney::platform::SceneLighting& lighting) {
        const FrameDescription& frame = engine.frame();
        const ScenePass* scene = nullptr;
        for (const auto& pass : frame.passes()) {
            if (const auto* found = std::get_if<ScenePass>(&pass); found != nullptr) {
                scene = found;
            }
        }
        REQUIRE(scene != nullptr);
        REQUIRE(scene->draws.size() == 28);
        const coney::world_objects::Car* car = cars.find(kCar);
        REQUIRE(car != nullptr);
        const coney::world_objects::CarPaint paint = coney::world_objects::paintOf(car->paint[0]);
        const Rgba painted{paint.r, paint.g, paint.b, 255};
        const coney::platform::SceneAssets& assets = engine.sceneAssets();
        // The name of the draw's one texture, through the asset store; empty when it has none.
        const auto textureName = [&](const Draw3d& draw) -> std::string {
            const std::span<const coney::graphics::TextureId> textures = frame.texturesOf(draw);
            const TextureData* texture = textures.size() == 1 ? assets.store().texture(textures[0]) : nullptr;
            return texture != nullptr ? texture->name : std::string();
        };
        std::vector<std::size_t> opaqueParts;
        std::vector<std::size_t> glassParts;
        for (std::size_t part = 0; part < coney::world_objects::kCarParts; ++part) {
            (coney::graphics::carPartGlass(part) ? glassParts : opaqueParts).push_back(part);
        }
        REQUIRE(opaqueParts.size() == 20);
        REQUIRE(glassParts.size() == 6);
        for (std::size_t i = 0; i < scene->draws.size(); ++i) {
            const Draw3d& draw = scene->draws[i];
            CHECK(assets.atomic(draw.mesh) != nullptr);
            CHECK_FALSE(draw.dualLayer.valid());
            CHECK(draw.states.depthTest);
            CHECK_FALSE(draw.material.distanceLevels);
            if (i == 20 || i == 21) {
                CHECK(textureName(draw) == "crate");
                CHECK(draw.material.colour == MaterialColour::Tint);
                CHECK(draw.material.tint == (i == 20 ? coney::graphics::kWhite : Rgba{0x80, 0xC0, 0xFF, 0x80}));
                CHECK(draw.states.cull == CullFaces::Back);
                CHECK(draw.states.depthWrite);
                CHECK(draw.transform.position[0] == (i == 20 ? 0.0F : 3.0F));
                continue;
            }
            const std::size_t part = i < 20 ? opaqueParts[i] : glassParts[i - 22];
            CHECK(textureName(draw) == "coupe");
            CHECK(draw.material.colour == MaterialColour::Paint);
            CHECK(draw.material.tint == (coney::graphics::carPartPainted(part) ? painted : coney::graphics::kWhite));
            CHECK(draw.states.cull == CullFaces::None);
            CHECK(draw.states.depthWrite == (i < 20));
            CHECK(draw.transform.position[0] == 6.0F); // the car's x, the same in RenderWare's axes
        }
        // One selection of the objects' lights for each draw, each draw's lights as many as its selection chose.
        const auto& rows = lighting.lightingFrame().selections;
        REQUIRE(rows.size() == scene->draws.size());
        for (std::size_t i = 0; i < rows.size(); ++i) {
            CHECK(rows[i].caller == coney::graphics::SelectionCaller::Object);
            CHECK(rows[i].objects);
            CHECK(rows[i].pointLights);
            CHECK(frame.lightsOf(scene->draws[i]).size() == rows[i].count);
        }
    });
}

TEST_CASE("an object's and a car's meshes and textures are held by id while they are",
          "[platform][objects][scene-assets]") {
    // Expected: docs/engine/renderer.md#frame-description: PlacedObjects and ParkedCars hold each loaded model's
    // meshes (one per atomic: the crate's one, the car's 26) and its dictionary's first texture in the engine's scene
    // assets, and drop them as they go.
    std::size_t meshes = 0;
    std::size_t textures = 0;
    drawObjectScene(ObjectScene{}, [&](const RenderEngine& engine, const coney::world_objects::Cars&,
                                       const coney::platform::SceneLighting&) {
        meshes = engine.sceneAssets().store().meshCount();
        textures = engine.sceneAssets().store().textureCount();
    });
    CHECK(meshes == 27);
    CHECK(textures == 2);

    auto engine = startHeadless();
    const ArchiveFixture archive(coney::test::objectModelEntries(sceneModels()));
    auto list = coney::world_objects::ObjectList::parse(coney::test::objectListChunk(sceneModels()).span());
    REQUIRE(list.has_value());
    {
        PlacedObjects objects(*archive.wad, *list, {}, false, &engine->sceneAssets());
        objects.place(10.0, coney::crc32("dyn_crate"), Vec3{}, Quat{});
        CHECK(engine->sceneAssets().store().meshCount() == 1);
        CHECK(engine->sceneAssets().store().textureCount() == 1);
    }
    CHECK(engine->sceneAssets().store().meshCount() == 0);
    CHECK(engine->sceneAssets().store().textureCount() == 0);
}

TEST_CASE("a car part's lights are selected at squared distance 0 and a world object's at its own",
          "[platform][objects][frame-description]") {
    // Expected: docs/research/lighting.md#select, confirmed (code): CarInstance_Render clears f12 at 0x00172cc8 and
    // selects the objects' lights with squared distance 0, so a car keeps the nearest cap however far it is; a world
    // object (ObjectRender_Draw, 0x0017fd78) selects with its squared distance to the camera.
    drawObjectScene(ObjectScene{}, [](const RenderEngine&, const coney::world_objects::Cars&,
                                      const coney::platform::SceneLighting& lighting) {
        const auto& rows = lighting.lightingFrame().selections;
        REQUIRE(rows.size() == 28);
        for (std::size_t i = 0; i < rows.size(); ++i) {
            const bool crate = i == 20 || i == 21; // the 20 opaque parts, the two crates, then the 6 glass parts
            if (crate) {
                CHECK(rows[i].distSq > 300.0F); // the camera is about 20 m away
            } else {
                CHECK(rows[i].distSq == 0.0F);
                CHECK(rows[i].cap == coney::graphics::selectionCap(0.0F, true, false));
            }
        }
    });
}

TEST_CASE("a car far from the camera keeps the nearest cap of lights", "[platform][objects][frame-description]") {
    // Expected: docs/research/lighting.md#select, confirmed (code): CarInstance_Render clears f12 at 0x00172cc8 before
    // its selection call, so a car part gets the nearest cap of 6 lights (the objects' cap falls to 3 only past
    // 69 m) however far the car stands; ObjectRender_Draw passes a world object's squared distance to the camera.
    // The car is about 80 m from the camera, where a selection made at its distance would keep 3 of the 8 lights.
    drawObjectScene(
        ObjectScene{.carX = 80.0F, .pointLights = 8},
        [](const RenderEngine&, const coney::world_objects::Cars&, const coney::platform::SceneLighting& lighting) {
            const auto& rows = lighting.lightingFrame().selections;
            REQUIRE(rows.size() == 28);
            for (std::size_t i = 0; i < rows.size(); ++i) {
                const bool crate = i == 20 || i == 21; // the 20 opaque parts, the two crates, the glass
                CHECK(rows[i].count == 6);             // the crates, about 20 m away, keep 6 as well
                if (!crate) {
                    CHECK(rows[i].distSq == 0.0F);
                }
            }
        });
}
