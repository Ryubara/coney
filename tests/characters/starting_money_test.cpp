// SPDX-License-Identifier: GPL-3.0-or-later
// A new human's starting money and object (docs/research/crimes.md#starting-money), with Coney's stand-in generator.
#include "characters/starting_money.h"

#include <cstdint>
#include <optional>

#include <catch2/catch_test_macros.hpp>

using coney::GameRandom;
using coney::characters::CarryConfig;
using coney::characters::ClassMoney;
using coney::characters::rollStartingCarry;

TEST_CASE("the street civilian always starts with $5-$20, or five times that, and no object", "[characters]") {
    // Expected: crimes.md#starting-money, confirmed (code)
    // PoizoCiv (type 417): category 4, v16 -1, a grp_ object group; category 4's record.
    const CarryConfig civilian{.category = 4, .altRecord = false, .dropChance = -1, .object = "grp_male_ped"};
    const ClassMoney street{.min = 5, .max = 20, .noObject = 100, .bonusChance = 1, .bonus = 5.0F};
    GameRandom random;
    for (int human = 0; human < 200; ++human) {
        const std::uint32_t before = random.index(coney::RandomStream::Gameplay);
        const auto carry = rollStartingCarry(civilian, street, random);
        CHECK(carry.object == "none");
        const bool plain = carry.money >= 5 && carry.money <= 20;
        const bool bonus = carry.money >= 25 && carry.money <= 100 && carry.money % 5 == 0;
        CHECK((plain || bonus));
        // Four draws: the object roll, the money, the bonus roll and the brain's.
        CHECK((random.index(coney::RandomStream::Gameplay) - before) % GameRandom::kTableSize == 4);
    }
}

TEST_CASE("a capped type: no object at or below the chance, money up to the cap; an object rolls the range",
          "[characters]") {
    // Expected: crimes.md#starting-money, confirmed (code)
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

TEST_CASE("categories 4 and 6 give the brain ped type 1, or 2 for the rich and the women; 5 and 7 give 3",
          "[characters]") {
    // Expected: human-init.md#ped-type, confirmed (code)
    // No object, a fixed sum and no bonus, so the money is known: $5 is poor, $30 is rich (21 or more).
    const ClassMoney poor{.min = 5, .max = 5, .noObject = 100, .bonusChance = -1, .bonus = 1.0F};
    const ClassMoney rich{.min = 30, .max = 30, .noObject = 100, .bonusChance = -1, .bonus = 1.0F};
    const auto config = [](int category, bool woman) {
        return CarryConfig{.category = category, .altRecord = woman, .dropChance = -1, .object = "none"};
    };
    GameRandom random;
    CHECK(rollStartingCarry(config(6, false), poor, random).pedType == std::optional<std::uint16_t>(1));
    CHECK(rollStartingCarry(config(6, false), rich, random).pedType == std::optional<std::uint16_t>(2));
    CHECK(rollStartingCarry(config(6, true), poor, random).pedType == std::optional<std::uint16_t>(2));
    CHECK(rollStartingCarry(config(5, false), poor, random).pedType == std::optional<std::uint16_t>(3));
    CHECK(rollStartingCarry(config(7, true), rich, random).pedType == std::optional<std::uint16_t>(3));
    CHECK_FALSE(rollStartingCarry(config(9, false), poor, random).pedType.has_value());
    CHECK_FALSE(rollStartingCarry(config(14, false), poor, random).pedType.has_value());
    // A rich man or a woman of category 4 is a 2 whatever the brain's roll.
    CHECK(rollStartingCarry(config(4, false), rich, random).pedType == std::optional<std::uint16_t>(2));
    CHECK(rollStartingCarry(config(4, true), poor, random).pedType == std::optional<std::uint16_t>(2));
}

TEST_CASE("a poor man of category 4 is a ped type 2 when the brain's roll is below 10, else 1", "[characters]") {
    // Expected: human-init.md#ped-type, confirmed (code)
    const ClassMoney poor{.min = 5, .max = 5, .noObject = 100, .bonusChance = -1, .bonus = 1.0F};
    const CarryConfig man{.category = 4, .altRecord = false, .dropChance = -1, .object = "none"};
    int twos = 0;
    int ones = 0;
    for (std::uint32_t seed = 0; seed < GameRandom::kTableSize; ++seed) {
        GameRandom random;
        random.startAt(seed);
        // The reference draws: the object roll, the money, the bonus roll, then the brain's roll.
        GameRandom reference;
        reference.startAt(seed);
        static_cast<void>(reference.range(coney::RandomStream::Gameplay, 0, 100));
        static_cast<void>(reference.range(coney::RandomStream::Gameplay, 5, 5));
        static_cast<void>(reference.range(coney::RandomStream::Gameplay, 0, 100));
        const std::uint16_t expected = reference.range(coney::RandomStream::Gameplay, 0, 100) < 10 ? 2 : 1;
        const auto carry = rollStartingCarry(man, poor, random);
        CHECK(carry.pedType == std::optional<std::uint16_t>(expected));
        (expected == 2 ? twos : ones) += 1;
    }
    // Both happen over the table.
    CHECK(twos > 0);
    CHECK(ones > twos);
}
