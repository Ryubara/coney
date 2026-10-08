// SPDX-License-Identifier: GPL-3.0-or-later
#include "warriors/created_humans.h"

#include <string>

#include <catch2/catch_test_macros.hpp>

namespace {

// A human the scripts made: `name` with `handle`, for player `player` (0 for an AI human).
coney::HumanCreation made(std::string name, double handle, int player) {
    coney::HumanCreation human;
    human.name = std::move(name);
    human.handle = handle;
    human.playerIndex = player;
    return human;
}

} // namespace

TEST_CASE("player 1 is his first creation until the player is handed to another human", "[warriors][humans]") {
    coney::CreatedHumans humans;
    REQUIRE(humans.add(made("Rembrandt", 181, 1)));
    REQUIRE(humans.add(made("Ash", 182, 2)));
    // Checkpoint 2's new Rembrandt, made for player 1 while the first one still is.
    REQUIRE(humans.add(made("Rembrandt", 299, 1)));
    REQUIRE(humans.player(1) != nullptr);
    CHECK(humans.player(1)->handle == 181);

    // HuChangePlayerGang's hand-over: from now on player 1 is the new human.
    humans.setPlayer(1, 299);
    REQUIRE(humans.player(1) != nullptr);
    CHECK(humans.player(1)->handle == 299);
    CHECK(humans.player(2)->handle == 182);

    // A handle no human has changes nothing; a new level forgets the hand-over.
    humans.setPlayer(1, 999);
    CHECK(humans.player(1)->handle == 299);
    humans.clear();
    REQUIRE(humans.add(made("Cleon", 5, 1)));
    CHECK(humans.player(1)->handle == 5);
}
