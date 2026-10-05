// SPDX-License-Identifier: GPL-3.0-or-later
#include "debug/combat_tunables.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "combat/combat_tuning.h"
#include "debug/game_tunables.h"

using coney::debug::TunableRegistry;

TEST_CASE("the combat tunables edit the values combat reads, defaulting to the research's", "[debug][combat]") {
    TunableRegistry registry;
    coney::debug::registerCombatTunables(registry);
    CHECK(registry.inCategory("Combat").size() == 43);
    REQUIRE(registry.find("Combat/History hold") != nullptr);
    CHECK(registry.find("Combat/History hold")->defaultValue() == 7.0);
    REQUIRE(registry.find("Combat/Power endurance") != nullptr);
    CHECK(registry.find("Combat/Power endurance")->defaultValue() == Catch::Approx(0.25));
    REQUIRE(registry.find("Combat/Mash target") != nullptr);
    CHECK(registry.find("Combat/Mash target")->defaultValue() == 1000.0);

    registry.set("Combat/History hold", 5);
    registry.set("Combat/Snap attacks", 0);
    registry.applyPending();
    CHECK(coney::combat::combatTuning().historyHoldSamples == 5);
    CHECK_FALSE(coney::combat::combatTuning().snapAttacks);

    // Back to the researched values, so other tests see combat as it is.
    registry.resetAll();
    registry.applyPending();
    CHECK(coney::combat::combatTuning().historyHoldSamples == 7);
    CHECK(coney::combat::combatTuning().snapAttacks);
    CHECK(registry.removeCategory("Combat") == 43);
}

TEST_CASE("the combat tunables register beside the game's, as main does, with the rage's new values",
          "[debug][combat]") {
    TunableRegistry registry;
    coney::debug::registerGameTunables(registry);
    const std::size_t game = registry.size();
    coney::debug::registerCombatTunables(registry);
    CHECK(registry.size() == game + 43);
    REQUIRE(registry.find("Combat/Rage drain") != nullptr);
    CHECK(registry.find("Combat/Rage drain")->defaultValue() == Catch::Approx(9.36));
    REQUIRE(registry.find("Combat/Rage hold") != nullptr);
    CHECK(registry.find("Combat/Rage hold")->defaultValue() == 5000.0);
    REQUIRE(registry.find("Combat/Rage decay") != nullptr);
    CHECK(registry.find("Combat/Rage decay")->defaultValue() == Catch::Approx(7.8));
}
