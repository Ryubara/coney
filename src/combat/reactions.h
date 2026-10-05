// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "animation/anim_math.h"
#include "combat/stick.h"

// How a human reacts to a hit: the attack's hit code (direction, height and strength) is turned by where the attacker
// stands and how high, the victim's state changes the strength, and the reaction table names the clip; a stun flag on
// the attack stuns, and a held block cancels the damage with a block reaction unless the hit breaks it.
// Research: docs/research/combat.md#hit-codes, docs/research/combat.md#reactions, docs/research/combat.md#block

namespace coney::combat {

/// The Anim Range List flag (`+0x0e`) that makes a hit stun.
inline constexpr std::uint16_t kRangeFlagStun = 0x400;
/// The reaction a missing table entry falls back to (and the light high front one).
inline constexpr int kFallbackReaction = 272;
/// The stun's loop after the reaction (356, seen at runtime; the code names 355 as a stunned reaction's return, not
/// seen) and the clip that ends a stun before the idle returns.
inline constexpr int kStunLoop = 356;
inline constexpr int kStunEnd = 357;
/// The dying fallback outside the `DIE` set.
inline constexpr int kDeathFallback = 292;
/// The `DIE` set lies this many ids after the reaction it replaces (304-315).
inline constexpr int kDeathOffset = 20;

/// A hit code (Anim Range List `+0x0c`) taken apart.
struct HitCode {
    int direction = 2; ///< Bits 0-1: 2 straight, 1 and 3 from a side, 0 from behind.
    int height = 1;    ///< Bits 2-3: 0 low, 1 mid, 2 high.
    int strength = 0;  ///< Bits 4-5: 0 light, 1 medium, 2 heavy, 3 crushing.
};

/// The parts of hit code `code`.
[[nodiscard]] HitCode decodeHitCode(int code);

/// The side of the victim the attacker stands on: Side::Front within 45° of its facing, Side::Rear beyond 135°,
/// otherwise its right or left (the reaction's side 0, 1, 2 and 3). The victim stands at `victim` facing `heading`
/// (radians, 0 along +y, growing anticlockwise from above, as human::facing()); the original's `0x00267338`.
[[nodiscard]] Side victimSide(const anim::Vec3& victim, float heading, const anim::Vec3& attacker);

/// What picking a reaction needs.
struct ReactionInput {
    int attackAnim = -1;        ///< The attacker's anim id (a combo id is 13-20).
    int code = 0;               ///< The attack's hit code.
    Side side = Side::Front;    ///< victimSide().
    float attackerAbove = 0.0F; ///< How much higher the attacker's feet stand, metres (negative: lower).
    bool victimFlag400 =
        false;               ///< The victim's human flag `0x400` (the player has it): combo hits keep their strength.
    bool victimHurt = false; ///< The victim's health is below its power class's hurt fraction.
    bool attackerFlag200000 = false; ///< The attacker's human flag `0x200000`: strength + 1 (see hitReaction()).
    bool victimFlag200 = false;      ///< The victim's human flag `0x200`: strength - 1.
    bool victimFlag80 = false;       ///< The victim's human flag `0x80`: strength at most 1.
};

/// Whether `animId` is a combo id for the reaction's strength rules: 13, 14, 15 and 17 to 20. Both of the original's
/// tests (`0x00266b50`, `0x00266c40`) skip 16 `SS2`.
[[nodiscard]] bool isComboAttack(int animId);

/// The reaction picked, with the modified code it came from.
struct Reaction {
    int animId = kFallbackReaction;
    HitCode code; ///< The strength and height after the modifiers, and the reaction's direction.
};

/// The victim's reaction: the code's strength less 1 when the victim has flag `0x200` or the attack is a combo id
/// (isComboAttack()) at a victim without flag `0x400` that is not hurt; plus 1 when the attacker has flag `0x200000`
/// and the victim `0x400` or the attack is a combo id; at most 1 for a victim with `0x80`; 0 to 3. The height raised 1
/// when the attacker stands 0.3 to 0.9 m higher and 2 when 0.9 to 1.5 m, lowered as much when lower (0 to 2); a low
/// hit is always strength 2; the direction the code's plus the side, modulo 4; the id from the table at
/// `0x00510798`, a missing entry giving 272.
/// Not here: the allies' rule (Coney has no allies yet).
/// @orig 0x0026b0a0 Hit_PickReaction (unknown)
/// @orig 0x00266d00 Hit_PickReaction (unknown)
[[nodiscard]] Reaction hitReaction(const ReactionInput& input);

/// The dying reaction (`0x00266fd8`): the reaction at strength 2, then the `DIE` set (its id + 20) when `dieSet`, else
/// 292.
[[nodiscard]] int deathReaction(const ReactionInput& input, bool dieSet);

/// The block reaction for a hit at `height` from `direction` (the reaction's): mid (and low) 614, 615, 612, 613 and
/// high 610, 611, 608, 609, back, left, front and right.
/// @orig 0x00269f30 Human_BlockHit (unknown)
[[nodiscard]] int blockReaction(int height, int direction);

/// Whether a held block takes the hit: a reaction's strength of 3 and an attacker playing 26 to 34 break it.
/// Not here: the weapon rules (Coney has no weapons yet).
[[nodiscard]] bool blockHolds(int strength, int attackAnim);

} // namespace coney::combat
