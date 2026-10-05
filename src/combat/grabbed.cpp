// SPDX-License-Identifier: GPL-3.0-or-later
#include "combat/grabbed.h"

#include <algorithm>
#include <cmath>

namespace coney::combat {

namespace {

// The struggle needs more than this share of the player's own power.
constexpr float kStruggleOwnPower = 1.0F / 6.0F;
// The break-free threshold is this share of the grabber's maximum, or the hurt share when it is hurt.
constexpr float kBreakShare = 0.25F;
constexpr float kBreakShareHurt = 0.5F;

} // namespace

bool struggleAllowed(const GrabbedInput& input) {
    const GrabberState& grabber = input.grabber;
    const float grabberFraction = static_cast<float>(grabber.power) / static_cast<float>(std::max(1, grabber.powerMax));
    return !input.hurt && !grabber.raging && input.ownPowerFraction > kStruggleOwnPower &&
           grabberFraction > 1.0F / static_cast<float>(std::max(1, grabber.struggleDivisor));
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
        if (!input.movePlaying) {
            outcome.action = GrabbedAction::StrikeBack;
            outcome.animId = rear ? kGrabStrikeBackRear : kGrabStrikeBackFront;
        }
        break;
    case command::kCirclePressed:
    case command::kCircleTapped:
    case command::kCircleHeld:
        if (!input.movePlaying && breakFreeRoll(input.grabber, random)) {
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
