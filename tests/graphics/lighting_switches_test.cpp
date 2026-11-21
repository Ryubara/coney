// SPDX-License-Identifier: GPL-3.0-or-later
// The lighting lab's drawing-side switches (docs/guides/realtime-lighting.md#lab): the layers that leave lights out of
// a draw or draw them with another colour, the views, and Hold lighting, which keeps one moment's lighting while no
// step runs. Synthetic lights only; nothing here reaches the simulation, and the page that sets them is tested in
// tests/debug/lighting_page_test.cpp.
#include "graphics/lighting_switches.h"

#include <cstdint>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "graphics/light_manager.h"

using Catch::Approx;
using coney::graphics::LightColour;
using coney::graphics::LightDescriptor;
using coney::graphics::LightHandle;
using coney::graphics::LightingHold;
using coney::graphics::LightingLayers;
using coney::graphics::LightingSwitches;
using coney::graphics::LightingView;
using coney::graphics::LightManager;
using coney::graphics::LightSelection;
using coney::graphics::LightType;
using coney::graphics::LightView;
using coney::graphics::LitMoment;

namespace {

// A light of `type` in `colour`, lighting objects; a point light is 3 m round (0, 0, 5).
LightDescriptor light(LightType type, LightColour colour) {
    LightDescriptor desc;
    desc.type = type;
    desc.colour = colour;
    desc.lights = coney::graphics::kLightsObjects;
    desc.position = {0.0F, 0.0F, 5.0F};
    desc.radius = type == LightType::Point ? 3.0F : 0.0F;
    return desc;
}

// A selection of `records`, in order.
LightSelection selectionOf(const std::vector<std::uint16_t>& records) {
    LightSelection selection;
    for (const std::uint16_t record : records) {
        selection.records.at(selection.count++) = record;
    }
    return selection;
}

// The records of `selection`, in order.
std::vector<std::uint16_t> recordsOf(const LightSelection& selection) {
    return {selection.lights().begin(), selection.lights().end()};
}

// The record index of `handle`.
std::uint16_t recordOf(LightHandle handle) { return static_cast<std::uint16_t>(handle - 1); }

} // namespace

TEST_CASE("the lighting lab's layers leave their lights out of a draw, in the selection's order",
          "[graphics][lighting_lab]") {
    // Expected: unverified, #1250 (Coney's own debug tooling: the layers of docs/guides/realtime-lighting.md#lab)
    LightManager lights;
    const std::uint16_t ambient =
        recordOf(lights.addLight(light(LightType::Ambient, LightColour{0.1F, 0.1F, 0.1F, 1.0F})));
    const std::uint16_t moon =
        recordOf(lights.addLight(light(LightType::Directional, LightColour{0.2F, 0.2F, 0.3F, 1.0F})));
    const std::uint16_t lamp = recordOf(lights.addLight(light(LightType::Point, LightColour{1.0F, 0.8F, 0.6F, 1.0F})));
    const LightSelection chosen = selectionOf({LightManager::kWorldAmbient, ambient, moon, LightManager::kGlow, lamp});

    // All on, the final view: the selection as it is.
    CHECK(recordsOf(coney::graphics::layeredSelection(chosen, lights, LightingLayers{}, LightingView::Final)) ==
          recordsOf(chosen));
    LightingLayers layers;
    layers.worldAmbient = false;
    CHECK(recordsOf(coney::graphics::layeredSelection(chosen, lights, layers, LightingView::Final)) ==
          std::vector<std::uint16_t>{ambient, moon, LightManager::kGlow, lamp});
    layers = LightingLayers{};
    layers.objectLights = false;
    CHECK(recordsOf(coney::graphics::layeredSelection(chosen, lights, layers, LightingView::Final)) ==
          std::vector<std::uint16_t>{LightManager::kWorldAmbient, LightManager::kGlow, lamp});
    layers = LightingLayers{};
    layers.pointLights = false;
    CHECK(recordsOf(coney::graphics::layeredSelection(chosen, lights, layers, LightingView::Final)) ==
          std::vector<std::uint16_t>{LightManager::kWorldAmbient, ambient, moon});
    // The prelight-only view draws no runtime light at all.
    CHECK(coney::graphics::layeredSelection(chosen, lights, LightingLayers{}, LightingView::PrelightOnly).count == 0);

    // Switches at their defaults draw the faithful picture; any layer off or another view does not.
    LightingSwitches switches;
    CHECK(switches.faithfulPicture());
    switches.hold = true;
    switches.lightFromGameCamera = false;
    CHECK(switches.faithfulPicture());
    switches.layers.coronas = false;
    CHECK_FALSE(switches.faithfulPicture());
    switches.layers.coronas = true;
    switches.view = LightingView::PrelightOnly;
    CHECK_FALSE(switches.faithfulPicture());
}

