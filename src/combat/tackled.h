// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "combat/anim_ids.h"
#include "combat/commands.h"
#include "combat/grabbed.h"
#include "combat/stick.h"

// The human held down in a mount (state 0x800), after a tackle, a grab's mount or the grounded mount: the struggle that
// drains the mounter's power, the break out from under it and the reversal that puts him on top. A pure decision over
// what the mounter shows; the mounter plays both sides' clips and applies what it is told.
// Research: docs/research/combat-moves.md#mounted-victim, docs/research/combat-reactions.md#ground-escape

namespace coney::combat {

/// The grounded mount's first clip (`MOUNT_GROUNDED`, from the front); 238 right, 239 back, 240 left
/// (`Mount_StartOnGrounded` 0x00271b30: `Human_GetSideOf(victim, attacker)` + 0xed).
inline constexpr int kGroundedMountFront = 237;
/// `MOUNT_GROUNDED_REACT`: the downed human's side of the grounded mount.
inline constexpr int kGroundedMountReact = 241;
/// `MOUNT_STRUGGLE`: the mounted human's struggle strike (the mounter plays 251).
inline constexpr int kMountStruggle = 250;
/// `MOUNT_BREAK`: the mounted human breaks out (the mounter plays 247); both end in the fight stance.
inline constexpr int kMountBreak = 246;
/// `MOUNT_REVERSAL`: the mounted human throws the mounter over (it plays 243); then the roles swap.
inline constexpr int kMountReversal = 242;

/// The last of the grounded mount's clips: 237 + the side, front, right, back, left.
inline constexpr int kGroundedMountLeft = 240;

/// What the mounted human's own clips allow this update, by the bits they hold (the record's `+0x08`).
enum class TackledPhase : std::uint8_t {
    Free,       ///< The mounted idle 207: every input is read.
    Struggling, ///< His struggle 250 plays (`0x1`): a square or cross still drains the mounter, nothing starts.
    Closed,     ///< Another of his clips plays (the mount's start, a strike's reaction): nothing is read (`0x5c7eee0`).
};

/// What the mounted human did.
enum class TackledAction : std::uint8_t {
    None,
    Struggle, ///< Square or cross: 250 (the mounter 251), the mounter drained and struck.
    Break,    ///< Circle and the roll won: 246 / 247, both free (`Mount_GetOff`).
    Reversal, ///< R1 and the roll won: 242 / 243, then he mounts the mounter (`Mount_StartReversal`).
};

/// One update's input to the mounted human.
struct TackledInput {
    CommandId command = command::kNone;
    TackledPhase phase = TackledPhase::Free;
    bool hurt = false;          ///< Below his class's hurt fraction of health.
    int ownPower = 0;           ///< His power meter (record `+0x148`).
    int ownPowerMax = 1;        ///< Its maximum (at least 1).
    int ownStruggleDivisor = 3; ///< His power class byte `+0x36`: the share of the mounter's power one struggle takes.
    bool noBreak = false;       ///< His human flag `0x2000000000` (`HuSetNoEscape`): circle never breaks out.
    bool noReversal = false;    ///< His human flag `0x100000000`: R1 never reverses.
    /// The command the reversal takes besides `0x19` (`0x00510988`, R1 pressed in the street's tables).
    CommandId reversalCommand = command::kR1Pressed;
    GrabberState mounter; ///< The mounter's numbers this update.
};

/// What the update decided.
struct TackledOutcome {
    TackledAction action = TackledAction::None;
    int animId = anim_id::kNone; ///< His clip (the mounter plays animId + 1, from his set).
    int mounterPowerCost = 0;    ///< Power the mounter loses (a struggle's, spent even with no clip).
};

/// Whether square or cross struggles (`Grab_CanStruggle` 0x002258f0, then `Player_UpdateTackled`'s own test): he is
/// not hurt, the mounter does not rage, his power is above a sixth of his maximum, and his power's share of it is above
/// 1 / the mounter's divisor. Grab_CanStruggle's test is read inline, as combat::struggleAllowed() reads it.
/// @orig 0x0027f1a0 Player_UpdateTackled (unknown)
[[nodiscard]] bool mountStruggleAllowed(const TackledInput& input);

/// One update of the mounted human: square or cross struggles (the mounter loses its maximum / his divisor, truncated,
/// even while 250 plays, when no new 250 starts); circle breaks out on the mounter's break-free roll (breakFreeRoll())
/// unless he has the no-escape flag; R1 (or `0x19`) reverses on the roll unless the mounter has flag `0x40` or he the
/// block-tackle flag. Nothing is read while a clip of his other than the struggle plays, and only the struggle's drain
/// while it does.
/// @orig 0x0027f1a0 Player_UpdateTackled (unknown)
[[nodiscard]] TackledOutcome updateTackled(const TackledInput& input, CombatRandom& random);

} // namespace coney::combat
