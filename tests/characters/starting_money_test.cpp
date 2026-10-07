// SPDX-License-Identifier: GPL-3.0-or-later
// A new human's starting money and object (docs/research/crimes.md#starting-money), with Coney's stand-in generator.
#include "characters/starting_money.h"

#include <catch2/catch_test_macros.hpp>

using coney::GameRandom;
using coney::characters::CarryConfig;
using coney::characters::ClassMoney;
using coney::characters::rollStartingCarry;

TEST_CASE("the street civilian always starts with $5-$20, or five times that, and no object", "[characters]") {
    // PoizoCiv (type 417): category 4, v16 -1, a grp_ object group; category 4's record.
    const CarryConfig civilian{.category = 4, .altRecord = false, .dropChance = -1, .object = "grp_male_ped"};
    const ClassMoney street{.min = 5, .max = 20, .noObject = 100, .bonusChance = 1, .bonus = 5.0F};
    GameRandom random;
    for (int human = 0; human < 200; ++human) {
        const std::uint32_t before = random.index();
        const auto carry = rollStartingCarry(civilian, street, random);
        CHECK(carry.object == "none");
        const bool plain = carry.money >= 5 && carry.money <= 20;
        const bool bonus = carry.money >= 25 && carry.money <= 100 && carry.money % 5 == 0;
        CHECK((plain || bonus));
        // Four draws: the object roll, the money, the bonus roll and the brain's.
        CHECK((random.index() - before) % GameRandom::kTableSize == 4);
    }
}

TEST_CASE("a capped type: no object at or below the chance, money up to the cap; an object rolls the range",
          "[characters]") {
    const ClassMoney cls{.min = 5, .max = 10, .noObject = 100, .bonusChance = 0, .bonus = 1.0F};
    GameRandom random;
    // v16 0 caps the money at 0 (min is not below it).
    const CarryConfig broke{.category = 9, .altRecord = false, .dropChance = 0, .object = "dyn_bat"};
    CHECK(rollStartingCarry(broke, cls, random).money == 0);
    // With a no-object chance of 0 every roll above 0 may carry the object; a drop chance of 100 always does.
    const ClassMoney carrier{.min = 5, .max = 10, .noObject = -1, .bonusChance = 0, .bonus = 1.0F};
    const CarryConfig armed{.category = 9, .altRecord = false, .dropChance = 100, .object = "dyn_bat"};
    const auto carry = rollStartingCarry(armed, carrier, random);
    CHECK(carry.object == "dyn_bat");
    CHECK(carry.dropChance == 100);
    CHECK(carry.money >= 5);
    CHECK(carry.money <= 10);
    CHECK(coney::characters::moneyCategory(CarryConfig{.category = 4, .altRecord = true}) == 16);
}
