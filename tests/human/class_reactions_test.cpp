// SPDX-License-Identifier: GPL-3.0-or-later
// What the power class's reaction fields change about a hit on the player (`Human_PlayReaction`): hard to stun
// (`+0x3b`, human flag `0x1000000000`) and the cap on the ground strikes a human reacts to while he gets up (`+0x38`),
// driven by synthetic hits on the synthetic fight character (docs/research/characters.md#power-class-bytes).
#include <algorithm>
#include <cstdint>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "combat/power_class.h"
#include "human/human.h"
#include "human/human_flags.h"
#include "human/victim.h"
#include "support/fight_fixtures.h"

using coney::anim::Vec3;
using coney::human::IncomingHit;
using coney::test::Fight;
using coney::test::FightCharacter;
namespace flag = coney::human::flag;

namespace {

// Where an attacker in front of the player (at (40, 40) facing +y) stands.
constexpr Vec3 kInFront{40.0F, 41.0F, 0.0F};
// The clips: the ground strike's reaction, the ground, the rise; a heavy hit's knockdown; the stun's loop and end.
constexpr std::uint32_t kGroundStrike = 195;
constexpr std::uint32_t kGround = 196;
constexpr std::uint32_t kRise = 199;
constexpr std::uint32_t kStunEnd = 357;

// A hit of attack `animId` with `damage`, `code` and `flags`, from in front.
IncomingHit hitOf(int animId, int damage, int code, std::uint16_t flags = 0) {
    return IncomingHit{.damage = damage, .attackAnim = animId, .code = code, .flags = flags, .attacker = kInFront};
}

// Knocks the player down (XX2, 288), kicks him `kicks` times while he lies in 196, then runs until 199 has played
// `riseUpdates` updates. Returns whether he got that far.
bool kickThenRise(Fight& fight, int kicks, int riseUpdates) {
    fight.human().takeHit(hitOf(13, 46, 0x26));
    fight.run("", 20);
    for (int kick = 0; kick < kicks; ++kick) {
        fight.human().takeHit(hitOf(193, 20, 0x0a));
        fight.run("", 14);
    }
    int inRise = 0;
    for (int update = 0; update < 200; ++update) {
        fight.run("", 1);
        if (fight.human().animator().animId() == kRise) {
            ++inRise;
            if (inRise == riseUpdates) {
                return true;
            }
        }
    }
    return false;
}

} // namespace

TEST_CASE("a hard-to-stun human is not stunned by an attack that stuns", "[human][combat]") {
    // Expected: characters.md#power-class-bytes, confirmed (code)
    const FightCharacter character;
    Fight fight(character, 30.0F);
    fight.human().setFlag(flag::kHardToStun, true);
    // SX2 (0x1a) with the stun flag 0x400: 280, and no stun.
    fight.human().takeHit(hitOf(15, 37, 0x1a, 0xc00));
    std::vector<std::uint32_t> clips;
    fight.run("", 30, [&](std::uint64_t) { clips.push_back(fight.human().animator().animId()); });
    CHECK(std::ranges::find(clips, 280U) != clips.end());
    CHECK(std::ranges::find(clips, kStunEnd) == clips.end());
    CHECK(fight.human().fighter().victim().stuns() == 0);
    // Stunned already on his feet, a hard-to-stun human is stunned again, even by a hit without the stun flag.
    fight.human().setFlag(flag::kHardToStun, false);
    fight.human().takeHit(hitOf(15, 37, 0x1a, 0xc00));
    fight.run("", 2);
    REQUIRE(fight.human().fighter().victim().stunned());
    fight.human().setFlag(flag::kHardToStun, true);
    fight.human().takeHit(hitOf(12, 14, 0x0a, 0x800));
    fight.run("", 1);
    CHECK(fight.human().fighter().victim().stuns() == 2);
}

TEST_CASE("hit early in his rise, a human kicked fewer times than his cap goes down again with 195",
          "[human][combat]") {
    // Expected: characters.md#power-class-bytes, confirmed (code)
    const FightCharacter character;
    Fight fight(character, 30.0F);
    // The player's class 64 caps at 3: two kicks on the ground.
    REQUIRE(fight.human().fighter().victim().powerClass().groundStrikeCap == 3);
    REQUIRE(kickThenRise(fight, 2, 2));
    const int health = fight.human().fighter().health().value();
    fight.human().takeHit(hitOf(12, 14, 0x0a, 0x800));
    fight.run("", 1);
    CHECK(fight.human().animator().animId() == kGroundStrike);
    CHECK(fight.human().fighter().victim().grounded());
    CHECK(fight.human().fighter().health().value() == health - 14);
    fight.run("", 20);
    CHECK(fight.human().animator().animId() == kGround);
}

TEST_CASE("hit in his rise after as many ground strikes as his cap, a human takes the damage and gets up",
          "[human][combat]") {
    // Expected: characters.md#power-class-bytes, confirmed (code)
    const FightCharacter character;
    Fight fight(character, 30.0F);
    REQUIRE(kickThenRise(fight, 3, 2));
    const int health = fight.human().fighter().health().value();
    const int reactions = fight.human().fighter().victim().reactions();
    fight.human().takeHit(hitOf(12, 14, 0x0a, 0x800));
    fight.run("", 1);
    // No reaction: the rise plays on.
    CHECK(fight.human().animator().animId() == kRise);
    CHECK_FALSE(fight.human().fighter().victim().grounded());
    CHECK(fight.human().fighter().victim().reactions() == reactions);
    CHECK(fight.human().fighter().health().value() == health - 14);
    // Once he is up the ground strikes are forgotten: the rise ends (12 updates) and a hit plays the table's 272.
    fight.run("", 15);
    REQUIRE(fight.human().animator().animId() != kRise);
    fight.human().takeHit(hitOf(12, 14, 0x0a, 0x800));
    fight.run("", 1);
    CHECK(fight.human().animator().animId() == 272);
}

TEST_CASE("a class with a cap of 0 never reacts to a hit during his rise", "[human][combat]") {
    // Expected: characters.md#power-class-bytes, confirmed (code)
    const FightCharacter character;
    Fight fight(character, 30.0F);
    coney::human::FighterProfile profile = fight.human().fighterProfile();
    profile.powerClass.groundStrikeCap = 0;
    fight.human().setFighterProfile(profile);
    REQUIRE(kickThenRise(fight, 0, 2));
    fight.human().takeHit(hitOf(12, 14, 0x0a, 0x800));
    fight.run("", 1);
    CHECK(fight.human().animator().animId() == kRise);
}
