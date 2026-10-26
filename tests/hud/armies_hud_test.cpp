// SPDX-License-Identifier: GPL-3.0-or-later
// The Armies of the Night HUD parts (docs/research/armies.md, docs/research/hud-functions-parts.md#fn-pre-basewidget):
// the levels that have them, the credits, the GO sign's frames by mode and the join messages' blink.
#include "hud/armies_hud.h"

#include <cstdint>

#include <catch2/catch_test_macros.hpp>

using coney::hud::ArmiesHud;
using coney::hud::GoSignMode;
using coney::hud::isArmiesLevel;

TEST_CASE("the Armies of the Night levels are 60 to 69, as the original's test takes them", "[hud][armies]") {
    CHECK_FALSE(isArmiesLevel(59));
    CHECK(isArmiesLevel(60));
    CHECK(isArmiesLevel(64));
    CHECK(isArmiesLevel(69));
    CHECK_FALSE(isArmiesLevel(70));
    CHECK_FALSE(isArmiesLevel(99));
}

TEST_CASE("the GO sign starts off, blinks its one arrow in mode 0 and lights three in turn in mode 1, every 300 ms",
          "[hud][armies]") {
    ArmiesHud armies;
    armies.levelSetUp();
    CHECK(armies.goSignMode() == static_cast<std::uint8_t>(GoSignMode::Off));

    armies.setGoSignMode(static_cast<std::uint8_t>(GoSignMode::Text));
    armies.update(1000, 60, {true, false});
    CHECK(armies.active());
    CHECK(armies.goSignFrame() == 0);
    // Not yet past 300 ms: the frame stays; past it, it steps and wraps at 2.
    armies.update(1300, 60, {true, false});
    CHECK(armies.goSignFrame() == 0);
    armies.update(1301, 60, {true, false});
    CHECK(armies.goSignFrame() == 1);
    armies.update(1602, 60, {true, false});
    CHECK(armies.goSignFrame() == 0);

    armies.setGoSignMode(static_cast<std::uint8_t>(GoSignMode::Arrows));
    std::uint64_t now = 1602;
    for (std::uint8_t frame = 1; frame <= 4; ++frame) {
        now += 301;
        armies.update(now, 60, {true, false});
        CHECK(armies.goSignFrame() == frame % 4);
    }

    // A story level has none of it.
    armies.levelSetUp();
    armies.update(now, 99, {true, false});
    CHECK_FALSE(armies.active());
}

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

TEST_CASE("a join message shows for a player who is not in, blinking every 500 ms", "[hud][armies]") {
    ArmiesHud armies;
    armies.levelSetUp();
    armies.setJoinMessage(0, true);
    armies.setJoinMessage(1, true);
    armies.update(0, 60, {true, false});
    // Player 1 is in: his message never shows; player 2's does, on and off.
    CHECK_FALSE(armies.joinShowing(0));
    CHECK(armies.joinShowing(1));
    armies.update(501, 60, {true, false});
    CHECK_FALSE(armies.joinShowing(1));
    armies.update(1002, 60, {true, false});
    CHECK(armies.joinShowing(1));
    armies.setJoinMessage(1, false);
    CHECK_FALSE(armies.joinShowing(1));
}
