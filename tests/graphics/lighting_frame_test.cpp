// SPDX-License-Identifier: GPL-3.0-or-later
// The lighting frame (docs/engine/renderer.md#lighting, docs/guides/realtime-lighting.md#the-lighting-frame): the
// lights the viewport's cull kept with this step's flicker colour, the level's shadow light and who casts a shadow
// (docs/research/xbox-lighting.md#shadow-light), and that none of it reaches the simulation. Synthetic lights only.
#include "graphics/lighting_frame.h"

#include <cmath>
#include <cstdint>
#include <optional>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "ai/hiding.h"
#include "graphics/level_lighting.h"
#include "graphics/light_manager.h"
#include "raycast/collision_mesh.h"
#include "support/ai_fixtures.h"
#include "support/collision_fixtures.h"

using Catch::Approx;
using coney::graphics::buildLightingFrame;
using coney::graphics::FrameLight;
using coney::graphics::LevelLighting;
using coney::graphics::LightColour;
using coney::graphics::LightDescriptor;
using coney::graphics::LightHandle;
using coney::graphics::LightingFrame;
using coney::graphics::LightManager;
using coney::graphics::LightType;
using coney::graphics::LightView;
using coney::world::Vec3;

namespace {

// A camera at the origin looking along +z, draw distance 115.
LightView camera() {
    return LightView{.position = {}, .forward = {0.0F, 0.0F, 1.0F}, .nearClip = 0.5F, .farClip = 115};
}

// A point lamp at `at` of radius `radius` lighting `lights`, warm white.
LightDescriptor lamp(Vec3 at, float radius, std::uint16_t lights = coney::graphics::kLightsObjects) {
    LightDescriptor desc;
    desc.type = LightType::Point;
    desc.position = at;
    desc.radius = radius;
    desc.colour = LightColour{1.0F, 0.9F, 0.8F, 1.0F};
    desc.lights = lights;
    return desc;
}

// A light of `type` with `colour` lighting `lights`.
LightDescriptor global(LightType type, LightColour colour, std::uint16_t lights) {
    LightDescriptor desc;
    desc.type = type;
    desc.colour = colour;
    desc.lights = lights;
    return desc;
}

// Whether `list` holds `record`.
bool holds(const std::vector<std::uint16_t>& list, std::uint16_t record) {
    for (const std::uint16_t r : list) {
        if (r == record) {
            return true;
        }
    }
    return false;
}

// A level with a random-flicker lamp, a blinking lamp and a bursting lamp in front of camera(), and one behind it.
void addFlickeringLamps(LevelLighting& level) {
    LightDescriptor random = lamp({0, 0, 10}, 3.0F);
    random.effects = coney::graphics::kFlickerRandom;
    static_cast<void>(level.lights.addLight(random));
    const LightHandle blink = level.lights.addLight(lamp({0, 0, 12}, 3.0F));
    static_cast<void>(
        level.lights.setFlicker(blink, coney::graphics::FlickerTiming{.onTime = 100, .offTime = 50, .dim = 20}));
    const LightHandle burst = level.lights.addLight(lamp({2, 0, 14}, 3.0F, coney::graphics::kLightsWorld | 1));
    static_cast<void>(level.lights.setFlicker(
        burst, coney::graphics::FlickerTiming{.pause = 300, .burst = 3, .flickerTime = 40, .dim = 50}));
    static_cast<void>(level.lights.addLight(lamp({0, 0, -20}, 3.0F)));
}

} // namespace

