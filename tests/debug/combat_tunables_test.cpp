// SPDX-License-Identifier: GPL-3.0-or-later
#include "debug/combat_tunables.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "combat/combat_tuning.h"
#include "debug/game_tunables.h"

using coney::debug::TunableRegistry;

TEST_CASE("the combat tunables edit the values combat reads, defaulting to the research's", "[debug][combat]") {
    // Expected: combat/index.md#constants, confirmed (runtime) (the history hold 7, the power endurance 0.25, the
    // mash target 1000; the count is Coney's list)
    TunableRegistry registry;
    coney::debug::registerCombatTunables(registry);
    CHECK(registry.inCategory("Combat").size() == 39);
    REQUIRE(registry.find("Combat/History hold") != nullptr);
    CHECK(registry.find("Combat/History hold")->defaultValue() == 7.0);
    REQUIRE(registry.find("Combat/Power endurance") != nullptr);
    CHECK(registry.find("Combat/Power endurance")->defaultValue() == Catch::Approx(0.25));
    REQUIRE(registry.find("Combat/Mash target") != nullptr);
    CHECK(registry.find("Combat/Mash target")->defaultValue() == 1000.0);

    registry.set("Combat/History hold", 5);
    registry.set("Combat/Snap needs target", 0);
    registry.applyPending();
    CHECK(coney::combat::combatTuning().historyHoldSamples == 5);
    CHECK_FALSE(coney::combat::combatTuning().snapNeedsTarget);

    // Back to the researched values, so other tests see combat as it is.
    registry.resetAll();
    registry.applyPending();
    CHECK(coney::combat::combatTuning().historyHoldSamples == 7);
    CHECK(coney::combat::combatTuning().snapNeedsTarget);
    CHECK(registry.removeCategory("Combat") == 39);
}

TEST_CASE("the combat tunables register beside the game's, as main does, with the rage's hold", "[debug][combat]") {
    // Expected: combat-rage.md#rage, confirmed (code) (the hold timer of 5000 ms; the count is Coney's list)
    TunableRegistry registry;
    coney::debug::registerGameTunables(registry);
    const std::size_t game = registry.size();
    coney::debug::registerCombatTunables(registry);
    CHECK(registry.size() == game + 39);
    // The rage's drain and decay are each player's Warrior class's (combat::RageClass), not tunables.
    CHECK(registry.find("Combat/Rage drain") == nullptr);
    REQUIRE(registry.find("Combat/Rage hold") != nullptr);
    CHECK(registry.find("Combat/Rage hold")->defaultValue() == 5000.0);
    CHECK(registry.find("Combat/Rage decay") == nullptr);
}
