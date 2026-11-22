// SPDX-License-Identifier: GPL-3.0-or-later
// The effects scenes of the draw-stream hashes (docs/engine/renderer.md#comparing): Coney-made particle systems,
// glints and fog sprites, coronas, blob shadows, glass panes and health rings, the water and the debug tools' scene
// lines, each drawn headless between a frame's start and its present while the draw layer records. No game data: every
// sheet, texel, vertex and name is made here.
//
// The expected hashes are Coney's own: they were recorded from these scenes before the effects moved onto the frame
// description (item 10 of docs/engine/renderer.md#order, first commit: nothing of the effects is drawn as a model, so
// the particles, coronas, rings and the rest are the Im3D draws item 1 records), and the move onto the description
// must leave every one as it is. A change to one fails CI until the pull request explains it and updates the hash.
// The original has no counterpart.
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "animation/anim_math.h"
#include "core/name_hash.h"
#include "effects/glints.h"
#include "effects/ground_fog.h"
#include "effects/particles.h"
#include "effects/water.h"
#include "fileio/disc.h"
#include "fileio/wad.h"
#include "graphics/human_lighting.h"
#include "graphics/level_lighting.h"
#include "graphics/light_manager.h"
#include "graphics/particle_page.h"
#include "graphics/render_device.h"
#include "hud/health_rings.h"
#include "platform/draw_stream.h"
#include "platform/line_list.h"
#include "platform/particle_renderer.h"
#include "platform/render_engine.h"
#include "platform/scene_lighting.h"
#include "platform/sprite_sheets.h"
#include "platform/texture_dictionary.h"
#include "platform/water_renderer.h"
#include "support/fixtures.h"
#include "support/optional_value.h"
#include "support/rw_fixtures.h"
#include "world/view_frustum.h"
#include "world_objects/glass.h"

using coney::anim::Vec3;
using coney::graphics::Rgba;
using coney::graphics::UvRect;
using coney::platform::DrawStream;
using coney::platform::RenderBackend;
using coney::platform::RenderEngine;

