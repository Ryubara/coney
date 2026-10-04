// SPDX-License-Identifier: GPL-3.0-or-later
#include "gamemodes/text_viewer_mode.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "core/game_timer.h"
#include "gamemodes/game_mode_stack.h"
#include "support/font_fixtures.h"
#include "support/recording_device.h"

using coney::TextViewerMode;
using coney::test::RecordingDevice;

TEST_CASE("the text viewer draws one quad per glyph and shadow in the font's texture", "[text_viewer]") {
    RecordingDevice device;
    const coney::graphics::Font font = coney::test::testFont();
    TextViewerMode viewer(device, font, std::nullopt, "ab<CR><X>");
    coney::GameModeStack stack;
    stack.push(viewer);
    coney::GameTimer timer;
    timer.setFixedStep(true);
    CHECK(stack.runUntilEmpty(timer, {}, std::uint64_t{2}) == 2);

    CHECK(device.calls == std::vector<std::string>{"begin", "draw", "present", "begin", "draw", "present"});
    REQUIRE(device.draws.size() == 2);
    CHECK(device.draws[1].texture == font.sheet().texture.get());
    CHECK(device.draws[1].quads.size() == 6); // three glyphs, each with its shadow
    CHECK(viewer.layout().lines == 2);
    for (const coney::graphics::LogicalQuad& quad : device.draws[1].quads) {
        CHECK(quad.x >= 0.0F);
        CHECK(quad.y >= 0.0F);
        CHECK(quad.width > 0.0F);
    }
}
