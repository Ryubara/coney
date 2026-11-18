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
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "animation/anim_math.h"
#include "core/name_hash.h"
#include "graphics/car_draw.h"
#include "platform/draw_stream.h"
#include "platform/parked_cars.h"
#include "platform/perf_probes.h"
#include "platform/placed_objects.h"
#include "platform/render_engine.h"
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
using coney::platform::DrawKind;
using coney::platform::DrawKindScope;
using coney::platform::DrawStream;
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

// What the scene varies: the second crate's tint (0xRRGGBBAA; its alpha under 255 draws it in the translucent pass)
// and whether the car is painted.
struct ObjectScene {
    std::uint32_t tint = 0x80C0FF80U;
    bool painted = true;
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

// Draws the object scene headless on a fresh renderer, recording its one frame: two crates and the car's opaque parts
// among the objects (between the worlds, which are none here), the car's glass after them, as the play mode orders
// them (docs/research/graphics.md#car-draw).
DrawStream drawObjectScene(const ObjectScene& scene) {
    auto engine = startHeadless();
    const ArchiveFixture archive(coney::test::objectModelEntries(sceneModels()));
    auto list = coney::world_objects::ObjectList::parse(coney::test::objectListChunk(sceneModels()).span());
    REQUIRE(list.has_value());
    coney::world::SectorBudget budget(10'000);
    auto set = WorldSet::load(*archive.wad, {}, budget, false, engine->sceneAssets());
    REQUIRE(set.has_value());
    coney::world_objects::Cars cars;
    REQUIRE(cars.spawn(sceneModels()[1].name, Vec3{6.0F, 0.0F, 0.0F}, Quat{}, kCar) != nullptr);
    if (scene.painted) {
        cars.setColour(kCar, {0.25F, 0.5F, 0.75F, 1.0F});
    }
    std::optional<DrawStream> stream;
    {
        PlacedObjects objects(*archive.wad, *list, {}, false);
        ParkedCars parked(*archive.wad, cars, false, {});
        // The crate's type, as objects are placed: the CRC-32 of its Object List name.
        const std::uint32_t crate = coney::crc32("dyn_crate");
        objects.place(10.0, crate, Vec3{0.0F, 0.0F, 0.0F}, Quat{});
        objects.place(11.0, crate, Vec3{3.0F, 0.0F, 0.0F}, Quat{}, PlacedObjects::Look{.tint = scene.tint});
        WorldRenderer renderer;
        renderer.recordHeadless(true);
        const auto render = [&renderer](rw::Atomic* atomic) { renderer.lighting().drawObjectAtomic(atomic); };
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
    // its materials' colours, which an object's tint multiplies (docs/research/objects.md#tint) and a car's paint sets
    // on its painted parts (docs/research/graphics.md#car-draw).
    const std::uint64_t base = drawObjectScene(ObjectScene{}).hash();
    CHECK(drawObjectScene(ObjectScene{}).hash() == base);
    CHECK(drawObjectScene(ObjectScene{.tint = 0xFFFFFFFFU}).hash() != base);
    CHECK(drawObjectScene(ObjectScene{.painted = false}).hash() != base);
}