namespace {

// The headless renderer every scene draws on, at the original's frame size.
std::unique_ptr<RenderEngine> startHeadless() {
    coney::platform::WindowDesc desc;
    desc.width = 640;
    desc.height = 448;
    auto engine = RenderEngine::start(RenderBackend::Null, desc);
    REQUIRE(engine.has_value());
    return std::move(*engine);
}

// A WAD of one file that holds no sheet and no texture: a renderer asked to load from it draws its sprites flat, as
// each says. The archive is held by pointer, not in an optional (clang-tidy's optional-access check does not see the
// REQUIRE that ends the test when opening fails).
struct EmptyWad {
    coney::test::TempDir folder;
    std::unique_ptr<coney::io::Wad> wad;
    EmptyWad() {
        coney::test::Bytes file;
        file.text("not a sheet");
        file.padTo(2048);
        coney::test::Bytes dir;
        dir.header(1, 0, 0, 0);
        dir.u32(0).u32(static_cast<std::uint32_t>(file.size())).u32(coney::nameHash("./ee_files/none.txt"));
        folder.write("WARRIORS.DIR", dir.span());
        folder.write("WARRIORS.WAD", file.span());
        auto disc = coney::io::Disc::open(folder.path());
        REQUIRE(disc.has_value());
        auto opened = coney::io::Wad::open(std::move(*disc));
        REQUIRE(opened.has_value());
        wad = std::make_unique<coney::io::Wad>(std::move(*opened));
    }
};

// A sprite sheet of `rects` rectangles on a grid of 8 by 8 cells, over a Coney-made 2 x 2 texture named `name`.
coney::graphics::SpriteSheet makeSheet(std::string_view name, std::size_t rects) {
    const coney::test::Bytes stream = coney::test::texDictionary({coney::test::rgbaTexture2x2(std::string(name))});
    auto dictionary = coney::platform::TextureDictionary::read(stream.span());
    REQUIRE(dictionary.has_value());
    auto texture = coney::platform::SheetTexture::create(std::move(*dictionary));
    REQUIRE(texture.has_value());
    coney::graphics::SpriteSheet sheet;
    for (std::size_t i = 0; i < rects; ++i) {
        const auto column = static_cast<float>(i % 8);
        const auto row = static_cast<float>((i / 8) % 8);
        sheet.page.rects.push_back(UvRect{column / 8.0F, row / 8.0F, (column + 1.0F) / 8.0F, (row + 1.0F) / 8.0F});
    }
    sheet.texture = std::shared_ptr<const coney::graphics::Texture>(std::move(*texture));
    return sheet;
}

// A camera looking down +z in RenderWare's axes, as the world pass places it: its right is -x.
coney::world::CameraPose camera() {
    coney::world::CameraPose pose;
    pose.position = coney::world::Vec3{0.0F, 2.0F, -6.0F};
    return pose;
}

// What a scene draws between a frame's start and its present: the recorded stream of that frame, with the rasters
// known by their texels.
template <typename Draw> DrawStream drawFrame(RenderEngine& engine, const Draw& scene) {
    engine.requestDrawStream(0);
    engine.beginFrame(Rgba{32, 48, 64, 255});
    scene();
    engine.present();
    const std::optional<DrawStream>& recorded = engine.drawStream();
    REQUIRE(recorded.has_value());
    return coney::test::got(recorded);
}

// The particle scene: a blood spray and a spark burst of the `part_page1` sheet (blended by alpha, added), a glass
// shard with no sheet (flat), the glints (added) and the ground fog's wisps (alpha) as the sprites no system owns draw.
DrawStream drawParticleScene(RenderEngine& engine, const coney::io::Wad& wad) {
    engine.prepareDrawStreams();
    coney::effects::ParticleSystems systems(1234);
    systems.spawn("blood_spray", Vec3{1.0F, 2.0F, 3.0F});
    systems.spawn("spark", Vec3{-1.0F, 2.5F, 4.0F});
    systems.spawnShard(Vec3{0.5F, 1.0F, 2.0F}, 0.12F, 0x80C0E0A0U);
    for (int step = 0; step < 3; ++step) {
        systems.step(1.0F / 30.0F);
    }
    REQUIRE(systems.particleCount() > 0);
    coney::platform::ParticleRenderer renderer(wad, false, {});
    renderer.useSheet(coney::effects::ParticleSheet::PartPage1, makeSheet("part_page1", 64));
    renderer.useSheet(coney::effects::ParticleSheet::PartFog00, makeSheet("part_fog_00", 1));
    std::vector<coney::effects::Particle> glints(2);
    glints[0].position = Vec3{2.0F, 1.0F, 5.0F};
    glints[0].size = 0.4F;
    glints[0].colour = 0xFFE0A0C0U;
    glints[0].rect = 3;
    glints[1].position = Vec3{-2.0F, 3.0F, 6.0F};
    glints[1].size = 0.3F;
    glints[1].angle = 0.5F;
    glints[1].colour = 0x60FF80FFU;
    glints[1].rect = 5;
    std::vector<coney::effects::Particle> wisps(1);
    wisps[0].position = Vec3{0.0F, 0.2F, 3.0F};
    wisps[0].size = 2.0F;
    wisps[0].colour = 0xC0C0C060U;
    wisps[0].fades = false;
    const coney::world::CameraPose view = camera();
    return drawFrame(engine, [&] {
        renderer.draw(engine, systems, view);
        renderer.drawSprites(engine, glints, coney::effects::ParticleSheet::PartPage1, true, view);
        renderer.drawSprites(engine, wisps, coney::effects::ParticleSheet::PartFog00, false, view);
    });
}

// The sprite scene: a corona of the `lighting` sheet, a blob shadow, two glass panes, a health ring and a target
// marker of the `part_page1` sheet, as the lights draw them.
DrawStream drawSpriteScene(RenderEngine& engine) {
    engine.prepareDrawStreams();
    coney::graphics::LevelLighting level;
    level.lights.addLight(coney::graphics::LightDescriptor{.type = coney::graphics::LightType::Point,
                                                           .position = {1.0F, 3.0F, 8.0F},
                                                           .colour = {1.0F, 0.8F, 0.4F, 1.0F},
                                                           .radius = 12.0F,
                                                           .coronaSize = 1.5F,
                                                           .lights = coney::graphics::kLightsWorld,
                                                           .corona = 2});
    coney::platform::SceneLighting lighting(level, makeSheet("lighting", 6), makeSheet("part_page1", 64));
    lighting.beginFrame(camera(), 0.1F, 200.0F, 0);
    REQUIRE(level.lights.coronas().size() == 1);
    const std::array<coney::graphics::BlobShadow, 2> shadows{{
        {{1.0F, 5.0F, 0.05F}, {0.0F, 0.0F, 1.0F}, 1.0F},
        {{-2.0F, 6.0F, 0.55F}, {0.0F, 0.6F, 0.8F}, 1.2F},
    }};
    const std::array<coney::world_objects::GlassQuad, 2> panes{{
        {.corners = {Vec3{-1.0F, 4.0F, 0.0F}, Vec3{1.0F, 4.0F, 0.0F}, Vec3{-1.0F, 4.0F, 2.0F}, Vec3{1.0F, 4.0F, 2.0F}},
         .rect = 10,
         .colour = 0x80A0C0C0U},
        {.corners = {Vec3{2.0F, 6.0F, 0.0F}, Vec3{3.0F, 6.0F, 0.0F}, Vec3{2.0F, 6.0F, 1.5F}, Vec3{3.0F, 6.0F, 1.5F}},
         .rect = 11,
         .colour = 0xFFFFFF80U},
    }};
    coney::hud::GroundRing ring;
    ring.centre = Vec3{0.5F, 3.0F, 0.04F};
    ring.radius = 0.54F;
    ring.value = 70.0F;
    ring.capacity = 90.0F;
    ring.fill = coney::hud::RingColour{200, 40, 40};
    ring.alpha = 220;
    const std::array<coney::hud::GroundRing, 1> rings{ring};
    coney::hud::TargetMarker marker;
    marker.centre = Vec3{-1.5F, 4.5F, 0.041F};
    const std::array<coney::hud::TargetMarker, 1> markers{marker};
    return drawFrame(engine, [&] {
        lighting.drawGlass(engine, panes);
        lighting.drawBlobShadows(engine, shadows);
        lighting.drawGroundRings(engine, rings, markers);
        lighting.drawCoronas(engine);
    });
}

// The water scene: the surface placed, stepped and drawn untextured (the WAD holds no texture).
DrawStream drawWaterScene(RenderEngine& engine, const coney::io::Wad& wad) {
    coney::effects::Water water;
    coney::effects::WaterSettings settings;
    settings.position = Vec3{-4.0F, 0.0F, -1.0F};
    settings.width = 8.0F;
    settings.length = 10.0F;
    settings.colour = {40, 90, 160};
    settings.waveHeight = 0.2F;
    settings.waveSpeed = 1.0F;
    water.set(settings);
    for (int step = 0; step < 4; ++step) {
        water.step();
    }
    coney::platform::WaterRenderer renderer(wad, {});
    return drawFrame(engine, [&] { renderer.draw(engine, water); });
}

// The scene lines scene: two coloured lines and one translucent one, as the debug tools draw them.
DrawStream drawLinesScene(RenderEngine& engine) {
    coney::platform::LineList lines;
    lines.add(Vec3{0.0F, 0.0F, 1.0F}, Vec3{2.0F, 1.0F, 1.0F}, Rgba{255, 0, 0, 255});
    lines.add(Vec3{2.0F, 1.0F, 1.0F}, Vec3{2.0F, 3.0F, 2.0F}, Rgba{0, 255, 0, 255});
    lines.add(Vec3{-1.0F, 0.5F, 0.0F}, Vec3{-1.0F, 0.5F, 2.0F}, Rgba{0, 128, 255, 128});
    return drawFrame(engine, [&] { lines.draw(engine); });
}

} // namespace

