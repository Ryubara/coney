// SPDX-License-Identifier: GPL-3.0-or-later
#include "combat/revive.h"

#include <catch2/catch_test_macros.hpp>

using coney::combat::mayRevive;
using coney::combat::ReviveFacts;
using coney::combat::ReviveFlashFrom;
using coney::combat::reviveFlashFrom;

namespace {

// Player 1 beside a downed AI Warrior in the open, holding a flash: the case that revives.
ReviveFacts playerBesideWarrior() {
    return ReviveFacts{.reviverHeld = false,
                       .reviverBlocked = false,
                       .found = true,
                       .downedHeld = false,
                       .wayClear = true,
                       .downedIsPlayer = false,
                       .reviverIsWarrior = false,
                       .reviverHasFlash = true,
                       .downedHasFlash = false};
}

} // namespace

TEST_CASE("a free reviver with a flash revives a downed partner in the open", "[combat][revive]") {
    CHECK(mayRevive(playerBesideWarrior()));
}

TEST_CASE("the revive is refused while either human is held or the reviver is blocked, or with nobody found",
          "[combat][revive]") {
    ReviveFacts facts = playerBesideWarrior();
    facts.reviverHeld = true;
    CHECK_FALSE(mayRevive(facts));
    facts = playerBesideWarrior();
    facts.reviverBlocked = true;
    CHECK_FALSE(mayRevive(facts));
    facts = playerBesideWarrior();
    facts.found = false;
    CHECK_FALSE(mayRevive(facts));
    facts = playerBesideWarrior();
    facts.downedHeld = true;
    CHECK_FALSE(mayRevive(facts));
}

TEST_CASE("the revive needs a flash, the reviver's or the downed human's", "[combat][revive]") {
    ReviveFacts facts = playerBesideWarrior();
    facts.reviverHasFlash = false;
    CHECK_FALSE(mayRevive(facts));
    facts.downedHasFlash = true;
    CHECK(mayRevive(facts));
}

TEST_CASE("with the way not clear only a Warrior reviving a downed player goes on", "[combat][revive]") {
    ReviveFacts facts = playerBesideWarrior();
    facts.wayClear = false;
    // A downed AI human: refused, whoever revives.
    CHECK_FALSE(mayRevive(facts));
    facts.reviverIsWarrior = true;
    CHECK_FALSE(mayRevive(facts));
    // A downed player: a Warrior may, another player may not.
    facts.downedIsPlayer = true;
    CHECK(mayRevive(facts));
    facts.reviverIsWarrior = false;
    CHECK_FALSE(mayRevive(facts));
}

TEST_CASE("a finished revive spends the downed human's flash first, else the reviver's", "[combat][revive]") {
    CHECK(reviveFlashFrom(true) == ReviveFlashFrom::Downed);
    CHECK(reviveFlashFrom(false) == ReviveFlashFrom::Reviver);
}
