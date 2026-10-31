// SPDX-License-Identifier: GPL-3.0-or-later
// The level's lighting state that scripts set (docs/research/lighting.md#world,
// docs/research/xbox-lighting.md#shadow-light): the fog colour as the original narrows it, and the shadow light and
// casters the Xbox's shadow object keeps, as the lighting frame hands them on. Synthetic values only.
#include "graphics/level_lighting.h"

#include <cmath>
#include <cstdint>
#include <optional>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "graphics/light_manager.h"
#include "graphics/lighting_frame.h"
#include "graphics/render_device.h"

using Catch::Approx;
using coney::graphics::buildLightingFrame;
using coney::graphics::LevelLighting;
using coney::graphics::LightColour;
using coney::graphics::LightingFrame;
using coney::graphics::LightView;

namespace {

// A camera at the origin looking along +z, draw distance 115.
LightView camera() {
    return LightView{.position = {}, .forward = {0.0F, 0.0F, 1.0F}, .nearClip = 0.5F, .farClip = 115};
}

} // namespace

TEST_CASE("The level's shadow light: none until a script sets it, then its colour and offset in RenderWare's axes",
          "[graphics][level_lighting]") {
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
          "[graphics][level_lighting]") {
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

// Level_SetFogColour (0x0040c868) truncates each component × 255 with Float_ToUInt (0x0042c718), which gives 0 for a
// negative number, and keeps the low byte, so a component above 1 wraps instead of saturating.
TEST_CASE("The fog colour keeps each component's low byte, as the original truncates and narrows it",
          "[graphics][level_lighting]") {
    // Expected: lighting.md#world, confirmed (code)
    const coney::graphics::Rgba inRange = coney::graphics::fogColourOf(1.0F, 0.5F, 0.0F);
    CHECK(inRange.r == 255);
    CHECK(inRange.g == 127);
    CHECK(inRange.b == 0);
    CHECK(inRange.a == 255);

    const coney::graphics::Rgba outOfRange = coney::graphics::fogColourOf(1.2F, -0.5F, 2.0F);
    CHECK(outOfRange.r == 50);  // 306 wraps to 50
    CHECK(outOfRange.g == 0);   // negative is 0
    CHECK(outOfRange.b == 254); // 510 wraps to 254
    CHECK(outOfRange.a == 255);
}