TEST_CASE("the lighting lab draws flickering lights at their base colour and lights without the brightness on request",
          "[graphics][lighting_lab]") {
    // Expected: lighting.md#colour, confirmed (code): ambient and directional lights get the brightness and offset
    // added; lighting.md#flicker: the flicker changes a point light's current colour, never its base
    LightManager lights;
    const std::uint16_t ambient =
        recordOf(lights.addLight(light(LightType::Ambient, LightColour{0.1F, 0.1F, 0.1F, 1.0F})));
    LightDescriptor flickering = light(LightType::Point, LightColour{1.0F, 0.8F, 0.6F, 1.0F});
    flickering.effects = coney::graphics::kFlickerRandom;
    const std::uint16_t lamp = recordOf(lights.addLight(flickering));
    // Step the flicker until the lamp shines with another colour than its base.
    const LightView view{.position = {}, .forward = {0.0F, 0.0F, 1.0F}, .nearClip = 0.5F, .farClip = 115.0F};
    for (int i = 0; i < 200 && lights.record(lamp).current == lights.record(lamp).desc.colour; ++i) {
        lights.advance(view, 33);
    }
    REQUIRE_FALSE(lights.record(lamp).current == lights.record(lamp).desc.colour);

    const LightingLayers on{};
    CHECK(coney::graphics::layeredColour(lights, lamp, on) == lights.record(lamp).current);
    CHECK(coney::graphics::layeredColour(lights, ambient, on) == lights.record(ambient).current);
    LightingLayers layers;
    layers.flicker = false;
    CHECK(coney::graphics::layeredColour(lights, lamp, layers) == LightColour{1.0F, 0.8F, 0.6F, 1.0F});
    CHECK(coney::graphics::layeredColour(lights, ambient, layers) == lights.record(ambient).current);
    layers = LightingLayers{};
    layers.brightness = false;
    const LightColour bare = coney::graphics::layeredColour(lights, ambient, layers);
    CHECK(bare.r == Approx(0.1F));
    CHECK(bare.g == Approx(0.1F));
    CHECK(bare.b == Approx(0.1F));
    CHECK(lights.record(ambient).current.r > 0.2F); // 0.1 + 40/255
    CHECK(coney::graphics::layeredColour(lights, lamp, layers) == lights.record(lamp).current);
}

TEST_CASE("Hold lighting keeps the held moment while no step runs, and a step moves it on",
          "[graphics][lighting_lab]") {
    // Expected: unverified, #1250 (Coney's own debug tooling: Hold lighting, docs/guides/realtime-lighting.md#lab)
    LightingHold hold;
    const auto at = [](float x, std::uint64_t ms) {
        return LitMoment{.view = LightView{.position = {x, 0.0F, 0.0F}}, .nowMs = ms};
    };
    // Off, every frame is lit as asked.
    CHECK(hold.choose(at(1.0F, 100), false).view.position.x == 1.0F);
    CHECK(hold.choose(at(2.0F, 100), false).view.position.x == 2.0F);
    // On, with no step, the moment of the last frame stays, whatever the camera does.
    const LitMoment held = hold.choose(at(3.0F, 133), true);
    CHECK(held.view.position.x == 2.0F);
    CHECK(held.nowMs == 100);
    CHECK(hold.choose(at(9.0F, 133), true).view.position.x == 2.0F);
    // A step moves it on to the next frame's moment, which then stays.
    hold.stepped();
    const LitMoment next = hold.choose(at(4.0F, 166), true);
    CHECK(next.view.position.x == 4.0F);
    CHECK(next.nowMs == 166);
    CHECK(hold.choose(at(5.0F, 166), true).view.position.x == 4.0F);
    // Off again, the camera's moment comes back at once.
    CHECK(hold.choose(at(6.0F, 166), false).view.position.x == 6.0F);
}

TEST_CASE("the World page's fog draws the level's fog unless overridden, its start below the far clip",
          "[graphics][world_page]") {
    // Expected: world.md#fog, confirmed (code): the level's fog is SetFogColor's colour and SetFogDistance's fraction
    // of the far clip (WorldFog); the overrides and the 0.99 cap are Coney's own debug tooling (debug-menu.md#world),
    // the cap the sandbox's fog plane's (sandbox_renderer.cpp)
    coney::graphics::WorldFog level;
    level.colour = coney::graphics::Rgba{10, 20, 30, 255};
    level.start = 0.5F;
    coney::graphics::FogSwitches fog;
    // No override: the level's fog, as it is.
    CHECK(coney::graphics::shownFog(level, fog).colour == level.colour);
    CHECK(coney::graphics::shownFog(level, fog).start == 0.5F);
    // A colour and a start of the page's own.
    fog.colour = coney::graphics::Rgba{200, 100, 50, 255};
    fog.start = 0.25F;
    CHECK(coney::graphics::shownFog(level, fog).colour == coney::graphics::Rgba{200, 100, 50, 255});
    CHECK(coney::graphics::shownFog(level, fog).start == 0.25F);
    // A start at or past the far clip is held just below it, and none below 0.
    fog.start = 1.0F;
    CHECK(coney::graphics::shownFog(level, fog).start == coney::graphics::kMaxFogStart);
    fog.start = -1.0F;
    CHECK(coney::graphics::shownFog(level, fog).start == 0.0F);
}

TEST_CASE("the World page's parts and fog count against the faithful picture", "[graphics][world_page]") {
    // Expected: unverified, #216 (Coney's own debug tooling: the World page, docs/guides/debug-menu.md#world)
    LightingSwitches switches;
    REQUIRE(switches.faithfulPicture());
    CHECK(switches.parts == coney::graphics::WorldParts{});
    switches.parts.humans = false;
    CHECK_FALSE(switches.faithfulPicture());
    switches.parts = {};
    switches.fog.start = 0.3F;
    CHECK_FALSE(switches.faithfulPicture());
    switches.fog = {};
    CHECK(switches.faithfulPicture());
}