TEST_CASE("The lighting frame holds the lights the viewport's cull kept, with their lists, ambient and fog",
          "[graphics][lighting_frame]") {
    // Expected: lighting.md#cull, confirmed (code)
    LevelLighting level;
    const LightHandle ambient =
        level.lights.addLight(global(LightType::Ambient, LightColour{0.1F, 0.1F, 0.1F, 1.0F},
                                     coney::graphics::kLightsObjects | coney::graphics::kLightsWorld));
    LightDescriptor moon =
        global(LightType::Directional, LightColour{0.3F, 0.3F, 0.4F, 1.0F}, coney::graphics::kLightsObjects);
    moon.direction = Vec3{0.0F, -1.0F, 0.0F};
    const LightHandle moonlight = level.lights.addLight(moon);
    const LightHandle near = level.lights.addLight(lamp({0, 0, 10}, 3.0F, coney::graphics::kLightsWorld | 1));
    const LightHandle behind = level.lights.addLight(lamp({0, 0, -10}, 3.0F));
    const LightHandle far = level.lights.addLight(lamp({0, 0, 90}, 3.0F)); // beyond 115 × 0.75
    level.lights.setWorldAmbient(0.2F, 0.2F, 0.2F);
    level.fog.colour = coney::graphics::fogColourOf(0.5F, 0.25F, 0.0F);
    level.fog.start = 0.4F;
    level.lights.beginViewport(camera(), 0);

    const LightingFrame frame = buildLightingFrame(level, camera());

    // The cull's lists as the manager made them.
    CHECK(frame.worldList ==
          std::vector<std::uint16_t>(level.lights.worldList().begin(), level.lights.worldList().end()));
    CHECK(frame.objectList ==
          std::vector<std::uint16_t>(level.lights.objectList().begin(), level.lights.objectList().end()));
    CHECK(frame.pulseList ==
          std::vector<std::uint16_t>(level.lights.pulseList().begin(), level.lights.pulseList().end()));
    CHECK(frame.points == std::vector<std::uint16_t>{static_cast<std::uint16_t>(near - 1)});
    CHECK(frame.worldPoints == std::vector<std::uint16_t>{static_cast<std::uint16_t>(near - 1)});
    CHECK(holds(frame.objectList, static_cast<std::uint16_t>(moonlight - 1)));

    // Every light of a list once, in record order; the lamps the cull dropped are not there.
    REQUIRE_FALSE(frame.lights.empty());
    for (std::size_t i = 1; i < frame.lights.size(); ++i) {
        CHECK(frame.lights[i - 1].record < frame.lights[i].record);
    }
    CHECK(frame.light(static_cast<std::uint16_t>(behind - 1)) == nullptr);
    CHECK(frame.light(static_cast<std::uint16_t>(far - 1)) == nullptr);
    const FrameLight* lampLight = frame.light(static_cast<std::uint16_t>(near - 1));
    REQUIRE(lampLight != nullptr);
    CHECK(lampLight->type == LightType::Point);
    CHECK(lampLight->radius == 3.0F);
    CHECK(lampLight->position.z == 10.0F);
    CHECK(lampLight->lights == (coney::graphics::kLightsWorld | 1));
    CHECK(lampLight->colour == level.lights.record(static_cast<std::uint16_t>(near - 1)).current);
    // An ambient light shines with the brightness added; the frame keeps both colours.
    const FrameLight* ambientLight = frame.light(static_cast<std::uint16_t>(ambient - 1));
    REQUIRE(ambientLight != nullptr);
    CHECK(ambientLight->base == LightColour{0.1F, 0.1F, 0.1F, 1.0F});
    CHECK(ambientLight->colour == level.lights.record(static_cast<std::uint16_t>(ambient - 1)).current);
    const FrameLight* moonLight = frame.light(static_cast<std::uint16_t>(moonlight - 1));
    REQUIRE(moonLight != nullptr);
    CHECK(moonLight->direction.y == -1.0F);

    // The world ambient now, and the fog.
    CHECK(frame.worldAmbient == level.lights.record(LightManager::kWorldAmbient).current);
    CHECK(frame.fogColour == level.fog.colour);
    CHECK(frame.fogStart == 0.4F);
    CHECK(frame.view.farClip == 115.0F);
}

