// SPDX-License-Identifier: GPL-3.0-or-later
// The game-over check's rules as the original's code has them (docs/research/combat-reactions.md#mission-failing): a
// dead player's reason, the route check's phase on the game state's update counter, and the hand-off's Armies cap, HUD
// and fall cut, and what the check and the route test read of a human. Synthetic players and humans.
#include <array>
#include <optional>

#include <catch2/catch_test_macros.hpp>

#include "human/human.h"
#include "support/ai_fixtures.h"
#include "warriors/game_over.h"

using coney::GameOverCheck;
using coney::GameOverPlayer;
using coney::GameOverReason;

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

TEST_CASE("a story failure's hand-off hides the HUD; an Armies level's keeps it", "[game-over]") {
    // Expected: combat-reactions.md#mission-failing, confirmed (code): Gm_Level_Update hides the HUD only in the story
    // failure's death camera start, which an Armies level skips.
    GameOverCheck check;
    CHECK(check.handOffHidesHud());
    check.setArmiesLevel(true);
    CHECK_FALSE(check.handOffHidesHud());
}

TEST_CASE("player 1 counts as knocked out once his knock-out state is set, not when his health runs out",
          "[game-over]") {
    // Expected: combat-reactions.md#mission-failing, confirmed (code): GameState_CheckGameOver reads Human_IsKnockedOut
    // (state 0x40000), which a player gets at his dying clip's end, not at 0 health.
    coney::test::AiScene scene;
    coney::human::Human& player = scene.add({40.0F, 40.0F, 0.0F}, 0.0F).human();
    CHECK_FALSE(coney::gameOverPlayer(player, false).knockedOut);
    player.fighter().health().set(0);
    CHECK_FALSE(coney::gameOverPlayer(player, false).knockedOut);
    player.script().knockedOut = true;
    CHECK(coney::gameOverPlayer(player, true).knockedOut);
    CHECK(coney::gameOverPlayer(player, true).holdsFlash);
    player.setArrested(true);
    CHECK(coney::gameOverPlayer(player, false).cuffed);
}

TEST_CASE("the route test asks every member not cuffed and not down or dead, and skips a wounded one", "[game-over]") {
    // Expected: combat-reactions.md#defeat-helpers, confirmed (code): Gang_CanReachToHelp skips a member Human_IsCuffed
    // or Human_IsDownOrDead reports (states 0x180050000, wounded 0x10000 among them).
    coney::test::AiScene scene;
    coney::human::Human& mate = scene.add({40.0F, 40.0F, 0.0F}, 0.0F).human();
    CHECK(coney::asksForRoute(mate));
    mate.setWounded(true);
    CHECK_FALSE(coney::asksForRoute(mate));
    mate.setWounded(false);
    mate.setArrested(true);
    CHECK_FALSE(coney::asksForRoute(mate));
    mate.setArrested(false);
    mate.script().knockedOut = true;
    CHECK_FALSE(coney::asksForRoute(mate));
}
