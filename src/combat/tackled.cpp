// SPDX-License-Identifier: GPL-3.0-or-later
#include "combat/tackled.h"

#include <algorithm>

namespace coney::combat {

namespace {

// A struggle needs more than this share of his own maximum power (`Grab_CanStruggle`: max / 6, integer division).
constexpr int kStruggleOwnPowerDivisor = 6;
// At a mount's clip end, circle with the stick at or below this reverses the mount; above it, it breaks out.
constexpr float kMountEndStickAtRest = 0.25F;

} // namespace

bool mountStruggleAllowed(const TackledInput& input) {
    const int ownMax = std::max(1, input.ownPowerMax);
    if (input.hurt || input.mounter.raging || input.ownPower <= ownMax / kStruggleOwnPowerDivisor) {
        return false;
    }
    // Human_PowerFraction: full while raging or tireless.
    const float ownFraction =
        input.ownPowerFull ? 1.0F : static_cast<float>(input.ownPower) / static_cast<float>(ownMax);
    return 1.0F / static_cast<float>(std::max(1, input.mounter.struggleDivisor)) < ownFraction;
}

TackledOutcome updateTackled(const TackledInput& input, CombatRandom& random) {
    TackledOutcome outcome;
    // 1. No command, or a clip of his that closes the input: nothing.
    if (input.command == command::kNone || input.phase == TackledPhase::Closed) {
        return outcome;
    }
    // 2. The struggle: the mounter's power drained by a share of its maximum (Human_SpendPower truncates), then 250
    // unless his struggle still plays.
    // Square or cross as PlayerCmd_IsSquareAny (0xf, 0x11) and PlayerCmd_IsCrossAny (0x12, 0x10) read them; circle as
    // PlayerCmd_IsCircleAny (pressed, tapped or held).
    const CommandId pressed = input.command;
    const bool squareOrCross = pressed == command::kSquarePressed || pressed == command::kSquareChain ||
                               pressed == command::kCrossPressed || pressed == command::kCrossLongHold;
    const bool circle =
        pressed == command::kCirclePressed || pressed == command::kCircleTapped || pressed == command::kCircleHeld;
    if (squareOrCross && mountStruggleAllowed(input)) {
        const float share = 1.0F / static_cast<float>(std::max(1, input.ownStruggleDivisor));
        outcome.mounterPowerCost = static_cast<int>(share * static_cast<float>(input.mounter.powerMax));
        if (input.phase == TackledPhase::Free) {
            outcome.action = TackledAction::Struggle;
            outcome.animId = kMountStruggle;
        }
        return outcome;
    }
    // 3. While the struggle plays nothing else is read.
    if (input.phase == TackledPhase::Struggling) {
        return outcome;
    }
    // 4. Circle breaks out on the roll, unless he may never escape.
    if (circle && !input.noBreak && breakFreeRoll(input.mounter, random)) {
        outcome.action = TackledAction::Break;
        outcome.animId = kMountBreak;
        return outcome;
    }
    // 5. R1 reverses on the roll, unless either flag forbids it.
    const bool reverse = input.command == input.reversalCommand || input.command == command::kGrabSpin;
    if (reverse && !input.mounter.flag40 && !input.noReversal && breakFreeRoll(input.mounter, random)) {
        outcome.action = TackledAction::Reversal;
        outcome.animId = kMountReversal;
    }
    return outcome;
}

MountEnd mountClipEnd(const MountEndInput& input, CombatRandom& random) {
    // The auto-escape flag gets the mounter off at once.
    if (input.autoEscape) {
        return MountEnd::GetOff;
    }
    // Circle (any kind) wins on a coin (Random_Int(2) == 0): with the stick at rest (the per-player record's current
    // magnitude, 0.25 or less) the mount is reversed, with it pushed the mounter is thrown off.
    const CommandId pressed = input.command;
    const bool circle =
        pressed == command::kCirclePressed || pressed == command::kCircleTapped || pressed == command::kCircleHeld;
    if (!circle || random.coin()) {
        return MountEnd::Stay;
    }
    return input.stickMagnitude <= kMountEndStickAtRest ? MountEnd::Reverse : MountEnd::GetOff;
}

} // namespace coney::combat
