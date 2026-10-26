// SPDX-License-Identifier: GPL-3.0-or-later
// The Armies of the Night HUD parts (docs/research/armies.md, docs/research/hud-functions-parts.md#fn-pre-basewidget):
// the levels that have them, the credits, the GO sign's frames by mode and the join messages' blink.
#include "hud/armies_hud.h"

#include <algorithm>
#include <cstdint>
#include <string>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "graphics/font.h"
#include "graphics/overlay_camera.h"
#include "graphics/sprite_batch.h"
#include "hud/hud_canvas.h"
#include "support/font_fixtures.h"

using coney::hud::ArmiesHud;
using coney::hud::GoSignMode;
using coney::hud::isArmiesLevel;

TEST_CASE("each level's set-up clears the Armies credits, which the stage's script sets again", "[hud][armies]") {
    // Expected: hud-functions-parts.md#fn-pre-basewidget, confirmed (code): HUD_InitLevel zeroes HUD +0x190d0, the
    // word 0x00619910 HUDANSetCredit writes.
    ArmiesHud armies;
    armies.setCredits(7);
    CHECK(armies.credits() == 7);
    armies.levelSetUp();
    CHECK(armies.credits() == 0);
}

TEST_CASE("the credits line sits at x 0.71 in the default video mode", "[hud][armies]") {
    // Expected: hud-functions-parts.md#fn-pre-basewidget, confirmed (code): HUD_Update sets 0x0050d4bc to 0.75, then
    // to 0.71 when the display flags 2, 4 and 8 are clear; y 0.92.
    CHECK(ArmiesHud::kCreditsPlace.x == 0.71F);
    CHECK(ArmiesHud::kCreditsPlace.y == 0.92F);
}

TEST_CASE("the GO sign sits 0.4 further left in Spanish and 0.02 in Italian", "[hud][armies]") {
    // Expected: hud-functions-parts.md#fn-pre-basewidget, confirmed (code): GoSign_Setup's shift 0x0050cf98 by the
    // game state's language +0x120.
    ArmiesHud armies;
    armies.levelSetUp(coney::Language::Spanish);
    CHECK(armies.goSignShift() == 0.4F);
    armies.levelSetUp(coney::Language::Italian);
    CHECK(armies.goSignShift() == 0.02F);
    armies.levelSetUp(coney::Language::French);
    CHECK(armies.goSignShift() == 0.0F);
    armies.levelSetUp(coney::Language::English);
    CHECK(armies.goSignShift() == 0.0F);
}

TEST_CASE("each level's set-up turns both join messages on, and a panel's first attach turns its own off",
          "[hud][armies]") {
    // Expected: armies.md#the-join-messages, confirmed (code) and (runtime): HUD_InitLevel calls each new ANHud's
    // slot +0x80 (0x004ec650), which sets +0x4614 to 1; ANHud_Init (0x002107e8), from HUD_AttachPlayer on the
    // panel's first attach, sets it to 0. In level60 panel 2 keeps it: "PRESS THE START BUTTON TO JOIN".
    ArmiesHud armies;
    armies.levelSetUp();
    CHECK(armies.joinMessage(0));
    CHECK(armies.joinMessage(1));
    armies.panelAttached(0);
    CHECK_FALSE(armies.joinMessage(0));
    CHECK(armies.joinMessage(1));
    armies.update(0, 60, {true, false});
    CHECK(armies.joinShowing(1));
}

TEST_CASE("a join message is centred on its panel's place, as Font_Draw's flag 2 centres each run", "[hud][armies]") {
    // Expected: gui.md (Font_Draw 0x00179c30: flag 0x02 moves the start left by half the measured width);
    // ANHud_Update sets the join text's alignment to 2 at the panel's place, (0.76, 0.08) for player 2. Confirmed
    // (runtime) in level60: the original's two lines span about 0.61 to 0.86 of the width, centred near 0.73.
    coney::graphics::Font font = coney::test::testFont();
    coney::graphics::SpriteBatch batch{font.sheet(), 8192, 9000.0F};
    coney::hud::HudCanvas canvas;
    canvas.text.fonts = [&font](int /*slot*/) { return &font; };
    canvas.text.textBatch = [&batch](int /*slot*/) { return &batch; };
    ArmiesHud armies;
    armies.levelSetUp();
    armies.panelAttached(0);
    armies.update(0, 60, {true, false});
    REQUIRE(armies.joinShowing(1));
    armies.render(canvas,
                  [](std::uint32_t id) { return id == ArmiesHud::kJoinString ? std::string{"JOIN"} : std::string{}; });
    REQUIRE_FALSE(batch.sprites().empty());
    float left = batch.sprites().front().position.x;
    float right = left;
    for (const coney::graphics::Sprite& sprite : batch.sprites()) {
        left = std::min(left, sprite.position.x - sprite.width / 2.0F);
        right = std::max(right, sprite.position.x + sprite.width / 2.0F);
    }
    const float centre = coney::graphics::OverlayCamera::guiToOverlay(0.76F, 0.08F).x;
    CHECK((left + right) / 2.0F == Catch::Approx(centre).margin(0.01));
}
