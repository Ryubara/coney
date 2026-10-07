// SPDX-License-Identifier: GPL-3.0-or-later
// The HUD's scripted bars (hud/scripted_bars.h): what HUDEnableBar makes of each kind, the fills, the property call,
// the right column's layout and the chase gauge's marker, on synthetic sheets with no texture.
#include "hud/scripted_bars.h"

#include <algorithm>
#include <cstddef>
#include <string>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "graphics/overlay_camera.h"
#include "graphics/particle_page.h"
#include "graphics/sprite_batch.h"
#include "hud/hud_layout.h"

using Catch::Approx;
using coney::hud::ScriptedBars;

namespace {

// A sheet of `count` rectangles, each the whole texture, with no texture.
coney::graphics::SpriteSheet sheetOf(std::size_t count) {
    coney::graphics::SpriteSheet sheet;
    sheet.page.rects.assign(count, coney::graphics::UvRect{});
    return sheet;
}

// A canvas with a part_page0 of 64 rectangles and a chasebar sheet (record 26) of 8.
struct Canvas {
    coney::graphics::SpriteBatch parts{sheetOf(64), 256, 10000.0F};
    coney::graphics::SpriteBatch chasebar{sheetOf(8), 16, 11000.0F};
    coney::hud::HudCanvas canvas;
    Canvas() {
        canvas.parts = &parts;
        canvas.sheet = [this](std::uint32_t record) { return record == 26 ? &chasebar : nullptr; };
    }
};

// The GUI y a sprite's overlay position stands for.
float guiY(const coney::graphics::Sprite& sprite) { return 0.5F - sprite.position.y; }

} // namespace

TEST_CASE("HUDEnableBar kind 1 makes one full labelled bar in slot 0, red to green as it empties", "[hud]") {
    ScriptedBars bars;
    const std::vector<std::string> labels{"Car"};
    bars.enable(1, true, labels, 0, false, 0, 0);
    REQUIRE(bars.bar(0).exists);
    CHECK(bars.bar(0).label == "Car");
    CHECK(bars.bar(0).fill == 1.0F);
    CHECK(bars.bar(0).gradient);
    CHECK(bars.bar(0).width == Approx(0.22F));
    // A second call while it exists does nothing.
    bars.setPercentage(1, 0.5F, 1.0F, 0, 0.0F);
    bars.enable(1, true, std::vector<std::string>{"Other"}, 0, false, 0, 0);
    CHECK(bars.bar(0).label == "Car");
    CHECK(bars.bar(0).fill == 0.5F);
    // Clamped fills; the gradient's ends and middle.
    bars.setPercentage(1, 3.0F, 1.0F, 0, 0.0F);
    CHECK(bars.bar(0).fill == 1.0F);
    CHECK(ScriptedBars::gradientColour(1.0F) == coney::graphics::Rgba{115, 183, 11, 255});
    CHECK(ScriptedBars::gradientColour(0.0F) == coney::graphics::Rgba{255, 16, 16, 255});
    CHECK(ScriptedBars::gradientColour(0.5F) == coney::graphics::Rgba{185, 100, 14, 255});
    bars.enable(1, false, {}, 0, false, 0, 0);
    CHECK_FALSE(bars.bar(0).exists);
}

TEST_CASE("HUDEnableBar kind 3 makes a stack of labelled bars, each shown or hidden by its number", "[hud]") {
    ScriptedBars bars;
    const std::vector<std::string> labels{"Diego", "Vargas"};
    bars.enable(3, true, labels, 2, false, 0, 0);
    CHECK(bars.bar(0).label == "Diego");
    CHECK(bars.bar(1).label == "Vargas");
    CHECK_FALSE(bars.bar(2).exists);
    CHECK_FALSE(bars.bar(0).gradient);
    bars.setPercentage(3, 0.25F, 1.0F, 1, 0.0F);
    CHECK(bars.bar(1).fill == 0.25F);
    // Hide bar 0: bar 1 moves up by 0.07 from its place at 0.26 + 0.08.
    bars.enable(3, false, {}, 0, true, 0, 0);
    CHECK_FALSE(bars.bar(0).shown);
    Canvas c;
    bars.render(c.canvas, 0.0F);
    REQUIRE_FALSE(c.parts.sprites().empty());
    for (const coney::graphics::Sprite& sprite : c.parts.sprites()) {
        CHECK(guiY(sprite) == Approx(0.26F + 0.08F - 0.07F).margin(1e-4F));
    }
    // A property call recolours a bar and turns its gradient off or on.
    bars.setProperty(1, false, coney::graphics::Rgba{1, 2, 3, 4}, 0.3F);
    CHECK(bars.bar(1).colour == coney::graphics::Rgba{1, 2, 3, 4});
    CHECK(bars.bar(1).width == 0.3F);
    bars.enable(3, false, {}, 0, false, 0, 0);
    CHECK_FALSE(bars.bar(1).exists);
}

TEST_CASE("the bars sit at the right column's end, below the counter panels' shift", "[hud]") {
    ScriptedBars bars;
    bars.enable(1, true, {}, 0, false, 0, 0);
    Canvas c;
    bars.render(c.canvas, 0.16F);
    REQUIRE(c.parts.sprites().size() == 4);
    float right = -10.0F;
    for (const coney::graphics::Sprite& sprite : c.parts.sprites()) {
        CHECK(guiY(sprite) == Approx(0.26F + 0.16F).margin(1e-4F));
        right = std::max(right, sprite.position.x + (sprite.width / 2.0F));
    }
    // The bar's right end at GUI x 1.0.
    CHECK(right == Approx(coney::graphics::OverlayCamera::guiToOverlay(1.0F, 0.0F).x).margin(1e-4F));
}

TEST_CASE("the chase gauge's marker runs from the left end at max to the right end at min", "[hud]") {
    ScriptedBars bars;
    bars.enable(2, true, {}, 0, false, 0x1a0004, 0x1a0000);
    REQUIRE(bars.gauge().exists);
    CHECK(bars.gauge().max == 60.0F);
    CHECK(bars.gauge().value == 30.0F);
    CHECK(bars.gaugePosition() == Approx(0.5F));
    bars.setPercentage(2, 10.0F, 10.0F, 0, 0.0F);
    CHECK(bars.gaugePosition() == Approx(0.0F));
    bars.setPercentage(2, 0.0F, 10.0F, 0, 0.0F);
    CHECK(bars.gaugePosition() == Approx(1.0F));
    Canvas c;
    bars.render(c.canvas, 0.0F);
    // Track, marker (red at the end), icon.
    REQUIRE(c.chasebar.sprites().size() == 3);
    CHECK(c.chasebar.sprites()[1].colour == coney::graphics::Rgba{255, 16, 16, 255});
    bars.enable(2, false, {}, 0, false, 0, 0);
    CHECK_FALSE(bars.gauge().exists);
}
