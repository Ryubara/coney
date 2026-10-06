// SPDX-License-Identifier: GPL-3.0-or-later

// Checks against the player's own disc: level99's scripts, run as gameplay runs them at checkpoint 3, light the level
// with the values docs/research/lighting.md#level99 read at runtime: 96 lights in use, the moonlight, the reflected
// light and the object ambient with the brightness added, the 0.227 world ambient, 90 lamps of which 49 are point
// lights, the coronas by rectangle, and the fog. Runs only when CONEY_DISC names the disc; prints counts only.

#include <array>
#include <cstddef>
#include <cstdio>
#include <optional>
#include <string_view>
#include <utility>

#include <SDL3/SDL_stdinc.h>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "fileio/disc.h"
#include "fileio/wad.h"
#include "gamemodes/level_start.h"
#include "graphics/level_lighting.h"
#include "scripting/config_strings.h"

using Catch::Approx;

namespace {

// The disc named by CONEY_DISC, opened; nothing when it is not set.
std::optional<coney::io::Wad> openDisc() {
    const char* discPath = SDL_getenv("CONEY_DISC");
    if (discPath == nullptr || *discPath == '\0') {
        return std::nullopt;
    }
    auto disc = coney::io::Disc::open(discPath);
    REQUIRE(disc.has_value());
    auto wad = coney::io::Wad::open(std::move(*disc));
    REQUIRE(wad.has_value());
    return wad ? std::optional<coney::io::Wad>(std::move(*wad)) : std::nullopt;
}

} // namespace

TEST_CASE("level99's scripts light it as the original does at checkpoint 3") {
    const std::optional<coney::io::Wad> wad = openDisc();
    if (!wad) {
        SKIP("CONEY_DISC is not set");
    }
    coney::LevelScripts scripts(coney::script::wadScriptSource(*wad), "level99", 3, [](std::string_view) {});
    coney::graphics::LevelLighting lighting;
    scripts.context().lighting = &lighting;
    (void)coney::runLevelScript(scripts.scripts(), scripts.state(), scripts.humans(), scripts.flags(), "level99");

    const coney::graphics::LightManager& lights = lighting.lights;
    // The three built in, global.lua's three, the level's 90 lamps.
    CHECK(lights.count() == 96);

    // global.lua's lights: directional and ambient, lighting objects, with 0.157 added.
    std::size_t directional = 0;
    std::size_t ambient = 0;
    std::size_t points = 0;
    std::size_t lamps = 0;
    std::array<std::size_t, 6> coronas{};
    for (std::uint16_t i = coney::graphics::LightManager::kGlow + 1; i < coney::graphics::kLightCapacity; ++i) {
        const coney::graphics::LightRecord& record = lights.record(i);
        if (!record.inUse) {
            continue;
        }
        const coney::graphics::LightDescriptor& d = record.desc;
        if (d.type == coney::graphics::LightType::Directional) {
            ++directional;
            CHECK(d.lights == coney::graphics::kLightsObjects);
            if (d.colour.b == Approx(0.18F)) { // the moonlight
                CHECK(record.current.r == Approx(0.227F).margin(0.002));
                CHECK(record.current.g == Approx(0.257F).margin(0.002));
                CHECK(record.current.b == Approx(0.337F).margin(0.002));
            }
        } else if (d.type == coney::graphics::LightType::Ambient) {
            ++ambient;
            CHECK(record.current.r == Approx(0.212F).margin(0.002));
        } else {
            ++lamps;
            points += d.radius > 0.0F ? 1 : 0;
            if (d.corona >= 0 && d.corona < 6) {
                ++coronas.at(static_cast<std::size_t>(d.corona));
            }
        }
    }
    CHECK(directional == 2);
    CHECK(ambient == 1);
    CHECK(lamps == 90);
    CHECK(points == 49);
    CHECK(coronas == std::array<std::size_t, 6>{20, 0, 7, 10, 11, 0});

    // The world ambient 0.07 + 0.157, and the fog.
    CHECK(lights.worldAmbientBase().r + lights.addedColour().r == Approx(0.227F).margin(0.002));
    CHECK(lighting.fog.colour == coney::graphics::Rgba{12, 12, 5, 255});
    CHECK(lighting.fog.start == Approx(0.5F));
    std::printf("level99 lighting: %zu lights, %zu lamps (%zu point lights)\n", lights.count(), lamps, points);
}
