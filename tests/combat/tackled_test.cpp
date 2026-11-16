// SPDX-License-Identifier: GPL-3.0-or-later
#include "combat/tackled.h"

#include <catch2/catch_test_macros.hpp>

#include "combat/commands.h"

// The mounted human's inputs (`Player_UpdateTackled` 0x0027f1a0, docs/research/combat-moves.md#mounted-victim): square
// or cross struggles (250, the mounter's power drained), circle breaks out (246) and R1 reverses (242), both on the
// mounter's break-free roll, and nothing is read while one of his own clips other than the struggle plays.

using namespace coney::combat;

namespace {

// The runtime's puppet civilian on top: 400 power of 400, class divisor 4; the player below with full power of 400 and
// his divisor 3.
TackledInput mountedBy(CommandId command, int mounterPower = 400) {
    TackledInput input;
    input.command = command;
    input.ownPower = 400;
    input.ownPowerMax = 400;
    input.ownStruggleDivisor = 3;
    input.mounter.power = mounterPower;
    input.mounter.powerMax = 400;
    input.mounter.struggleDivisor = 4;
    return input;
}

} // namespace

TEST_CASE("square or cross in the mount struggles: 250, the mounter losing its maximum over the victim's divisor",
          "[combat]") {
    // Expected: combat-moves.md#mounted-victim, confirmed (code)
    CombatRandom random;
    for (const CommandId command :
         {command::kSquarePressed, command::kSquareChain, command::kCrossPressed, command::kCrossLongHold}) {
        INFO("command " << static_cast<int>(command));
        const TackledOutcome outcome = updateTackled(mountedBy(command), random);
        CHECK(outcome.action == TackledAction::Struggle);
        CHECK(outcome.animId == kMountStruggle);
        // Human_SpendPower(1 / 3) of a 400 maximum: 133, truncated.
        CHECK(outcome.mounterPowerCost == 133);
    }
}

TEST_CASE("a press while the struggle plays drains the mounter but starts nothing; another clip reads nothing",
          "[combat]") {
    // Expected: combat-moves.md#mounted-victim, confirmed (code)
    CombatRandom random;
    TackledInput input = mountedBy(command::kSquarePressed);
    input.phase = TackledPhase::Struggling;
    const TackledOutcome outcome = updateTackled(input, random);
    CHECK(outcome.action == TackledAction::None);
    CHECK(outcome.mounterPowerCost == 133);
    // Circle and R1 read nothing while it plays.
    input.command = command::kCircleTapped;
    input.mounter.power = 50;
    CHECK(updateTackled(input, random).action == TackledAction::None);
    input.command = command::kR1Pressed;
    CHECK(updateTackled(input, random).action == TackledAction::None);
    // While another clip of his plays (the mount's start, a strike's reaction) nothing is read at all.
    for (const CommandId command : {command::kSquarePressed, command::kCircleTapped, command::kR1Pressed}) {
        TackledInput closed = mountedBy(command, 10);
        closed.phase = TackledPhase::Closed;
        const TackledOutcome closedOutcome = updateTackled(closed, random);
        CHECK(closedOutcome.action == TackledAction::None);
        CHECK(closedOutcome.mounterPowerCost == 0);
    }
}

TEST_CASE("no struggle while hurt, against rage, at a sixth of his own power or below a mounter's share", "[combat]") {
    // Expected: combat-moves.md#mounted-victim, confirmed (code)
    TackledInput input = mountedBy(command::kSquarePressed);
    CHECK(mountStruggleAllowed(input));
    input.hurt = true;
    CHECK_FALSE(mountStruggleAllowed(input));
    input = mountedBy(command::kSquarePressed);
    input.mounter.raging = true;
    CHECK_FALSE(mountStruggleAllowed(input));
    // Grab_CanStruggle: power above max / 6 (66 of 400); 66 is not enough, 67 is.
    input = mountedBy(command::kSquarePressed);
    input.ownPower = 66;
    CHECK_FALSE(mountStruggleAllowed(input));
    // Then his own power's share must pass 1 / the mounter's divisor (a quarter): 100 of 400 is not above it.
    input.ownPower = 100;
    CHECK_FALSE(mountStruggleAllowed(input));
    input.ownPower = 101;
    CHECK(mountStruggleAllowed(input));
    CombatRandom random;
    input.ownPower = 100;
    const TackledOutcome refused = updateTackled(input, random);
    CHECK(refused.action == TackledAction::None);
    CHECK(refused.mounterPowerCost == 0);
}

TEST_CASE("circle breaks out with 246 once the mounter is at a quarter of its power; never with the no-escape flag",
          "[combat]") {
    // Expected: combat-moves.md#mounted-victim, confirmed (code)
    CombatRandom random;
    for (const CommandId command : {command::kCirclePressed, command::kCircleTapped, command::kCircleHeld}) {
        const TackledOutcome outcome = updateTackled(mountedBy(command, 100), random);
        CHECK(outcome.action == TackledAction::Break);
        CHECK(outcome.animId == kMountBreak);
        CHECK(outcome.mounterPowerCost == 0);
    }
    TackledInput input = mountedBy(command::kCircleTapped, 100);
    input.noBreak = true;
    CHECK(updateTackled(input, random).action == TackledAction::None);
    input = mountedBy(command::kCircleTapped, 100);
    input.mounter.raging = true;
    CHECK(updateTackled(input, random).action == TackledAction::None);
}

TEST_CASE("above a quarter the break is a roll of 1 in floor(power / quarter): at full power some presses win",
          "[combat]") {
    // Expected: combat-moves.md#mounted-victim, confirmed (code)
    CombatRandom random(7);
    int breaks = 0;
    constexpr int kPresses = 400;
    for (int press = 0; press < kPresses; ++press) {
        breaks += updateTackled(mountedBy(command::kCircleTapped, 400), random).action == TackledAction::Break ? 1 : 0;
    }
    // 1 in 4: about 100 of 400.
    CHECK(breaks > 60);
    CHECK(breaks < 140);
}

TEST_CASE("R1 reverses the mount with 242 on the roll; not against a mounter with flag 0x40 or with the block-tackle "
          "flag",
          "[combat]") {
    // Expected: combat-moves.md#mounted-victim, confirmed (code)
    CombatRandom random;
    for (const CommandId command : {command::kR1Pressed, command::kGrabSpin}) {
        const TackledOutcome outcome = updateTackled(mountedBy(command, 100), random);
        CHECK(outcome.action == TackledAction::Reversal);
        CHECK(outcome.animId == kMountReversal);
    }
    TackledInput input = mountedBy(command::kR1Pressed, 100);
    input.mounter.flag40 = true;
    CHECK(updateTackled(input, random).action == TackledAction::None);
    input = mountedBy(command::kR1Pressed, 100);
    input.noReversal = true;
    CHECK(updateTackled(input, random).action == TackledAction::None);
}
