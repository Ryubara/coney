// SPDX-License-Identifier: GPL-3.0-or-later
// The attacker's damage flags of the human flag word (docs/research/combat.md#human-flags): killer mode (0x20,
// `HuSetKillerMode`) takes all the victim's health with one strike, double damage (0x4000, `HuSetDoubleDamage`)
// doubles each hit. Driven by square presses at a passive target; synthetic clips and damage tables.
#include <catch2/catch_test_macros.hpp>

#include "human/human.h"
#include "human/human_flags.h"
#include "human/target_human.h"
#include "support/fight_fixtures.h"

using coney::test::Fight;
using coney::test::FightCharacter;
namespace flag = coney::human::flag;

TEST_CASE("one strike of a killer takes all the target's health", "[human][combat]") {
    const FightCharacter character;
    Fight fight(character);
    fight.human().setFlag(flag::kKiller, true);
    fight.run("10 tap square\n", 40);
    CHECK(fight.target().hitsTaken() == 1);
    CHECK(fight.target().health().depleted());
}

TEST_CASE("double damage doubles each hit's table damage; cleared, the hit is the table's again", "[human][combat]") {
    const FightCharacter character;
    SECTION("doubled: S1's 17 becomes 34") {
        Fight fight(character);
        fight.human().setFlag(flag::kDoubleDamage, true);
        fight.run("10 tap square\n", 40);
        CHECK(fight.target().hitsTaken() == 1);
        CHECK(fight.target().damageTaken() == 34);
    }
    SECTION("set and cleared: 17") {
        Fight fight(character);
        fight.human().setFlag(flag::kDoubleDamage, true);
        fight.human().setFlag(flag::kDoubleDamage, false);
        fight.run("10 tap square\n", 40);
        CHECK(fight.target().damageTaken() == 17);
    }
}
