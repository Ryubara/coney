// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "combat/anim_ids.h"
#include "combat/commands.h"
#include "combat/stick.h"

// The player held in another human's grab: the struggle strike that costs the grabber power, the strike back, the
// escape and the reversal (both a roll against the grabber's power), and the counter at the catch. These are pure
// decisions over what the grabber shows; the human plays the clips and the grabber applies what it is told.
// Research: docs/research/combat.md#grabbed

namespace coney::combat {

/// The grabbed player's clips, front then rear; the grabber plays each id + 1 (docs/research/combat.md#grabbed).
inline constexpr int kGrabStruggleFront = 96;
inline constexpr int kGrabStruggleRear = 108;
inline constexpr int kGrabStrikeBackFront = 104;
inline constexpr int kGrabStrikeBackRear = 116;
inline constexpr int kGrabEscapeFront = 100;
inline constexpr int kGrabEscapeRear = 112;
inline constexpr int kGrabReversalFront = 90;
inline constexpr int kGrabReversalRear = 92;
inline constexpr int kGrabFrontCounter = 76;
/// The grabbed victim's let-go clip (the grabber plays 95).
inline constexpr int kGrabLetGoVictim = 94;

/// What the grabbed player's struggle reads from its grabber this update.
struct GrabberState {
    int power = 0;           ///< Its power meter.
    int powerMax = 1;        ///< Its maximum (at least 1).
    bool hurt = false;       ///< Below its class's hurt fraction of health.
    bool raging = false;     ///< Raging: no struggle, escape or reversal works.
    bool flag40 = false;     ///< Human flag `0x40`: it cannot be reversed.
    int struggleDivisor = 4; ///< Its power class byte `+0x36`.
};

/// What the grabbed player did.
enum class GrabbedAction : std::uint8_t {
    None,
    Struggle,   ///< Square: 96 / 108, costing the grabber power.
    StrikeBack, ///< Cross: 104 / 116, damaging the grabber.
    Escape,     ///< Circle and the roll won: 100 / 112; the grabber is knocked down and stunned.
    Reversal,   ///< R1 and the roll won: 90 / 92, then the player holds the grabber from the rear.
};

/// One update's input to the grabbed player.
struct GrabbedInput {
    CommandId command = command::kNone;
    bool fromRear = false;      ///< Held from behind.
    bool hurt = false;          ///< The player is hurt.
    float ownPowerFraction = 1; ///< The player's power over its maximum.
    int ownStruggleDivisor = 3; ///< The player's power class byte `+0x36`.
    bool movePlaying = false;   ///< One of the player's grabbed moves is still playing: no new one starts.
    GrabberState grabber;
};

/// What the update decided.
struct GrabbedOutcome {
    GrabbedAction action = GrabbedAction::None;
    int animId = anim_id::kNone; ///< The player's clip (the grabber plays animId + 1).
    int grabberPowerCost = 0;    ///< Power the grabber loses (the struggle's, spent even with no clip).
};

/// Whether the player may struggle (`0x002258f0`): not hurt, the grabber not raging, its own power above a sixth of
/// its maximum, and the grabber's power fraction above 1 / the grabber's struggle divisor.
[[nodiscard]] bool struggleAllowed(const GrabbedInput& input);

/// Whether a break-free roll succeeds (`0x00225830`): never against a raging grabber; with `t` a quarter of the
/// grabber's maximum (half when it is hurt) and `p` its power, always when `p` ≤ `t`, otherwise when a random number
/// below `floor(p / t)` is 0.
[[nodiscard]] bool breakFreeRoll(const GrabberState& grabber, CombatRandom& random);

/// One update of the grabbed player: square struggles (its cost, the grabber's maximum / the player's divisor,
/// spent even when a move is still playing), cross strikes back, circle escapes on a winning roll, R1 pressed reverses
/// on one when the grabber lacks flag `0x40`.
/// @orig 0x0027fd68 Player_UpdateGrabbed (unknown)
[[nodiscard]] GrabbedOutcome updateGrabbed(const GrabbedInput& input, CombatRandom& random);

/// Whether a grab whose intro ends this update becomes the player's counter (76, grabber 77): the counter is on
/// (`0x00510254`, 1 in the street) and this update's command is R1 pressed.
/// @orig 0x0026c1d8 Grab_IntroEnd (unknown)
[[nodiscard]] bool counterAtCatch(CommandId command, bool countersOn);

} // namespace coney::combat
