// SPDX-License-Identifier: GPL-3.0-or-later
// The scripts' animation callbacks (docs/research/characters.md#anim-callbacks): the 16-slot table, the first match
// firing, all-humans slots, removal, and slots of humans gone counting as free. Synthetic handles.
#include "scripting/anim_callbacks.h"

#include <set>
#include <string>

#include <catch2/catch_test_macros.hpp>

using coney::script::AnimCallbacks;

TEST_CASE("an animation callback fires for its human and anim, the first matching slot winning", "[anim_callbacks]") {
    AnimCallbacks table;
    std::set<double> live{10.0, 11.0};
    table.setResolves([&live](double handle) { return live.contains(handle); });

    REQUIRE(table.add(10.0, 388, "P1.PlayerState"));
    REQUIRE(table.add(10.0, 388, "P1.Second"));
    REQUIRE(table.addAll(82, "Any"));
    CHECK(table.match(10.0, 388) == "P1.PlayerState");
    CHECK(table.match(11.0, 388).empty());
    CHECK(table.match(11.0, 82) == "Any");
    CHECK(table.match(10.0, 82) == "Any");

    table.remove(10.0, 388);
    CHECK(table.match(10.0, 388) == "P1.Second");
    table.remove(99.0, 82); // an all-humans slot goes whoever asks
    CHECK(table.match(11.0, 82).empty());

    // A human gone frees its slots, and its callbacks no longer fire.
    live.erase(10.0);
    CHECK(table.match(10.0, 388).empty());
    table.clear();
    CHECK(table.match(11.0, 82).empty());
}

TEST_CASE("the table holds 16 callbacks; a slot of a human gone is taken again", "[anim_callbacks]") {
    AnimCallbacks table;
    std::set<double> live;
    table.setResolves([&live](double handle) { return live.contains(handle); });
    for (int k = 0; k < static_cast<int>(AnimCallbacks::kSlots); ++k) {
        live.insert(100.0 + k);
        REQUIRE(table.add(100.0 + k, 1, "F" + std::to_string(k)));
    }
    live.insert(200.0);
    CHECK_FALSE(table.add(200.0, 1, "Full"));
    CHECK_FALSE(table.addAll(1, "Full"));
    live.erase(105.0);
    CHECK(table.add(200.0, 2, "Again"));
    CHECK(table.slots()[5].name == "Again");
}
