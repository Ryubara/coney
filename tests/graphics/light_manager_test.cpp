// SPDX-License-Identifier: GPL-3.0-or-later
// The light manager (docs/research/lighting.md): the built-in lights, the brightness and colour offset on ambient and
// directional lights only, the cull into lists, the per-atomic selection and its caps, the coronas and the flicker
// modes. Synthetic lights only; level99's values are checked against the disc in disc_lighting_test.cpp.
#include "graphics/light_manager.h"

#include <cmath>
#include <cstdint>
#include <set>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using Catch::Approx;
using coney::graphics::LightColour;
using coney::graphics::LightDescriptor;
using coney::graphics::LightHandle;
using coney::graphics::LightManager;
using coney::graphics::LightType;
using coney::graphics::LightView;
using coney::world::Vec3;

namespace {

// A camera at the origin looking along +z, draw distance 115.
LightView camera() {
    return LightView{.position = {}, .forward = {0.0F, 0.0F, 1.0F}, .nearClip = 0.5F, .farClip = 115};
}

// A point lamp at `at` of radius `radius` lighting `lights`, white.
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

} // namespace

TEST_CASE("The manager starts with its three built-in lights and brightness 40") {
    LightManager manager;
    CHECK(manager.count() == 3);
    CHECK(manager.record(LightManager::kWorldAmbient).desc.type == LightType::Ambient);
    CHECK(manager.record(LightManager::kWorldAmbient).desc.on);
    CHECK_FALSE(manager.record(LightManager::kPulse).desc.on);
    CHECK(manager.record(LightManager::kGlow).desc.radius == Approx(0.4F));
    CHECK(manager.brightness().r == Approx(40.0F / 255.0F));
    // Built-in lights cannot be removed.
    CHECK_FALSE(manager.removeLight(1));
}

TEST_CASE("Ambient and directional lights get brightness and offset; point lights do not") {
    LightManager manager;
    const LightHandle moon = manager.addLight(
        global(LightType::Directional, LightColour{0.07F, 0.10F, 0.18F, 1.0F}, coney::graphics::kLightsObjects));
    const LightHandle point = manager.addLight(lamp({0, 0, 5}, 3.0F));
    // level99's moonlight: (0.07, 0.10, 0.18) + 0.157.
    CHECK(manager.light(moon)->current.r == Approx(0.227F).margin(0.001));
    CHECK(manager.light(moon)->current.g == Approx(0.257F).margin(0.001));
    CHECK(manager.light(moon)->current.b == Approx(0.337F).margin(0.001));
    CHECK(manager.light(point)->current.r == Approx(1.0F));

    // A changed offset is re-applied to what exists; a negative total adds nothing.
    manager.setColourOffset(0.1F, -0.5F, 0.0F);
    CHECK(manager.light(moon)->current.r == Approx(0.07F + 0.157F + 0.1F).margin(0.001));
    CHECK(manager.light(moon)->current.g == Approx(0.10F));
    manager.setColourOffset(0.0F, 0.0F, 0.0F);
    manager.setBrightness(0);
    CHECK(manager.light(moon)->current.r == Approx(0.07F));
    manager.setBrightness(100);
    CHECK(manager.light(moon)->current.r == Approx(0.07F + 100.0F / 255.0F));
}

TEST_CASE("The world ambient is SetWorldAmbient's colour plus 0.07 plus the brightness") {
    LightManager manager;
    manager.beginViewport(camera(), 0);
    CHECK(manager.record(LightManager::kWorldAmbient).current.r == Approx(0.157F).margin(0.001));
    manager.setWorldAmbient(0.0F, 0.0F, 0.0F);
    manager.beginViewport(camera(), 0);
    CHECK(manager.record(LightManager::kWorldAmbient).current.r == Approx(0.227F).margin(0.001));
}

TEST_CASE("The pulse is a triangle wave between black and 0.25 over 600 ms") {
    LightManager manager;
    manager.setBrightness(0);
    manager.beginViewport(camera(), 0);
    CHECK(manager.record(LightManager::kPulse).current.r == Approx(0.0F));
    manager.beginViewport(camera(), 150);
    CHECK(manager.record(LightManager::kPulse).current.r == Approx(0.125F));
    manager.beginViewport(camera(), 450);
    CHECK(manager.record(LightManager::kPulse).current.r == Approx(0.125F));
    manager.beginViewport(camera(), 599);
    CHECK(manager.record(LightManager::kPulse).current.r == Approx(0.25F / 300.0F).margin(0.001));
}