TEST_CASE("A flickering light enters the frame with this step's colour", "[graphics][lighting_frame]") {
    // Expected: lighting.md#flicker, confirmed (code)
    LevelLighting level;
    addFlickeringLamps(level);
    bool sawDimmed = false;
    for (int step = 0; step < 90; ++step) {
        level.lights.advance(camera(), 33);
        level.lights.beginViewport(camera(), static_cast<std::uint64_t>(step) * 33U);
        const LightingFrame frame = buildLightingFrame(level, camera());
        for (const std::uint16_t record : level.lights.visiblePoints()) {
            const FrameLight* light = frame.light(record);
            REQUIRE(light != nullptr);
            CHECK(light->colour == level.lights.record(record).current);
            CHECK(light->base == level.lights.record(record).desc.colour);
            CHECK(light->flicker == (level.lights.record(record).desc.effects & coney::graphics::kEffectFlickerMask));
            sawDimmed = sawDimmed || light->colour.r < light->base.r;
        }
    }
    CHECK(sawDimmed);
}

TEST_CASE("The level's shadow light: none until a script sets it, then its colour and offset in RenderWare's axes",
          "[graphics][lighting_frame]") {
    // Expected: xbox-lighting.md#shadow-light, confirmed (code)
    LevelLighting level;
    // The Xbox shadow object's defaults (Xbox 0x000bda20, docs/research/xbox-lighting.md#shadow-light).
    CHECK(level.shadow.colour == LightColour{0.7F, 0.7F, 0.7F, 1.0F});
    CHECK(level.shadow.offset.x == -500.0F);
    CHECK(level.shadow.offset.y == -500.0F);
    CHECK(level.shadow.offset.z == 850.0F);
    CHECK_FALSE(level.shadow.scripted);
    level.lights.beginViewport(camera(), 0);
    CHECK_FALSE(buildLightingFrame(level, camera()).shadow.has_value());

    // global.lua's usual row (docs/research/xbox-lighting.md#shadow-light): (0.77, 0.77, 0.79), (-500, -500, 850).
    level.shadow.setColour(0.77F, 0.77F, 0.79F);
    CHECK(level.shadow.scripted);
    CHECK(level.shadow.colour == LightColour{0.77F, 0.77F, 0.79F, 1.0F});
    level.shadow.setOffset(-500.0F, 500.0F, 550.0F);
    CHECK(level.shadow.offset.y == 500.0F);

    const LightingFrame frame = buildLightingFrame(level, camera());
    REQUIRE(frame.shadow.has_value());
    CHECK(frame.shadow->colour == LightColour{0.77F, 0.77F, 0.79F, 1.0F});
    // (x, y, z) in the game's axes is (x, z, -y) in RenderWare's.
    CHECK(frame.shadow->offset.x == -500.0F);
    CHECK(frame.shadow->offset.y == 550.0F);
    CHECK(frame.shadow->offset.z == -500.0F);
    // The light travels from the offset towards the casters: the offset reversed, unit.
    const float length = std::sqrt((500.0F * 500.0F) + (550.0F * 550.0F) + (500.0F * 500.0F));
    CHECK(frame.shadow->direction.x == Approx(500.0F / length));
    CHECK(frame.shadow->direction.y == Approx(-550.0F / length));
    CHECK(frame.shadow->direction.z == Approx(500.0F / length));

    // The offset alone also makes the level's shadow light.
    LevelLighting offsetOnly;
    offsetOnly.shadow.setOffset(500.0F, -500.0F, 2000.0F);
    const std::optional<coney::graphics::FrameShadowLight> light = coney::graphics::frameShadowLight(offsetOnly.shadow);
    REQUIRE(light.has_value());
    CHECK(light->colour == LightColour{0.7F, 0.7F, 0.7F, 1.0F});
    CHECK(light->offset.y == 2000.0F);
}

TEST_CASE("Everyone casts a shadow until a script turns his off; the frame lists those turned off",
          "[graphics][lighting_frame]") {
    // Expected: xbox-lighting.md#shadow-map, confirmed (code)
    LevelLighting level;
    CHECK(level.casters.enabled(7));
    level.casters.setEnabled(7, false);
    level.casters.setEnabled(3, false);
    level.casters.setEnabled(7, false);
    CHECK_FALSE(level.casters.enabled(7));
    CHECK_FALSE(level.casters.enabled(3));
    CHECK(level.casters.enabled(4));
    level.lights.beginViewport(camera(), 0);
    CHECK(buildLightingFrame(level, camera()).shadowsOff == std::vector<std::uint32_t>{3, 7});
    level.casters.setEnabled(3, true);
    CHECK(level.casters.enabled(3));
    CHECK(buildLightingFrame(level, camera()).shadowsOff == std::vector<std::uint32_t>{7});
}

