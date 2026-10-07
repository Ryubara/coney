// SPDX-License-Identifier: GPL-3.0-or-later
#include "hud/tag_hud.h"

#include <cstddef>
#include <memory>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "graphics/overlay_camera.h"
#include "support/recording_device.h"

using Catch::Approx;
using coney::graphics::Rgba;
using coney::graphics::SpriteBatch;
using coney::hud::TagHud;
using coney::hud::TagPanelCell;
using coney::hud::TagPanelState;

namespace {

// A part_page0 stand-in: 100 equal rectangles over a small fake texture.
coney::graphics::SpriteSheet fakeSheet() {
    coney::graphics::SpriteSheet sheet;
    sheet.texture = std::make_shared<coney::test::FakeTexture>(64, 64);
    for (int i = 0; i < 100; ++i) {
        sheet.page.rects.push_back(coney::graphics::UvRect{0.0F, 0.0F, 0.25F, 0.25F});
    }
    return sheet;
}

// A game with a 10-point path, two painted cells, the cursor at (20.5, 30), half a charge left, red paint.
TagPanelState game() {
    TagPanelState state;
    for (int i = 0; i < 10; ++i) {
        state.path.push_back(TagPanelCell{i * 10, i * 5});
    }
    state.painted = {TagPanelCell{0, 0}, TagPanelCell{10, 5}};
    state.cursorX = 20.5F;
    state.cursorY = 30.0F;
    state.chargeLeft = 0.5F;
    state.colour = Rgba{200, 20, 20, 255};
    return state;
}

} // namespace

TEST_CASE("the tag panel's cursor fades white to black and back over 500 ms", "[hud][tag]") {
    CHECK(TagHud::cursorColour(0, false) == Rgba{255, 255, 255, 255});
    CHECK(TagHud::cursorColour(250, false) == Rgba{0, 0, 0, 255});
    CHECK(TagHud::cursorColour(125, false).r == 128);
    CHECK(TagHud::cursorColour(375, false).r == 128);
    CHECK(TagHud::cursorColour(500, false) == Rgba{255, 255, 255, 255});
    // White while paused.
    CHECK(TagHud::cursorColour(250, true) == Rgba{255, 255, 255, 255});
}

TEST_CASE("the tag panel's origin is the GUI point in overlay space", "[hud][tag]") {
    // Player 0: ((0.01 - 0.5) x 640 / 448, 0.5 - 0.70) = (-0.7, -0.2).
    CHECK(TagHud::origin(0).x == Approx(-0.7F));
    CHECK(TagHud::origin(0).y == Approx(-0.2F));
    CHECK(TagHud::origin(1).x == Approx((0.76F - 0.5F) * 640.0F / 448.0F));
}

TEST_CASE("a hidden tag panel draws nothing", "[hud][tag]") {
    SpriteBatch parts(fakeSheet(), 512, 10000.0F);
    coney::hud::HudCanvas canvas;
    canvas.parts = &parts;
    TagHud panel;
    panel.render(canvas, 0, 0);
    CHECK(parts.sprites().empty());
    panel.set(game());
    panel.set(std::nullopt);
    panel.render(canvas, 0, 0);
    CHECK(parts.sprites().empty());
}

TEST_CASE("the tag panel draws the path, the painted cells, the cursor, the charge bar and the frame", "[hud][tag]") {
    SpriteBatch parts(fakeSheet(), 512, 10000.0F);
    coney::hud::HudCanvas canvas;
    canvas.parts = &parts;
    TagHud panel;
    panel.set(game());
    panel.render(canvas, 0, 0);
    // 7 path dots (the last 3 left out), 2 cells, the cursor, the bar, the frame.
    const auto& sprites = parts.sprites();
    REQUIRE(sprites.size() == 7 + 2 + 3);
    const coney::graphics::OverlayPoint origin = TagHud::origin(0);
    // The path: dot 1 at cell (10, 5), grey 100 + 155 x 1 / 10 = 115, 0.015 square; larger cells draw higher.
    CHECK(sprites[1].position.x == Approx(origin.x + 0.025F));
    CHECK(sprites[1].position.y == Approx(origin.y + 0.0125F));
    CHECK(sprites[1].position.z == Approx(coney::graphics::OverlayCamera::kGuiDepth));
    CHECK(sprites[1].colour == Rgba{115, 115, 115, 255});
    CHECK(sprites[1].width == Approx(0.015F));
    CHECK(sprites[0].colour.r == 100);
    // The painted cells in the paint's colour at alpha 205.
    CHECK(sprites[8].colour == Rgba{200, 20, 20, 205});
    CHECK(sprites[8].width == Approx(0.03F));
    // The cursor between cells.
    CHECK(sprites[9].position.x == Approx(origin.x + (20.5F * 0.0025F)));
    CHECK(sprites[9].width == Approx(0.04F));
    // The bar: half the full height, its bottom at origin + 0.045, so its centre a quarter of 0.045 above.
    CHECK(sprites[10].height == Approx(0.0225F));
    CHECK(sprites[10].width == Approx(0.0255F));
    CHECK(sprites[10].position.x == Approx(origin.x + 0.0083F));
    CHECK(sprites[10].position.y - (sprites[10].height / 2.0F) == Approx(origin.y + 0.045F));
    CHECK(sprites[10].colour == Rgba{200, 20, 20, 255});
    // The frame, alpha 191, last.
    CHECK(sprites[11].colour == Rgba{191, 191, 191, 191});
    CHECK(sprites[11].position.y == Approx(origin.y + 0.07F));
}