TEST_CASE("The cull sorts lights into the world's, the objects' and the pulse's lists") {
    LightManager manager;
    const LightHandle both = manager.addLight(global(LightType::Ambient, LightColour{0.1F, 0.1F, 0.1F, 1.0F},
                                                     coney::graphics::kLightsObjects | coney::graphics::kLightsWorld));
    const LightHandle near = manager.addLight(lamp({0, 0, 10}, 3.0F, coney::graphics::kLightsWorld | 1));
    const LightHandle behind = manager.addLight(lamp({0, 0, -10}, 3.0F));
    const LightHandle far = manager.addLight(lamp({0, 0, 90}, 3.0F)); // beyond 115 × 0.75
    manager.beginViewport(camera(), 0);

    CHECK(manager.worldList().size() == 2); // light A and the both-ways ambient
    CHECK(manager.objectList().size() == 1);
    CHECK(manager.pulseList().size() == 2); // the pulse (off) and the ambient
    CHECK(manager.visiblePoints().size() == 1);
    CHECK(manager.visiblePoints()[0] == near - 1);
    CHECK(manager.visibleWorldPoints().size() == 1);
    (void)both;
    (void)behind;
    (void)far;
}

TEST_CASE("Objects get 6 lights within 40 m, 3 beyond 69.3 m; the world 8") {
    LightManager manager;
    for (int i = 0; i < 9; ++i) {
        manager.addLight(global(LightType::Directional, LightColour{0.01F, 0.01F, 0.01F, 1.0F},
                                coney::graphics::kLightsObjects | coney::graphics::kLightsWorld));
    }
    manager.beginViewport(camera(), 0);
    const coney::graphics::LightSphere sphere{{0, 0, 5}, 1.0F};
    CHECK(manager.select(0.0F, sphere, true, false, false).count == 6);
    CHECK(manager.select(1600.0F, sphere, true, false, false).count == 5);
    CHECK(manager.select(3300.0F, sphere, true, false, false).count == 4);
    CHECK(manager.select(4900.0F, sphere, true, false, false).count == 3);
    CHECK(manager.select(9000.0F, sphere, true, false, true).count == 4); // list C keeps 4
    CHECK(manager.select(0.0F, sphere, false, false, false).count == 8);
}

TEST_CASE("Point lights are chosen by sphere overlap; a full list keeps the first plus the deepest overlap") {
    LightManager manager;
    manager.addLight(
        global(LightType::Ambient, LightColour{0.05F, 0.05F, 0.05F, 1.0F}, coney::graphics::kLightsObjects));
    // Lamps 2 to 5 m away, radius 3; the atomic's sphere radius 1. One far lamp does not overlap.
    std::set<std::uint16_t> lamps;
    for (int i = 0; i < 6; ++i) {
        lamps.insert(
            static_cast<std::uint16_t>(manager.addLight(lamp({0, 0, 20.0F + static_cast<float>(i) * 0.6F}, 3.0F)) - 1));
    }
    const LightHandle apart = manager.addLight(lamp({30, 0, 20}, 3.0F));
    manager.beginViewport(camera(), 0);

    // An object close by: ambient (object list holds 1) + up to 5 point lights.
    const coney::graphics::LightSphere sphere{{0, 0, 22}, 1.0F};
    const coney::graphics::LightSelection selection = manager.select(0.0F, sphere, true, true, false);
    CHECK(selection.count == 6);
    for (std::size_t i = 1; i < selection.count; ++i) {
        CHECK(lamps.contains(selection.records.at(i)));
        CHECK(selection.records.at(i) != apart - 1);
    }
    // Without point lights only the ambient.
    CHECK(manager.select(0.0F, sphere, true, false, false).count == 1);
}

TEST_CASE("A glowing human gets the glow first, at him and in his colour") {
    LightManager manager;
    manager.beginViewport(camera(), 0);
    const coney::graphics::GlowRequest glow{.position = {1, 2, 3}, .rgba = {255, 0, 0, 200}};
    const coney::graphics::LightSelection selection = manager.select(0.0F, {{1, 2, 3}, 1.0F}, true, true, false, glow);
    REQUIRE(selection.count == 1);
    CHECK(selection.records[0] == LightManager::kGlow);
    CHECK(manager.record(LightManager::kGlow).current.r == Approx(1.0F));
    // An alpha of 10 or less does not glow.
    const coney::graphics::GlowRequest faint{.position = {1, 2, 3}, .rgba = {255, 0, 0, 10}};
    CHECK(manager.select(0.0F, {{1, 2, 3}, 1.0F}, true, true, false, faint).count == 0);
}

