// SPDX-License-Identifier: GPL-3.0-or-later
#include "combat/grabbed.h"

#include <catch2/catch_test_macros.hpp>

#include "combat/commands.h"

// The player in another human's grab: the struggle's conditions and cost, the break-free roll, the strike back, the
// escape and the reversal, and the counter at the catch (docs/research/combat-grabs.md#grabbed).

using namespace coney::combat;

namespace {

// The puppet civilian of the runtime as a grabber: 200 power of 200, class 2 (divisor 4); the victim at 200 of 200.
GrabbedInput heldBy(CommandId command, int grabberPower) {
    GrabbedInput input;
    input.command = command;
    input.ownPower = 200;
    input.ownPowerMax = 200;
    input.ownStruggleDivisor = 3;
    input.grabber.power = grabberPower;
    input.grabber.powerMax = 200;
    input.grabber.struggleDivisor = 4;
    return input;
}

} // namespace

TEST_CASE("square struggles: 96, costing a third of the grabber's maximum even with a move playing", "[combat]") {
    CombatRandom random;
    const GrabbedOutcome outcome = updateGrabbed(heldBy(command::kSquarePressed, 200), random);
    CHECK(outcome.action == GrabbedAction::Struggle);
    CHECK(outcome.animId == kGrabStruggleFront);
    CHECK(outcome.grabberPowerCost == 67);
    GrabbedInput busy = heldBy(command::kSquarePressed, 200);
    busy.movePlaying = true;
    busy.fromRear = true;
    const GrabbedOutcome spent = updateGrabbed(busy, random);
    CHECK(spent.action == GrabbedAction::None);
    CHECK(spent.grabberPowerCost == 67);
}

TEST_CASE("the struggle needs Grab_CanStruggle and the victim's own power fraction above 1 / the grabber's divisor",
          "[combat]") {
    // Expected: combat-grabs.md#grabbed, confirmed (code) (`Player_UpdateGrabbed` 0x0027fd68 compares
    // `Human_PowerFraction` of the victim, 1.0 while it rages, with 1 / the grabber's class divisor; `Grab_CanStruggle`
    // 0x002258f0: not hurt, the grabber not raging, own power above max / 6)
    GrabbedInput input = heldBy(command::kSquarePressed, 200);
    CHECK(canStruggle(input));
    CHECK(struggleAllowed(input));
    input.hurt = true;
    CHECK_FALSE(canStruggle(input));
    CHECK_FALSE(struggleAllowed(input));
    // A sixth of 200 is 33: 33 of power is not above it, 34 is (and 34 / 200 is under a quarter, so the gate refuses).
    input = heldBy(command::kSquarePressed, 200);
    input.ownPower = 33;
    CHECK_FALSE(canStruggle(input));
    input.ownPower = 34;
    CHECK(canStruggle(input));
    CHECK_FALSE(struggleAllowed(input));
    // Raging, the victim's fraction reads 1.0 whatever its meter.
    input.ownPowerFull = true;
    CHECK(struggleAllowed(input));
    // The grabber's own meter is not the gate: at a quarter of its maximum the victim still struggles.
    input = heldBy(command::kSquarePressed, 50);
    CHECK(struggleAllowed(input));
    input = heldBy(command::kSquarePressed, 200);
    input.grabber.raging = true;
    CHECK_FALSE(canStruggle(input));
    CHECK_FALSE(struggleAllowed(input));
}

