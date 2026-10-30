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
    CHECK(random.next(coney::RandomStream::Gameplay) == table[1]);
    CHECK(random.next(coney::RandomStream::Gameplay) == table[2]);
    for (int i = 0; i < 1021; ++i) {
        (void)random.next(coney::RandomStream::Gameplay);
    }
    CHECK(random.index(coney::RandomStream::Gameplay) == 1023);
    CHECK(random.next(coney::RandomStream::Gameplay) == table[0]);
    CHECK(random.index(coney::RandomStream::Gameplay) == 0);
}

TEST_CASE("random(low, high) is low plus the draw modulo the range, inclusive", "[game_random]") {
    GameRandom random;
    random.setTable(syntheticTable());
    // Entries 1, 2, 3 are 10, 17, 24: 1 + 10 mod 5 = 1, 1 + 17 mod 5 = 3, 1 + 24 mod 5 = 5.
    CHECK(random.range(coney::RandomStream::Gameplay, 1, 5) == 1);
    CHECK(random.range(coney::RandomStream::Gameplay, 1, 5) == 3);
    CHECK(random.range(coney::RandomStream::Gameplay, 1, 5) == 5);
    // A range of one number, and a range of none (high one below low), which the original divides by: low.
    CHECK(random.range(coney::RandomStream::Gameplay, 4, 4) == 4);
    CHECK(random.range(coney::RandomStream::Gameplay, 4, 3) == 4);
    // Negative bounds go through the unsigned arithmetic and stay in range: entry 6 is 45, -2 + 45 mod 5 = -2.
    CHECK(random.range(coney::RandomStream::Gameplay, -2, 2) == -2);
}

TEST_CASE("without the game's table the stand-in draws deterministically and in range", "[game_random]") {
    GameRandom first;
    GameRandom second;
    CHECK(!first.hasTable());
    for (int i = 0; i < 200; ++i) {
        const std::int32_t value = first.range(coney::RandomStream::Gameplay, 1, 5);
        CHECK(value == second.range(coney::RandomStream::Gameplay, 1, 5));
        CHECK(value >= 1);
        CHECK(value <= 5);
    }
}

TEST_CASE("starting the stream at an offset moves every draw along by it, the same on every run", "[game_random]") {
    // The table's index at an offset (the original's 0x006eb880 at a recording's start): the first draw reads the
    // entry after it.
    GameRandom offset;
    const std::vector<std::uint32_t> table = syntheticTable();
    offset.setTable(table);
    offset.startAt(40);
    CHECK(offset.index(coney::RandomStream::Gameplay) == 40);
    CHECK(offset.next(coney::RandomStream::Gameplay) == table[41]);
    // Masked to the table's 10 bits.
    offset.startAt(1024 + 5);
    CHECK(offset.index(coney::RandomStream::Gameplay) == 5);
    // Offset 0 is the power-on stream.
    GameRandom zero;
    zero.setTable(table);
    zero.startAt(0);
    CHECK(zero.next(coney::RandomStream::Gameplay) == table[1]);

    // The stand-in varies with the offset too, and repeats for the same one.
    GameRandom a;
    GameRandom b;
    GameRandom c;
    a.startAt(7);
    b.startAt(7);
    c.startAt(8);
    bool differs = false;
    for (int i = 0; i < 20; ++i) {
        const std::uint32_t drawn = a.next(coney::RandomStream::Gameplay);
        CHECK(drawn == b.next(coney::RandomStream::Gameplay));
        differs = differs || drawn != c.next(coney::RandomStream::Gameplay);
    }
    CHECK(differs);
}

TEST_CASE("each random stream walks its own index from 0, so two streams advance independently", "[game_random]") {
    // Expected: maths.md#random, confirmed (code)
    GameRandom random;
    const std::vector<std::uint32_t> table = syntheticTable();
    random.setTable(table);
    CHECK(random.next(coney::RandomStream::Cameras) == table[1]);
    CHECK(random.next(coney::RandomStream::Cameras) == table[2]);
    // A stream nothing has drawn from still reads entry 1 first.
    CHECK(random.next(coney::RandomStream::Sound) == table[1]);
    CHECK(random.index(coney::RandomStream::Cameras) == 2);
    CHECK(random.index(coney::RandomStream::Sound) == 1);
    CHECK(random.index(coney::RandomStream::Gameplay) == 0);
}

TEST_CASE("a draw on the effects stream leaves the gameplay stream's sequence unchanged", "[game_random]") {
    // Expected: maths.md#random, confirmed (code)
    // With the game's table and with the stand-in alike.
    for (const bool withTable : {true, false}) {
        GameRandom plain;
        GameRandom shattered;
        if (withTable) {
            plain.setTable(syntheticTable());
            shattered.setTable(syntheticTable());
        }
        for (int i = 0; i < 50; ++i) {
            // A pane of glass breaks between every two of the scripts' draws on one side only.
            static_cast<void>(shattered.next(coney::RandomStream::Effects));
            static_cast<void>(shattered.range(coney::RandomStream::Effects, 0, 7));
            CHECK(plain.range(coney::RandomStream::Gameplay, 1, 100) ==
                  shattered.range(coney::RandomStream::Gameplay, 1, 100));
        }
        CHECK(plain.index(coney::RandomStream::Gameplay) == shattered.index(coney::RandomStream::Gameplay));
    }
}

TEST_CASE("starting at an offset moves the gameplay stream only", "[game_random]") {
    // Expected: maths.md#random, confirmed (code)
    GameRandom random;
    const std::vector<std::uint32_t> table = syntheticTable();
    random.setTable(table);
    random.startAt(40);
    CHECK(random.index(coney::RandomStream::Gameplay) == 40);
    CHECK(random.index(coney::RandomStream::Effects) == 0);
    CHECK(random.next(coney::RandomStream::Effects) == table[1]);
    CHECK(random.next(coney::RandomStream::Gameplay) == table[41]);
}
