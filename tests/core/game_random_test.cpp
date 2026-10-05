// SPDX-License-Identifier: GPL-3.0-or-later
// The game's random numbers (docs/research/flags.md#player-starts): a table walked by one index that wraps at 1,024,
// and `low + v mod (high - low + 1)` with an unsigned modulo. The table here is a synthetic one; the game's is read
// from the player's disc at run time.
#include "core/game_random.h"

#include <cstdint>
#include <vector>

#include <catch2/catch_test_macros.hpp>

using coney::GameRandom;

namespace {

// A synthetic table: entry i holds i * 7 + 3.
std::vector<std::uint32_t> syntheticTable() {
    std::vector<std::uint32_t> table(GameRandom::kTableSize);
    for (std::uint32_t i = 0; i < table.size(); ++i) {
        table[i] = (i * 7U) + 3U;
    }
    return table;
}

} // namespace

TEST_CASE("a draw advances the shared index and reads that entry, wrapping at 1,024", "[game_random]") {
    GameRandom random;
    const std::vector<std::uint32_t> table = syntheticTable();
    random.setTable(table);
    CHECK(random.hasTable());
    CHECK(random.next() == table[1]);
    CHECK(random.next() == table[2]);
    for (int i = 0; i < 1021; ++i) {
        (void)random.next();
    }
    CHECK(random.index() == 1023);
    CHECK(random.next() == table[0]);
    CHECK(random.index() == 0);
}

TEST_CASE("random(low, high) is low plus the draw modulo the range, inclusive", "[game_random]") {
    GameRandom random;
    random.setTable(syntheticTable());
    // Entries 1, 2, 3 are 10, 17, 24: 1 + 10 mod 5 = 1, 1 + 17 mod 5 = 3, 1 + 24 mod 5 = 5.
    CHECK(random.range(1, 5) == 1);
    CHECK(random.range(1, 5) == 3);
    CHECK(random.range(1, 5) == 5);
    // A range of one number, and a range of none (high one below low), which the original divides by: low.
    CHECK(random.range(4, 4) == 4);
    CHECK(random.range(4, 3) == 4);
    // Negative bounds go through the unsigned arithmetic and stay in range: entry 6 is 45, -2 + 45 mod 5 = -2.
    CHECK(random.range(-2, 2) == -2);
}

TEST_CASE("without the game's table the stand-in draws deterministically and in range", "[game_random]") {
    GameRandom first;
    GameRandom second;
    CHECK(!first.hasTable());
    for (int i = 0; i < 200; ++i) {
        const std::int32_t value = first.range(1, 5);
        CHECK(value == second.range(1, 5));
        CHECK(value >= 1);
        CHECK(value <= 5);
    }
}