TEST_CASE("square of either kind struggles; cross strikes back only when Grab_CanStruggle allows it", "[combat]") {
    // Expected: combat-grabs.md#grabbed, confirmed (code) (`PlayerCmd_IsSquareAny` takes 0xf and 0x11,
    // `PlayerCmd_IsCrossAny` 0x10 and 0x12; cross runs `Grab_CanStruggle` 0x002258f0)
    CombatRandom random;
    CHECK(updateGrabbed(heldBy(command::kSquareChain, 200), random).action == GrabbedAction::Struggle);
    GrabbedInput hurt = heldBy(command::kCrossPressed, 200);
    hurt.hurt = true;
    CHECK(updateGrabbed(hurt, random).action == GrabbedAction::None);
    GrabbedInput raging = heldBy(command::kCrossLongHold, 200);
    raging.grabber.raging = true;
    CHECK(updateGrabbed(raging, random).action == GrabbedAction::None);
    GrabbedInput tired = heldBy(command::kCrossPressed, 200);
    tired.ownPower = 30;
    CHECK(updateGrabbed(tired, random).action == GrabbedAction::None);
}

TEST_CASE("a victim with flag 0x2000000000 cannot escape with circle", "[combat]") {
    // Expected: combat-grabs.md#grabbed, confirmed (code) (`Player_UpdateGrabbed` 0x0027fd68 skips the escape when the
    // victim's flags hold 0x2000000000)
    CombatRandom random;
    GrabbedInput input = heldBy(command::kCircleTapped, 40);
    CHECK(updateGrabbed(input, random).action == GrabbedAction::Escape);
    input.noEscape = true;
    CHECK(updateGrabbed(input, random).action == GrabbedAction::None);
}

TEST_CASE("cross strikes back with 104, and 116 from the rear", "[combat]") {
    CombatRandom random;
    CHECK(updateGrabbed(heldBy(command::kCrossPressed, 200), random).animId == kGrabStrikeBackFront);
    GrabbedInput rear = heldBy(command::kCrossLongHold, 200);
    rear.fromRear = true;
    const GrabbedOutcome outcome = updateGrabbed(rear, random);
    CHECK(outcome.action == GrabbedAction::StrikeBack);
    CHECK(outcome.animId == kGrabStrikeBackRear);
    CHECK(outcome.grabberPowerCost == 0);
}

TEST_CASE("the break-free roll: always at a quarter or less, 1 in floor(p / t) above, never against rage", "[combat]") {
    CombatRandom random(3);
    GrabberState grabber{.power = 50, .powerMax = 200};
    CHECK(breakFreeRoll(grabber, random));
    // A hurt grabber's threshold is half: 100 of 200.
    grabber.power = 100;
    grabber.hurt = true;
    CHECK(breakFreeRoll(grabber, random));
    // 189 of 200: floor(189 / 50) = 3, a third of the rolls.
    grabber = GrabberState{.power = 189, .powerMax = 200};
    int wins = 0;
    for (int i = 0; i < 300; ++i) {
        wins += breakFreeRoll(grabber, random) ? 1 : 0;
    }
    CHECK(wins > 60);
    CHECK(wins < 140);
    grabber.raging = true;
    grabber.power = 10;
    CHECK_FALSE(breakFreeRoll(grabber, random));
}

TEST_CASE("circle escapes and R1 reverses on a winning roll; flag 0x40 refuses the reversal", "[combat]") {
    CombatRandom random;
    const GrabbedOutcome escape = updateGrabbed(heldBy(command::kCirclePressed, 40), random);
    CHECK(escape.action == GrabbedAction::Escape);
    CHECK(escape.animId == kGrabEscapeFront);
    GrabbedInput reverse = heldBy(command::kR1Pressed, 40);
    reverse.fromRear = true;
    const GrabbedOutcome reversal = updateGrabbed(reverse, random);
    CHECK(reversal.action == GrabbedAction::Reversal);
    CHECK(reversal.animId == kGrabReversalRear);
    reverse.grabber.flag40 = true;
    CHECK(updateGrabbed(reverse, random).action == GrabbedAction::None);
}

TEST_CASE("R1 on the update the intro ends counters the grab", "[combat]") {
    CHECK(counterAtCatch(command::kR1Pressed, true));
    CHECK_FALSE(counterAtCatch(command::kR1Held, true));
    CHECK_FALSE(counterAtCatch(command::kR1Pressed, false));
}