TEST_CASE("Coronas sit raised and pulled towards the camera, fading within 8 m") {
    LightManager manager;
    LightDescriptor desc = lamp({0, 0, 20}, 0.0F);
    desc.colour = LightColour{1.0F, 0.5F, 0.0F, 1.0F};
    desc.corona = 3;
    desc.coronaHeight = 1.0F;
    desc.coronaPull = 0.5F;
    desc.coronaSize = 2.2F;
    manager.addLight(desc);
    LightDescriptor close = desc;
    close.position = Vec3{0, 0, 1};
    close.coronaHeight = 0.0F;
    close.coronaPull = 0.0F;
    manager.addLight(close); // 1 m away: alpha capped at 31.87
    LightDescriptor touching = close;
    touching.position = Vec3{0, 0, 0.6F}; // 0.6 m: alpha 19
    manager.addLight(touching);
    LightDescriptor none = desc;
    none.corona = -1;
    manager.addLight(none);
    manager.beginViewport(camera(), 0);

    REQUIRE(manager.coronas().size() == 3);
    const coney::graphics::CoronaSprite& sprite = manager.coronas()[0];
    CHECK(sprite.position.y == Approx(1.0F - 0.5F * 1.0F / std::sqrt(401.0F)).margin(0.001));
    CHECK(sprite.position.z < 20.0F);
    CHECK(sprite.size == Approx(2.2F));
    CHECK(sprite.rect == 3);
    CHECK(sprite.rgba[0] == 255);
    CHECK(sprite.rgba[1] == 127);
    CHECK(sprite.rgba[3] == 255);
    CHECK(manager.coronas()[1].rgba[3] == 31);
    CHECK(manager.coronas()[2].rgba[3] == 19);
}

TEST_CASE("Random flicker scales the base colour; blink alternates base and dimmed") {
    LightManager manager;
    LightDescriptor flickering = lamp({0, 0, 10}, 3.0F);
    flickering.effects = coney::graphics::kFlickerRandom;
    const LightHandle random = manager.addLight(flickering);
    const LightHandle blink = manager.addLight(lamp({0, 0, 12}, 3.0F));
    REQUIRE(manager.setFlicker(blink, coney::graphics::FlickerTiming{.onTime = 100, .offTime = 50, .dim = 20}));
    CHECK((manager.light(blink)->desc.effects & coney::graphics::kEffectFlickerMask) == coney::graphics::kFlickerBlink);

    bool sawDim = false;
    bool sawOn = false;
    bool randomChanged = false;
    for (int step = 0; step < 60; ++step) {
        manager.advance(camera(), 33);
        const float r = manager.light(random)->current.r;
        CHECK(r <= 1.0F);
        randomChanged = randomChanged || r < 1.0F;
        const float b = manager.light(blink)->current.r;
        if (b == Approx(0.2F)) {
            sawDim = true;
        } else if (b == Approx(1.0F)) {
            sawOn = true;
        }
    }
    CHECK(randomChanged);
    CHECK(sawDim);
    CHECK(sawOn);
}

TEST_CASE("Burst flicker pauses at the base colour between bursts") {
    LightManager manager;
    const LightHandle handle = manager.addLight(lamp({0, 0, 10}, 3.0F));
    REQUIRE(manager.setFlicker(
        handle, coney::graphics::FlickerTiming{.pause = 1000, .burst = 3, .flickerTime = 40, .dim = 50}));
    CHECK((manager.light(handle)->desc.effects & coney::graphics::kEffectFlickerMask) ==
          coney::graphics::kFlickerBurst);
    // During the first pause the light shines with its base colour.
    manager.advance(camera(), 500);
    CHECK(manager.light(handle)->current.r == Approx(1.0F));
    // After it, flickers at most half the base.
    manager.advance(camera(), 600);
    manager.advance(camera(), 1);
    CHECK(manager.light(handle)->current.r < 0.5F);
}

TEST_CASE("A light out of sight does not flicker") {
    LightManager manager;
    LightDescriptor desc = lamp({0, 0, -50}, 3.0F);
    desc.effects = coney::graphics::kFlickerRandom;
    const LightHandle handle = manager.addLight(desc);
    for (int i = 0; i < 30; ++i) {
        manager.advance(camera(), 33);
    }
    CHECK(manager.light(handle)->current.r == Approx(1.0F));
}

TEST_CASE("The pool holds 512 lights and reuses removed records") {
    LightManager manager;
    LightHandle last = 0;
    for (std::size_t i = 3; i < coney::graphics::kLightCapacity; ++i) {
        last = manager.addLight(lamp({0, 0, 10}, 1.0F));
        REQUIRE(last != 0);
    }
    CHECK(manager.addLight(lamp({0, 0, 10}, 1.0F)) == 0);
    CHECK(manager.removeLight(last));
    CHECK(manager.light(last) == nullptr);
    CHECK(manager.addLight(lamp({0, 0, 10}, 1.0F)) == last);
}
