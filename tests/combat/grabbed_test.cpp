// SPDX-License-Identifier: GPL-3.0-or-later
#include "combat/grabbed.h"

#include <catch2/catch_test_macros.hpp>

#include "combat/commands.h"

// The player in another human's grab: the struggle's conditions and cost, the break-free roll, the strike back, the
// escape and the reversal, and the counter at the catch (docs/research/combat.md#grabbed).

using namespace coney::combat;

namespace {

// The puppet civilian of the runtime as a grabber: 200 power of 200, class 2 (divisor 4).
GrabbedInput heldBy(CommandId command, int grabberPower) {
    GrabbedInput input;
    input.command = command;
    input.ownPowerFraction = 1.0F;
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

TEST_CASE("no struggle while hurt, against rage, low on power or with the grabber at a quarter", "[combat]") {
    GrabbedInput input = heldBy(command::kSquarePressed, 200);
    CHECK(struggleAllowed(input));
    input.hurt = true;
    CHECK_FALSE(struggleAllowed(input));
    input = heldBy(command::kSquarePressed, 200);
    input.ownPowerFraction = 0.15F;
    CHECK_FALSE(struggleAllowed(input));
    input = heldBy(command::kSquarePressed, 50);
    CHECK_FALSE(struggleAllowed(input));
    input = heldBy(command::kSquarePressed, 200);
    input.grabber.raging = true;
    CHECK_FALSE(struggleAllowed(input));
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
