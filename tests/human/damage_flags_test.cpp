// SPDX-License-Identifier: GPL-3.0-or-later
// The attacker's damage flags of the human flag word (docs/research/combat/index.md#human-flags): killer mode (0x20,
// `HuSetKillerMode`) takes all the victim's health with one strike, double damage (0x4000, `HuSetDoubleDamage`)
// doubles each hit; then what the victim's side changes: a Warrior's hit on class 0x80 is quartered
// (`Human_AddPendingDamage` 0x00264bd8), and a hit on a man already out or on a boss with flag 0x10 earns no rage
// (`Hit_Score` 0x002653d8). Driven by square and cross presses at a passive target; synthetic clips and damage tables.
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

TEST_CASE("a Warrior's hit on class 0x80 is quartered, after the doubling; anyone else's or on another class is not",
          "[human][combat]") {
    const FightCharacter character;
    SECTION("a Warrior on class 0x80: S1's 17 becomes 4") {
        Fight fight(character);
        fight.human().fighter().setWarriorBrain(true);
        fight.target().setCharacterClass(0x80);
        fight.run("10 tap square\n", 40);
        CHECK(fight.target().hitsTaken() == 1);
        CHECK(fight.target().damageTaken() == 4);
    }
    SECTION("doubled first: 34, then a quarter, 8") {
        Fight fight(character);
        fight.human().fighter().setWarriorBrain(true);
        fight.human().setFlag(flag::kDoubleDamage, true);
        fight.target().setCharacterClass(0x80);
        fight.run("10 tap square\n", 40);
        CHECK(fight.target().damageTaken() == 8);
    }
    SECTION("not a Warrior's brain: 17") {
        Fight fight(character);
        fight.target().setCharacterClass(0x80);
        fight.run("10 tap square\n", 40);
        CHECK(fight.target().damageTaken() == 17);
    }
    SECTION("a Warrior on another class: 17") {
        Fight fight(character);
        fight.human().fighter().setWarriorBrain(true);
        fight.target().setCharacterClass(0x7f);
        fight.run("10 tap square\n", 40);
        CHECK(fight.target().damageTaken() == 17);
    }
}

TEST_CASE("a hit earns no rage on a man already out or on a boss with flag 0x10", "[human][combat]") {
    const FightCharacter character;
    // X1 earns 5 rage (the rage test in being_hit_test.cpp).
    SECTION("a standing man: 5") {
        Fight fight(character);
        fight.run("10 tap cross\n", 40);
        CHECK(fight.target().hitsTaken() == 1);
        CHECK(fight.human().fighter().combat().rage().value() == 5);
    }
    SECTION("a man out of health scores none; the blow that put him out scored") {
        Fight fight(character);
        CHECK(coney::human::Fighter::scoresRageOn(fight.target()));
        fight.human().setFlag(flag::kKiller, true);
        fight.run("10 tap cross\n", 40);
        REQUIRE(fight.target().health().depleted());
        CHECK(fight.human().fighter().combat().rage().value() == 5);
        CHECK_FALSE(coney::human::Fighter::scoresRageOn(fight.target()));
    }
    SECTION("a boss with flag 0x10: none; the flag alone or the class alone: 5") {
        Fight boss(character);
        boss.target().setBossClass(true);
        boss.target().setFlags(flag::kGod);
        boss.run("10 tap cross\n", 40);
        CHECK(boss.target().hitsTaken() == 1);
        CHECK(boss.human().fighter().combat().rage().value() == 0);
        Fight god(character);
        god.target().setFlags(flag::kGod);
        god.run("10 tap cross\n", 40);
        CHECK(god.human().fighter().combat().rage().value() == 5);
        Fight classOnly(character);
        classOnly.target().setBossClass(true);
        classOnly.run("10 tap cross\n", 40);
        CHECK(classOnly.human().fighter().combat().rage().value() == 5);
    }
}
