// SPDX-License-Identifier: GPL-3.0-or-later
// The game-over check's rules as the original's code has them (docs/research/combat-reactions.md#mission-failing): a
// dead player's reason, the gang test that counts the downed player himself, the route check's phase on the game
// state's update counter, and the hand-off's Armies cap and fall cut. Synthetic players.
#include <array>
#include <optional>

#include <catch2/catch_test_macros.hpp>

#include "warriors/game_over.h"

using coney::GameOverCheck;
using coney::GameOverPlayer;
using coney::GameOverReason;
using coney::GangHelper;

namespace {

// The check's update with player 1 only.
std::optional<GameOverReason> step(GameOverCheck& check, const GameOverPlayer& player, bool noneAble, bool canReach) {
    const std::array<GameOverPlayer, 1> players{player};
    return check.update(players, noneAble, [canReach] { return canReach; });
}

// Updates of the hand-off until it opens the failure's screen, counting the first.
int handOffUpdates(GameOverCheck& check, bool fellOut) {
    int updates = 1;
    while (!check.stepHandOff(false, fellOut) && updates < 1000) {
        ++updates;
    }
    return updates;
}

} // namespace

TEST_CASE("a dead player fails the mission as beaten, even when player 1 is cuffed", "[game-over]") {
    // Expected: combat-reactions.md#mission-failing, confirmed (code)
    GameOverCheck check;
    CHECK(step(check, GameOverPlayer{.dead = true, .cuffed = true}, false, true) == GameOverReason::Beaten);
}

TEST_CASE("the gang test counts the downed player himself: alone in his gang he has no one to help", "[game-over]") {
    // Expected: combat-reactions.md#defeat-helpers, confirmed (code)
    // Player 1 first, as the check lists his gang; he is down (out or cuffed) whenever the test runs.
    const std::array<GangHelper, 1> alone{GangHelper{.knockedOut = true}};
    CHECK(coney::noneAbleToHelp(alone));
    const std::array<GangHelper, 2> withFree{GangHelper{.cuffed = true}, GangHelper{}};
    CHECK_FALSE(coney::noneAbleToHelp(withFree));
    const std::array<GangHelper, 3> allDown{GangHelper{.knockedOut = true}, GangHelper{.cuffed = true},
                                            GangHelper{.held = true}};
    CHECK(coney::noneAbleToHelp(allDown));
    // An empty gang is not "none able": the check goes on to the route test.
    CHECK_FALSE(coney::noneAbleToHelp({}));
}

TEST_CASE("the route check runs on every 37th update of the game state, not 37 after the wait began", "[game-over]") {
    // Expected: combat-reactions.md#mission-failing, confirmed (code)
    GameOverCheck check;
    const GameOverPlayer out{.knockedOut = true, .holdsFlash = true};
    // Ten updates in the fight, then down with a flash and no route: the nos come at updates 37, 74, 111 and 148.
    int updates = 0;
    for (; updates < 10; ++updates) {
        CHECK_FALSE(step(check, GameOverPlayer{}, false, false).has_value());
    }
    std::optional<GameOverReason> result;
    while (!result && updates < 1000) {
        ++updates;
        result = step(check, out, false, false);
    }
    CHECK(result == GameOverReason::Beaten);
    CHECK(updates == 4 * GameOverCheck::kRouteCheckUpdates);
}

TEST_CASE("getting up keeps the route check's count of nos", "[game-over]") {
    // Expected: combat-reactions.md#mission-failing, confirmed (code)
    GameOverCheck check;
    const GameOverPlayer out{.knockedOut = true, .holdsFlash = true};
    int updates = 0;
    // Two nos (updates 37 and 74), up for the next 30 updates, then down again: the 3rd and 4th nos at 111 and 148.
    for (; updates < 80; ++updates) {
        CHECK_FALSE(step(check, out, false, false).has_value());
    }
    for (; updates < 110; ++updates) {
        CHECK_FALSE(step(check, GameOverPlayer{}, false, false).has_value());
    }
    std::optional<GameOverReason> result;
    while (!result && updates < 1000) {
        ++updates;
        result = step(check, out, false, false);
    }
    CHECK(result == GameOverReason::Beaten);
    CHECK(updates == 4 * GameOverCheck::kRouteCheckUpdates);
}

TEST_CASE("in an Armies level the hand-off's countdown is capped at 90 updates", "[game-over]") {
    // Expected: combat-reactions.md#mission-failing, confirmed (code)
    GameOverCheck check;
    check.setArmiesLevel(true);
    REQUIRE(step(check, GameOverPlayer{.knockedOut = true}, false, true).has_value());
    CHECK(handOffUpdates(check, false) == 90);
    // A story level keeps the full 180.
    check.reset();
    check.setArmiesLevel(false);
    REQUIRE(step(check, GameOverPlayer{.knockedOut = true}, false, true).has_value());
    CHECK(handOffUpdates(check, false) == GameOverCheck::kHandOffUpdates);
}

TEST_CASE("a player fallen out of the world ends the hand-off's countdown at once", "[game-over]") {
    // Expected: combat-reactions.md#mission-failing, confirmed (code)
    GameOverCheck check;
    REQUIRE(step(check, GameOverPlayer{.knockedOut = true}, false, true).has_value());
    CHECK(handOffUpdates(check, true) == 1);
}
