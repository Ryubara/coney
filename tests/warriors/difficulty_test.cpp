// SPDX-License-Identifier: GPL-3.0-or-later
// The difficulty in force (W_GameState +0x154) and the rules of the code that reads it
// (docs/research/difficulty.md).
#include <cstdint>

#include <catch2/catch_test_macros.hpp>

#include "warriors/difficulty.h"
#include "warriors/game_state.h"

namespace difficulty = coney::difficulty;

TEST_CASE("a fresh game state plays on normal", "[difficulty]") {
    // Expected: difficulty.md#setting, confirmed (code)
    const coney::GameState state;
    CHECK(state.difficulty == difficulty::kNormal);
    CHECK(difficulty::kEasy == 0);
    CHECK(difficulty::kNormal == 1);
    CHECK(difficulty::kHard == 2);
    CHECK(difficulty::kFury == 3);
    CHECK(difficulty::kRumble == 4);
}

TEST_CASE("a flash heals a player in full up to hard, and by half the maximum on fury and rumble", "[difficulty]") {
    // Expected: difficulty.md#flash, confirmed (code)
    constexpr int kMaximum = 151;
    constexpr int kHurt = 30;
    for (const std::uint8_t level : {difficulty::kEasy, difficulty::kNormal, difficulty::kHard}) {
        INFO(static_cast<int>(level));
        CHECK(difficulty::flashHealth(kHurt, kMaximum, true, level) == kMaximum);
    }
    // Half the maximum rounded down (151 >> 1 = 75) is added.
    for (const std::uint8_t level : {difficulty::kFury, difficulty::kRumble, std::uint8_t{200}}) {
        INFO(static_cast<int>(level));
        CHECK(difficulty::flashHealth(kHurt, kMaximum, true, level) == kHurt + 75);
    }
    // Capped at the maximum.
    CHECK(difficulty::flashHealth(100, kMaximum, true, difficulty::kFury) == kMaximum);
    CHECK(difficulty::flashHealth(kMaximum - 75, kMaximum, true, difficulty::kFury) == kMaximum);
}

TEST_CASE("a flash heals a human who is no player in full at every difficulty", "[difficulty]") {
    // Expected: difficulty.md#flash, confirmed (code)
    for (const std::uint8_t level :
         {difficulty::kEasy, difficulty::kNormal, difficulty::kHard, difficulty::kFury, difficulty::kRumble}) {
        INFO(static_cast<int>(level));
        CHECK(difficulty::flashHealth(10, 120, false, level) == 120);
    }
}