TEST_CASE("the particle scene's draw stream has its expected hash", "[platform][draw-stream][effects]") {
    // Expected: recorded from this scene before the effects moved onto the frame description
    // (docs/engine/renderer.md#frame-description).
    auto engine = startHeadless();
    const EmptyWad archive;
    const DrawStream stream = drawParticleScene(*engine, *archive.wad);
    CHECK(stream.counts().im3dDraws == 5);
    CHECK(stream.hash() == 0x822560d4f66aa38eULL);
}

TEST_CASE("the sprite scene's draw stream has its expected hash", "[platform][draw-stream][effects]") {
    // Expected: recorded from this scene before the effects moved onto the frame description
    // (docs/engine/renderer.md#frame-description).
    auto engine = startHeadless();
    const DrawStream stream = drawSpriteScene(*engine);
    CHECK(stream.counts().im3dDraws == 4);
    CHECK(stream.hash() == 0x9643e3ff69a4bef9ULL);
}

TEST_CASE("the water scene's draw stream has its expected hash", "[platform][draw-stream][effects]") {
    // Expected: recorded from this scene before the effects moved onto the frame description
    // (docs/engine/renderer.md#frame-description).
    auto engine = startHeadless();
    const EmptyWad archive;
    const DrawStream stream = drawWaterScene(*engine, *archive.wad);
    CHECK(stream.counts().im3dDraws == 1);
    CHECK(stream.hash() == 0x4397e8c26bc0bac1ULL);
}

TEST_CASE("the scene lines scene's draw stream has its expected hash", "[platform][draw-stream][effects]") {
    // Expected: recorded from this scene before the effects moved onto the frame description
    // (docs/engine/renderer.md#frame-description).
    auto engine = startHeadless();
    const DrawStream stream = drawLinesScene(*engine);
    CHECK(stream.counts().im3dDraws == 1);
    CHECK(stream.hash() == 0x0315a4869a34248aULL);
}

TEST_CASE("an effects scene drawn twice on fresh renderers hashes the same", "[platform][draw-stream][effects]") {
    // Expected: docs/engine/renderer.md#comparing: a recording depends on the scene alone.
    std::uint64_t first = 0;
    {
        auto engine = startHeadless();
        first = drawSpriteScene(*engine).hash();
    }
    auto engine = startHeadless();
    CHECK(drawSpriteScene(*engine).hash() == first);
}
