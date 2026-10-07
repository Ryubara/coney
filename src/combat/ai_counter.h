// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "animation/anim_math.h"
#include "combat/anim_ids.h"

// The AI's counter (command 3): a human no pad drives presses it while another human comes in for a grab or a
// tackle at it, and the dispatcher answers with the counter as a paired move: 76 `GRAB_FRONT_COUNTER` against a grab's
// intro (69-71), 9 `TACKLE_FRONT_COUNTER` against a tackle's (2-4); the attacker plays the next clip (77, 10). These
// are the pure tests; the fighters play the pair (human::Fighter).
// Research: docs/research/ai.md#block, docs/research/combat-moves.md#counters

namespace coney::combat {

/// The counters and the attacker's side of each (its id + 1).
inline constexpr int kAiGrabCounter = 76;  ///< `GRAB_FRONT_COUNTER`.
inline constexpr int kAiTackleCounter = 9; ///< `TACKLE_FRONT_COUNTER`.
/// The record `+0x08` bits that refuse the command (`0x0027d6e0`): an attack's phases, the grab bit, the duck.
inline constexpr std::uint32_t kAiCounterRefusingPhases = 0x0100101f;
/// The record `+0x08` bits the counterer must have none of to be free (`0xfc7eaf7`).
inline constexpr std::uint32_t kAiCounterBusyPhases = 0x0fc7eaf7;

/// What the human pressing command 3 is this update.
struct AiCounterSide {
    bool padControlled = false;   ///< Per-player `+0x1b`: a pad's human never takes command 3.
    bool standing = true;         ///< On its feet and in no hold (its state has none of `0x7bf9e9f4300`).
    std::uint32_t phaseFlags = 0; ///< Its record `+0x08`.
    bool holdingObject = false;   ///< Something in hand (`0x00231a38`).
    bool hurt = false;            ///< Below its class's hurt fraction (`0x00222ff8`).
};

/// Whether the human may counter at all this update: not a pad's, on its feet, free (record `+0x08` none of
/// kAiCounterRefusingPhases or kAiCounterBusyPhases), empty-handed and not hurt.
/// @orig 0x0027d6e0 Player_TryCounterGrab (unknown)
[[nodiscard]] bool aiCounterAllowed(const AiCounterSide& side);

/// The counter that answers an attacker playing anim `attackerAnim`: kAiGrabCounter during a grab's intro or miss
/// (69-71, `0x00258e88`), kAiTackleCounter during a tackle's (2-4, `0x002590f8`); kNone for any other clip.
/// @orig 0x00258e88 Human_CanCounterGrab (unknown)
/// @orig 0x002590f8 Human_CanCounterTackle (unknown)
[[nodiscard]] int aiCounterFor(int attackerAnim);

/// Whether two humans face each other (`0x002672d0` false both ways, **inferred** on the page): each stands within
/// the other's front quarter (combat::victimSide()).
[[nodiscard]] bool faceToFace(anim::Vec3 a, float headingA, anim::Vec3 b, float headingB);

} // namespace coney::combat
