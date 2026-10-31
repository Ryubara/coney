// SPDX-License-Identifier: GPL-3.0-or-later
// Which crew mate says a tag line (docs/research/sound-events.md#tag-lines): the first candidate, each later one
// replacing it on Random_Int(100) below 50 from the tactics stream. The table is a synthetic one.
#include "gamemodes/tag_lines.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "core/game_random.h"

namespace {

// A synthetic table whose entries 1, 2, ... hold `draws` (the rest 0).
coney::GameRandom tableOf(const std::vector<std::uint32_t>& draws) {
    std::vector<std::uint32_t> table(coney::GameRandom::kTableSize, 0U);
    for (std::size_t i = 0; i < draws.size(); ++i) {
        table[i + 1] = draws[i];
    }
    coney::GameRandom random;
    random.setTable(table);
    return random;
}

} // namespace

TEST_CASE("a later tag-line candidate replaces the pick on a roll below 50 of 0-100 from the tactics stream",
          "[gameplay][tag]") {
    // Expected: sound-events.md#tag-lines, confirmed (code)
    // Three candidates: the second rolls 30 (replaces), the third 70 (keeps the second).
    coney::GameRandom random = tableOf({30, 70});
    CHECK(coney::pickTagSpeaker(3, random) == std::optional<std::size_t>(1));
    // One draw for each candidate after the first, on the tactics stream only.
    CHECK(random.index(coney::RandomStream::Tactics) == 2);
    CHECK(random.index(coney::RandomStream::Gameplay) == 0);

    // The roll is 0 to 100 inclusive: an entry of 100 is 100, not 0, and keeps the pick; 49 replaces, 50 keeps.
    coney::GameRandom edges = tableOf({100, 49, 50});
    CHECK(coney::pickTagSpeaker(4, edges) == std::optional<std::size_t>(2));

    // One candidate draws nothing; none says nothing.
    coney::GameRandom single = tableOf({});
    CHECK(coney::pickTagSpeaker(1, single) == std::optional<std::size_t>(0));
    CHECK(single.index(coney::RandomStream::Tactics) == 0);
    CHECK_FALSE(coney::pickTagSpeaker(0, single).has_value());
}