TEST_CASE("Building the lighting frame changes nothing the simulation steps", "[graphics][lighting_frame]") {
    // Expected: lighting.md#flicker, confirmed (code)
    // Two copies of one level: the first only steps its lights, the second also culls and builds a frame every step.
    LevelLighting stepped;
    LevelLighting drawn;
    addFlickeringLamps(stepped);
    addFlickeringLamps(drawn);
    drawn.shadow.setColour(0.0F, 0.0F, 0.0F);
    drawn.casters.setEnabled(1, false);
    std::size_t framedLights = 0;
    for (int step = 0; step < 300; ++step) {
        stepped.lights.advance(camera(), 33);
        drawn.lights.advance(camera(), 33);
        drawn.lights.beginViewport(camera(), static_cast<std::uint64_t>(step) * 33U);
        framedLights += buildLightingFrame(drawn, camera()).lights.size();
    }
    REQUIRE(framedLights > 0);

    // The flicker's state, every record's, and the manager's random numbers are the same.
    CHECK(stepped.lights.random().index() == drawn.lights.random().index());
    CHECK(stepped.lights.count() == drawn.lights.count());
    for (std::uint16_t i = 0; i < 16; ++i) {
        const coney::graphics::LightRecord& a = stepped.lights.record(i);
        const coney::graphics::LightRecord& b = drawn.lights.record(i);
        CHECK(a.inUse == b.inUse);
        CHECK(a.desc.colour == b.desc.colour);
        CHECK(a.timerMs == b.timerMs);
        CHECK(a.burstLeft == b.burstLeft);
        CHECK(a.dimmed == b.dimmed);
        if (a.desc.type == LightType::Point) {
            CHECK(a.current == b.current);
        }
    }
}

TEST_CASE("The stealth shadow flag never reads the level's shadow light or casters", "[graphics][lighting_frame]") {
    // Expected: stealth.md#shadow-ground, confirmed (code)
    // The same player on the same shadow ground (docs/research/stealth.md#shadow-ground), with the render-side
    // shadow state left alone in one run and turned upside down, with frames built, in the other.
    const auto hidesOnShadow = [](bool renderStateChanged) {
        coney::test::AiScene scene;
        scene.mesh = coney::test::makeMesh(
            coney::test::join(coney::test::floorAt(0.0F, 0.0F, 40.0F, 0.0F, 80.0F, 1, coney::raycast::kTriangleShadow),
                              coney::test::floorAt(0.0F, 40.0F, 80.0F, 0.0F, 80.0F)));
        scene.brains.setCollision(scene.mesh.get());
        LevelLighting level;
        coney::ai::Brain& player = scene.player();
        if (renderStateChanged) {
            level.shadow.setColour(0.0F, 0.0F, 0.0F);
            level.shadow.setOffset(0.0F, 0.0F, 1.0F);
            level.casters.setEnabled(static_cast<std::uint32_t>(player.handle()), false);
            static_cast<void>(level.lights.addLight(lamp({20, 0, -40}, 50.0F, coney::graphics::kLightsWorld | 1)));
        }
        std::vector<bool> hidden;
        coney::ai::HideMemory memory;
        for (const float x : {20.0F, 30.0F, 60.0F, 10.0F}) {
            player.human().place(coney::anim::Vec3{x, 40.0F, 0.0F}, 0.0F);
            scene.run(1);
            if (renderStateChanged) {
                level.lights.beginViewport(camera(), 0);
                static_cast<void>(buildLightingFrame(level, camera()));
            }
            hidden.push_back(player.human().onShadowGround());
            hidden.push_back(coney::ai::updateHiding(scene.brains, player, memory, false).mayHide);
            hidden.push_back(player.human().hidden());
        }
        return hidden;
    };
    const std::vector<bool> plain = hidesOnShadow(false);
    CHECK(plain == std::vector<bool>{true, true, true, true, true, true, false, false, false, true, true, true});
    CHECK(hidesOnShadow(true) == plain);
}
