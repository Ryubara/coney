// SPDX-License-Identifier: GPL-3.0-or-later
// The level table CfgLevelName fills (docs/research/frontend.md#the-level-table).
#include "warriors/level_table.h"

#include <string>

#include <catch2/catch_test_macros.hpp>

using coney::LevelRecord;
using coney::LevelTable;

namespace {

// A record at `index` named `name`.
LevelRecord record(double index, std::string name) {
    LevelRecord made;
    made.id = index;
    made.name = std::move(name);
    return made;
}

} // namespace

TEST_CASE("the level table keeps records by index and finds them by name in any case", "[level_table]") {
    LevelTable table;
    CHECK(table.count() == 0);
    CHECK(table.set(record(0, "level100")));
    CHECK(table.set(record(5, "Level5")));
    CHECK(table.count() == 2);
    REQUIRE(table.at(0) != nullptr);
    CHECK(table.at(0)->name == "level100");
    CHECK(table.at(1) == nullptr);
    CHECK(table.at(LevelTable::kCapacity) == nullptr);
    CHECK(table.find("LEVEL5") == 5U);
    CHECK(!table.find("level6").has_value());
    // Setting an index again replaces its record.
    CHECK(table.set(record(5, "level6")));
    CHECK(table.count() == 2);
    CHECK(table.find("level6") == 5U);
    table.clear();
    CHECK(table.count() == 0);
}

TEST_CASE("the level table refuses indices it has no room for and cuts names to their fields", "[level_table]") {
    LevelTable table;
    CHECK_FALSE(table.set(record(-1, "a")));
    CHECK_FALSE(table.set(record(128, "a")));
    CHECK_FALSE(table.set(record(1.5, "a")));
    CHECK(table.count() == 0);
    LevelRecord longNames = record(2, std::string(40, 'n'));
    longNames.secondName = std::string(40, 's');
    longNames.worldName = std::string(40, 'w');
    longNames.fourthName = std::string(40, 'f');
    REQUIRE(table.set(longNames));
    const LevelRecord* kept = table.at(2);
    REQUIRE(kept != nullptr);
    CHECK(kept->name.size() == LevelRecord::kNameLength);
    CHECK(kept->secondName.size() == LevelRecord::kSecondNameLength);
    CHECK(kept->worldName.size() == LevelRecord::kWorldNameLength);
    CHECK(kept->fourthName.size() == LevelRecord::kFourthNameLength);
}
