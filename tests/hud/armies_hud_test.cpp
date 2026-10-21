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
    armies.setCredits(7);
    armies.levelSetUp();
    CHECK(armies.credits() == 7);
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

    // A story level has none of it; the credits outlive the level.
    armies.levelSetUp();
    armies.update(now, 99, {true, false});
    CHECK_FALSE(armies.active());
    CHECK(armies.credits() == 7);
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
