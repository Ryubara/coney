// SPDX-License-Identifier: GPL-3.0-or-later
// When a human beaten to 0 health is knocked out (Human::knockedOut()): from the update his dying reaction's clip ends,
// as the original's death chain runs Human_OnHealthOut at its last clip's end
// (docs/research/combat-reactions.md#knockout-timing). A synthetic fighter with synthetic clips.
#include <cstdint>

#include <catch2/catch_test_macros.hpp>

#include "human/fighter.h"
#include "human/human.h"
#include "human/human_animator.h"
#include "human/human_flags.h"
#include "support/fight_fixtures.h"

using coney::human::Human;

TEST_CASE("a human beaten to 0 health is knocked out when his dying reaction's clip ends, not at the hit",
          "[human][combat]") {
    // Expected: combat-reactions.md#knockout-timing, confirmed (runtime): Human_PlayDeathReaction (0x00267a00) gives
    // the death chain's last clip before its loop Human_OnHealthOut (0x002674c0) as its end callback, which knocks
    // out through Human_KnockOut (0x00230328); in knockout_timing the lethal hit landed at update 13, the dying
    // reaction 292 played to update 57 with state 0x80000000 and no 0x40000, and 0x40000 came at 58 with the loop 349.
    const coney::test::FightCharacter character;
    coney::test::Fight fight(character, 30.0F);
    Human& human = fight.human();
    human.setFlag(coney::human::flag::kDemiGod, false);
    human.takeHit(coney::human::IncomingHit{
        .damage = 900, .attackAnim = 12, .code = 0x0a, .flags = 0x800, .attacker = {40.0F, 41.0F, 0.0F}});
    fight.run("", 1);
    REQUIRE(human.fighter().health().depleted());
    REQUIRE(human.animator().drivingClipPlaying());
    const std::uint32_t dying = human.animator().animId();
    // Through the whole dying reaction he is dying, not knocked out.
    int updates = 1;
    int knockedOutWhileDying = 0;
    while (human.animator().drivingClipPlaying() && updates < 600) {
        knockedOutWhileDying += human.knockedOut() ? 1 : 0;
        fight.run("", 1);
        ++updates;
    }
    REQUIRE(updates > 2);
    CHECK(knockedOutWhileDying == 0);
    REQUIRE(human.animator().animId() != dying);
    // The update the clip ends, the loop takes over and he is knocked out; he stays so while his health is out.
    CHECK(human.knockedOut());
    fight.run("", 30);
    CHECK(human.knockedOut());
}

TEST_CASE("a human beaten to 0 health keeps 1 health from the hit, and is down by state while he lies there",
          "[human][combat]") {
    // Expected: combat-reactions.md#down-health. Human_PlayDeathReaction (0x00267a00) writes 1 to the health (record
    // +0x144) as it builds the death chain at the lethal hit, and the knock-out at the chain's end (Human_OnHealthOut
    // 0x002674c0, Human_KnockOut 0x00230328) leaves it; Human_IsDownOrDead (0x00227e60) reads the states 0x180050000,
    // not the health. Confirmed (runtime), run r-20261122-war-party: the Orphans and Furies knocked out lay at 1
    // health, dying from the update of the lethal hit.
    const coney::test::FightCharacter character;
    coney::test::Fight fight(character, 30.0F);
    Human& human = fight.human();
    human.setFlag(coney::human::flag::kDemiGod, false);
    human.takeHit(coney::human::IncomingHit{
        .damage = 900, .attackAnim = 12, .code = 0x0a, .flags = 0x800, .attacker = {40.0F, 41.0F, 0.0F}});
    fight.run("", 1);
    // Dying: down by state, with 1 health.
    CHECK(human.fighter().health().value() == 1);
    CHECK(human.downOrDead());
    CHECK_FALSE(human.knockedOut());
    int updates = 0;
    while (human.animator().drivingClipPlaying() && updates < 600) {
        fight.run("", 1);
        ++updates;
    }
    // Knocked out: still down by state, still 1 health, and a hit on him takes none.
    REQUIRE(human.knockedOut());
    CHECK(human.fighter().health().value() == 1);
    CHECK(human.downOrDead());
    human.takeHit(coney::human::IncomingHit{
        .damage = 50, .attackAnim = 12, .code = 0x0a, .flags = 0x800, .attacker = {40.0F, 41.0F, 0.0F}});
    fight.run("", 30);
    CHECK(human.knockedOut());
    CHECK(human.fighter().health().value() == 1);
}
