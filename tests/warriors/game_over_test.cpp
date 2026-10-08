// SPDX-License-Identifier: GPL-3.0-or-later
// The game-over check and its hand-off (docs/research/combat.md#defeat): the cases in their order, the wait for help
// with its route checks, and the 180-update countdown to the mission-failed screen.
#include "warriors/game_over.h"

#include <array>

#include <catch2/catch_test_macros.hpp>

using coney::GameOverCheck;
using coney::GameOverPlayer;
using coney::GameOverReason;

namespace {

// The check's update with player 1 only.
std::optional<GameOverReason> once(GameOverCheck& check, const GameOverPlayer& player, bool noneAble,
                                   bool canReach = true) {
    const std::array<GameOverPlayer, 1> players{player};
    return check.update(players, noneAble, [canReach] { return canReach; });
}

} // namespace

TEST_CASE("the mission fails at once for a dead player, and never while a player is in the fight", "[game-over]") {
    GameOverCheck check;
    CHECK_FALSE(once(check, GameOverPlayer{}, true).has_value());
    CHECK(once(check, GameOverPlayer{.dead = true}, false) == GameOverReason::Beaten);
    CHECK(check.failed());
    // Once failed, nothing more is decided.
    CHECK_FALSE(once(check, GameOverPlayer{.dead = true}, false).has_value());
    check.reset();
    CHECK_FALSE(check.failed());
    CHECK(check.enabled());
}

TEST_CASE("the check does nothing while switched off", "[game-over]") {
    GameOverCheck check;
    check.setEnabled(false);
    CHECK_FALSE(once(check, GameOverPlayer{.dead = true}, true).has_value());
    check.setEnabled(true);
    CHECK(once(check, GameOverPlayer{.knockedOut = true}, true) == GameOverReason::Beaten);
}

TEST_CASE("a knocked-out player without a flash fails at once; with no one to help, any downed player fails",
          "[game-over]") {
    GameOverCheck check;
    CHECK(once(check, GameOverPlayer{.knockedOut = true}, false) == GameOverReason::Beaten);
    check.reset();
    CHECK(once(check, GameOverPlayer{.cuffed = true}, true) == GameOverReason::Busted);
    check.reset();
    // A cuffed player who can free himself is spared, even with no one to help.
    CHECK_FALSE(once(check, GameOverPlayer{.cuffed = true, .canFreeSelf = true}, true).has_value());
}

TEST_CASE("with a flash and help near the failure waits; the 4th failed route check in a row fails it", "[game-over]") {
    GameOverCheck check;
    const GameOverPlayer out{.knockedOut = true, .holdsFlash = true};
    // A member who can reach him: waits for good.
    for (int i = 0; i < 400; ++i) {
        CHECK_FALSE(once(check, out, false, true).has_value());
    }
    // No route: every 37th update counts a no; the 4th fails.
    check.reset();
    int updates = 0;
    std::optional<GameOverReason> result;
    while (!result && updates < 1000) {
        ++updates;
        result = once(check, out, false, false);
    }
    CHECK(result == GameOverReason::Beaten);
    CHECK(updates == 4 * GameOverCheck::kRouteCheckUpdates);
    // The wait is blocked by the game state's +0x414: no waiting.
    check.reset();
    CHECK(once(check, GameOverPlayer{.knockedOut = true, .holdsFlash = true, .waitBlocked = true}, false) ==
          GameOverReason::Beaten);
}

TEST_CASE("the mission-failed screen comes 180 updates after the failure, 10 once cross is pressed", "[game-over]") {
    GameOverCheck check;
    CHECK_FALSE(check.stepHandOff(false)); // nothing before a failure
    REQUIRE(once(check, GameOverPlayer{.knockedOut = true}, false).has_value());
    int updates = 1;
    while (!check.stepHandOff(false)) {
        ++updates;
    }
    CHECK(updates == GameOverCheck::kHandOffUpdates);
    CHECK_FALSE(check.stepHandOff(false)); // once
    check.reset();
    REQUIRE(once(check, GameOverPlayer{.knockedOut = true}, false).has_value());
    for (int i = 0; i < 20; ++i) {
        CHECK_FALSE(check.stepHandOff(false));
    }
    updates = 1;
    while (!check.stepHandOff(updates == 1)) {
        ++updates;
    }
    CHECK(updates == GameOverCheck::kHandOffCut);
}
