// SPDX-License-Identifier: GPL-3.0-or-later
#include "gamemodes/sheet_viewer_mode.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "core/game_timer.h"
#include "gamemodes/game_mode_stack.h"
#include "support/recording_device.h"

using coney::SheetViewerMode;
using coney::graphics::SpriteSheet;
using coney::graphics::UvRect;
using coney::test::FakeTexture;
using coney::test::RecordingDevice;

TEST_CASE("the sheet viewer draws every rectangle as a sprite inside the logical screen", "[sheet_viewer]") {
    auto texture = std::make_shared<FakeTexture>(128, 64);
    SpriteSheet sheet;
    sheet.page.rects = {UvRect{0.0F, 0.0F, 0.5F, 1.0F}, UvRect{0.5F, 0.0F, 1.0F, 0.5F}, UvRect{0.5F, 0.5F, 1.0F, 1.0F}};
    sheet.texture = texture;
    RecordingDevice device;
    SheetViewerMode viewer(device, sheet);
    coney::GameModeStack stack;
    stack.push(viewer);
    coney::GameTimer timer;
    CHECK(stack.runUntilEmpty(timer, {}, std::uint64_t{2}) == 2);

    CHECK(device.calls == std::vector<std::string>{"begin", "draw", "present", "begin", "draw", "present"});
    REQUIRE(device.draws.size() == 2);
    const coney::test::RecordedDraw& draw = device.draws[1]; // the batch was emptied and refilled
    CHECK(draw.texture == texture.get());
    REQUIRE(draw.quads.size() == 3);
    for (std::size_t i = 0; i < draw.quads.size(); ++i) {
        const coney::graphics::LogicalQuad& quad = draw.quads[i];
        CHECK(quad.uv == sheet.page.rect(i));
        CHECK(quad.x >= 0.0F);
        CHECK(quad.y >= 0.0F);
        CHECK(quad.x + quad.width <= 640.5F);
        CHECK(quad.y + quad.height <= 448.5F);
        CHECK(quad.width > 0.0F);
    }
    // The first rectangle is 64 x 64 texels and the second 64 x 32: their shapes survive the layout.
    CHECK(draw.quads[0].width / draw.quads[0].height > 0.99F);
    CHECK(draw.quads[0].width / draw.quads[0].height < 1.01F);
    CHECK(draw.quads[1].width / draw.quads[1].height > 1.99F);
}
