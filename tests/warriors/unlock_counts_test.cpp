// SPDX-License-Identifier: GPL-3.0-or-later
// The counts of unlockable records the statistics, mission-select and Rumble screens show
// (docs/research/game-stats.md#counts), on synthetic records.
// Each new case here failed first on the unit's stubbed source (coney-tools original fails-first).
#include <cstddef>
#include <cstdint>

#include <catch2/catch_test_macros.hpp>

#include "warriors/profile_record.h"
#include "warriors/unlock_records.h"

using coney::LevelTargets;
using coney::SavedProgress;
using coney::UnlockCounts;
using coney::UnlockRecord;
using coney::UnlockRecords;
using coney::UnlockTally;

namespace {

// A record of `level`, `group`, `item` and `type` (no data).
UnlockRecord rec(std::uint8_t level, std::uint8_t group, std::uint8_t item, std::uint8_t type) {
    return UnlockRecord{.level = level, .group = group, .item = item, .type = type, .extra = 0, .data = 0};
}

} // namespace

TEST_CASE("the completion count takes every type but 0, 5 and 7-11 outside levels 1 and 4; group 1 counts distinct "
          "pairs of types 1-4 and 6",
          "[unlocks]") {
    // Expected: game-stats.md#counts, confirmed (code)
    UnlockRecords records;
    records.setCount(12);
    REQUIRE(records.set(0, rec(2, 0, 0, 4)));   // counted
    REQUIRE(records.set(1, rec(2, 0, 0, 0)));   // type 0: not counted
    REQUIRE(records.set(2, rec(1, 0, 0, 6)));   // level 1: not counted
    REQUIRE(records.set(3, rec(4, 0, 0, 6)));   // level 4: not counted
    REQUIRE(records.set(4, rec(3, 1, 1, 2)));   // counted; group-1 pair (3, 1)
    REQUIRE(records.set(5, rec(3, 1, 1, 4)));   // counted; same pair, not tested for the group count
    REQUIRE(records.set(6, rec(4, 1, 2, 6)));   // level 4: not in the total, but its group-1 pair (4, 2) counts
    REQUIRE(records.set(7, rec(5, 1, 1, 7)));   // type 7: neither
    REQUIRE(records.set(8, rec(9, 0, 3, 1)));   // counted
    REQUIRE(records.set(9, rec(9, 0, 0, 3)));   // counted
    REQUIRE(records.set(10, rec(2, 0, 0, 1)));  // counted
    REQUIRE(records.set(11, rec(6, 1, 5, 12))); // type 12: counted, but not a group-1 type

    SavedProgress progress;
    UnlockCounts counts = records.countUnlocked(progress);
    CHECK(counts.records.total == 7);
    CHECK(counts.records.unlocked == 0);
    CHECK(counts.groupItems.total == 2);
    CHECK(counts.groupItems.unlocked == 0);
    CHECK(counts.percent() == 0);

    // Unlock the second record of pair (3, 1) only: the pair stays locked, the record counts.
    progress.setLocked(5, false);
    progress.setLocked(0, false);
    progress.setLocked(2, false); // level 1: changes nothing
    counts = records.countUnlocked(progress);
    CHECK(counts.records.unlocked == 2);
    CHECK(counts.groupItems.unlocked == 0);
    CHECK(counts.percent() == 28); // 2 / 7, truncated

    progress.setLocked(4, false);
    progress.setLocked(6, false);
    counts = records.countUnlocked(progress);
    CHECK(counts.groupItems.unlocked == 2);
    CHECK(counts.groupItems.complete());
    CHECK_FALSE(counts.records.complete());
}

TEST_CASE("the completion percentage divides before it multiplies by 100: 53 of 100 is 52", "[unlocks]") {
    // Expected: game-stats.md#summary-text, confirmed (code) at 0x00215290 (div.s, then mul.s by 100): in single
    // precision 53 / 100 is just under 0.53, and 100 times it truncates to 52 (53 × 100 / 100 would give 53).
    UnlockCounts counts;
    counts.records = UnlockTally{.unlocked = 53, .total = 100};
    CHECK(counts.percent() == 52);
    counts.records = UnlockTally{.unlocked = 59, .total = 100};
    CHECK(counts.percent() == 58);
    counts.records = UnlockTally{.unlocked = 30, .total = 469};
    CHECK(counts.percent() == 6);
}

TEST_CASE("a level's targets: the score, characters, arenas and distinct group-1 items; 60 and 101 cover ranges",
          "[unlocks]") {
    // Expected: game-stats.md#counts, confirmed (code)
    UnlockRecords records;
    records.setCount(12);
    REQUIRE(records.set(0, rec(9, 0, 0, 0)));  // the level-complete record
    REQUIRE(records.set(1, rec(9, 0, 15, 4))); // the score reward: 15,000; a character
    REQUIRE(records.set(2, rec(9, 0, 7, 2)));  // an arena
    REQUIRE(records.set(3, rec(9, 1, 1, 4)));  // group 1 item 1: a character too
    REQUIRE(records.set(4, rec(9, 1, 1, 6)));  // the same item again
    REQUIRE(records.set(5, rec(9, 2, 30, 4))); // group 2: ignored, even for the score
    REQUIRE(records.set(6, rec(62, 0, 0, 4))); // 60 stands for 60-64
    REQUIRE(records.set(7, rec(64, 1, 3, 2)));
    REQUIRE(records.set(8, rec(65, 0, 0, 4)));  // outside 60-64
    REQUIRE(records.set(9, rec(137, 0, 2, 2))); // 101 stands for 101-137
    REQUIRE(records.set(10, rec(138, 0, 0, 2)));
    REQUIRE(records.set(11, rec(9, 1, 2, 7))); // type 7: not a group-1 item

    SavedProgress progress;
    const LevelTargets nine = records.levelTargets(progress, 9);
    CHECK(nine.scoreTarget == 15000);
    CHECK(nine.characters.total == 2);
    CHECK(nine.arenas.total == 1);
    CHECK(nine.groupItems.total == 1);
    CHECK(nine.groupItems.unlocked == 0);

    progress.setLocked(3, false); // the first record of item 1 decides it
    progress.setLocked(1, false);
    const LevelTargets after = records.levelTargets(progress, 9);
    CHECK(after.groupItems.unlocked == 1);
    CHECK(after.characters.unlocked == 2);
    CHECK(after.characters.complete());
    CHECK_FALSE(after.arenas.complete());

    const LevelTargets sixty = records.levelTargets(progress, 60);
    CHECK(sixty.characters.total == 1);
    CHECK(sixty.arenas.total == 1);
    CHECK(sixty.groupItems.total == 1);
    CHECK(sixty.scoreTarget == 0);

    const LevelTargets hundredOne = records.levelTargets(progress, 101);
    CHECK(hundredOne.arenas.total == 1);
    CHECK(hundredOne.scoreTarget == 2000);
}

TEST_CASE("the character count takes every type-4 record", "[unlocks]") {
    // Expected: game-stats.md#counts, confirmed (code)
    UnlockRecords records;
    records.setCount(3);
    REQUIRE(records.set(0, rec(1, 0, 0, 4)));
    REQUIRE(records.set(1, rec(4, 5, 0, 4)));
    REQUIRE(records.set(2, rec(4, 5, 0, 2)));
    SavedProgress progress;
    progress.setLocked(1, false);
    const UnlockTally characters = records.countCharacters(progress);
    CHECK(characters.total == 2);
    CHECK(characters.unlocked == 1);
    CHECK(UnlockTally{}.complete()); // nothing to unlock is complete
}
