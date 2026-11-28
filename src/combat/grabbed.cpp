// SPDX-License-Identifier: GPL-3.0-or-later
#include "combat/grabbed.h"

#include <algorithm>
#include <cmath>

namespace coney::combat {

namespace {

// The struggle needs more power than the player's maximum over this (`Grab_CanStruggle`).
constexpr int kStruggleOwnShare = 6;
// The break-free threshold is this share of the grabber's maximum, or the hurt share when it is hurt.
constexpr float kBreakShare = 0.25F;
constexpr float kBreakShareHurt = 0.5F;

} // namespace

bool canStruggle(const GrabbedInput& input) {
    return !input.hurt && !input.grabber.raging && input.ownPower > std::max(1, input.ownPowerMax) / kStruggleOwnShare;
}

bool struggleAllowed(const GrabbedInput& input) {
    // The player's own fraction, not the grabber's, against the grabber's class divisor.
    const float ownFraction =
        input.ownPowerFull ? 1.0F
                           : static_cast<float>(input.ownPower) / static_cast<float>(std::max(1, input.ownPowerMax));
    return canStruggle(input) && ownFraction > 1.0F / static_cast<float>(std::max(1, input.grabber.struggleDivisor));
}

bool breakFreeRoll(const GrabberState& grabber, CombatRandom& random) {
    if (grabber.raging) {
        return false;
    }
    const float threshold =
        static_cast<float>(std::max(1, grabber.powerMax)) * (grabber.hurt ? kBreakShareHurt : kBreakShare);
    if (static_cast<float>(grabber.power) <= threshold) {
        return true;
    }
    // A chance of 1 / floor(p / t).
    const auto odds = static_cast<std::uint32_t>(std::floor(static_cast<float>(grabber.power) / threshold));
    return odds <= 1 || (random.next() >> 8U) % odds == 0;
}

GrabbedOutcome updateGrabbed(const GrabbedInput& input, CombatRandom& random) {
    GrabbedOutcome outcome;
    const bool rear = input.fromRear;
    switch (input.command) {
    case command::kSquarePressed:
    case command::kSquareChain:
        // The cost is the grabber's maximum over the player's divisor, spent even when no clip can start.
        if (!struggleAllowed(input)) {
            break;
        }
        outcome.grabberPowerCost = static_cast<int>(std::lround(
            static_cast<float>(input.grabber.powerMax) / static_cast<float>(std::max(1, input.ownStruggleDivisor))));
        if (!input.movePlaying) {
            outcome.action = GrabbedAction::Struggle;
            outcome.animId = rear ? kGrabStruggleRear : kGrabStruggleFront;
        }
        break;
    case command::kCrossPressed:
    case command::kCrossLongHold:
        if (!input.movePlaying && canStruggle(input)) {
            outcome.action = GrabbedAction::StrikeBack;
            outcome.animId = rear ? kGrabStrikeBackRear : kGrabStrikeBackFront;
        }
        break;
    case command::kCirclePressed:
    case command::kCircleTapped:
    case command::kCircleHeld:
        if (!input.movePlaying && !input.noEscape && breakFreeRoll(input.grabber, random)) {
            outcome.action = GrabbedAction::Escape;
            outcome.animId = rear ? kGrabEscapeRear : kGrabEscapeFront;
        }
        break;
    case command::kR1Pressed:
        if (!input.movePlaying && !input.grabber.flag40 && breakFreeRoll(input.grabber, random)) {
            outcome.action = GrabbedAction::Reversal;
            outcome.animId = rear ? kGrabReversalRear : kGrabReversalFront;
        }
        break;
    default:
        break;
    }
    return outcome;
}

bool counterAtCatch(CommandId command, bool countersOn) { return countersOn && command == command::kR1Pressed; }

} // namespace coney::combat
