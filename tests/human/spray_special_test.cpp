// SPDX-License-Identifier: GPL-3.0-or-later
#include <cstdint>

#include <catch2/catch_test_macros.hpp>

#include "combat/anim_ids.h"
#include "combat/power_class.h"
#include "human/fighter.h"
#include "human/human.h"
#include "human/human_animator.h"
#include "support/fight_fixtures.h"

// Circle + triangle inside the human: the fighter plays the spray 664 that combat decided and reports it, so whoever
// keeps the inventory takes one paint; the spray warns no one and leaves the kept target as it was.

namespace id = coney::combat::anim_id;

using coney::test::Fight;
using coney::test::FightCharacter;

namespace {

// What one circle + triangle press did: whether the human played 664, and how many sprays it reported.
struct SprayRun {
    bool played = false;
    int sprays = 0;
};

// Presses circle + triangle once with a target 1 m ahead, the paint and the hand as given.
SprayRun pressSpray(bool paint, bool holding) {
    const FightCharacter character;
    Fight fight(character);
    fight.human().fighter().setSprayPaint(paint);
    fight.human().fighter().setHoldingObject(holding);
    SprayRun run;
    fight.run("5 press circle triangle\n9 release circle triangle\n", 40, [&](std::uint64_t) {
        run.played = run.played || fight.human().animator().animId() == static_cast<std::uint32_t>(id::kSpraySpecial);
        run.sprays += fight.human().fighter().takeSprays();
    });
    return run;
}

} // namespace

TEST_CASE("the player's circle + triangle sprays only with paint left and an empty hand", "[human][combat]") {
    // Expected: combat/index.md#run-attacks, confirmed (code)
    // The same press with paint and an empty hand sprays; with no paint, or with an object in hand, it plays and
    // reports nothing.
    const SprayRun sprayed = pressSpray(true, false);
    CHECK(sprayed.played);
    CHECK(sprayed.sprays == 1);
    const SprayRun noPaint = pressSpray(false, false);
    CHECK_FALSE(noPaint.played);
    CHECK(noPaint.sprays == 0);
    const SprayRun holding = pressSpray(true, true);
    CHECK_FALSE(holding.played);
    CHECK(holding.sprays == 0);
}

TEST_CASE("the spray warns nobody: not even the human it plays at", "[human][combat]") {
    // Expected: combat/index.md#run-attacks, confirmed (code)
    // Human_WarnOfAttack is not on Player_Special's spray path: the human 1 m ahead hears of no attack.
    const FightCharacter character;
    Fight fight(character);
    fight.human().fighter().setSprayPaint(true);
    bool played = false;
    fight.run("5 press circle triangle\n9 release circle triangle\n", 20, [&](std::uint64_t) {
        played = played || fight.human().animator().animId() == static_cast<std::uint32_t>(id::kSpraySpecial);
    });
    REQUIRE(played);
    CHECK(fight.target().attacksAnnounced() == 0);
}

TEST_CASE("the spray keeps the target the human had: with none it ends in 389", "[human][combat]") {
    // Expected: combat/index.md#run-attacks, confirmed (runtime)
    // The original's spray at a pedestrian it found left human +0xc8 at -1 throughout, and 389 followed 664. The
    // human here is one no player controls: a player's fight stance would take the human 1 m ahead as its target
    // before the press (Fighter::trackTarget(), Coney's reading of the stance).
    const FightCharacter character;
    Fight fight(character);
    fight.human().setFighterProfile(
        coney::human::FighterProfile{.player = false, .powerClass = coney::combat::kCivilianPowerClass, .health = 600});
    fight.human().fighter().setSprayPaint(true);
    REQUIRE(fight.human().fighter().target() == nullptr);
    bool played = false;
    bool kept = false;
    std::uint32_t after = 0;
    fight.run("5 press circle triangle\n9 release circle triangle\n", 60, [&](std::uint64_t) {
        const std::uint32_t now = fight.human().animator().animId();
        if (now == static_cast<std::uint32_t>(id::kSpraySpecial)) {
            played = true;
        } else if (played && after == 0) {
            after = now;
        }
        kept = kept || fight.human().fighter().target() != nullptr;
    });
    REQUIRE(played);
    CHECK_FALSE(kept);
    CHECK(after == 389);
}
