// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <optional>

#include "animation/anim_clip.h"
#include "combat/commands.h"
#include "combat/reactions.h"
#include "combat/stick.h"

// The victim's side of a hit, as `Human_ApplyPendingDamage` takes it each update: a held block first (a duck the
// attacker's clip announced lets the attack pass over), the health floor of a player, the hit armour of a player
// winding up or in a chain window, and the knocked-down human's mashing. Pure decisions; the human plays the clips.
// Research: docs/research/combat.md#damage, docs/research/combat.md#block, docs/research/combat.md#reactions,
// docs/research/combat.md#being-hit-runtime

namespace coney::combat {

/// The clip a blocker ducks with when the attacker's clip announces the attack (616 `BLOCK_DODGE`).
inline constexpr int kBlockDodge = 616;
/// The anim event types in an attacker's clip that warn its target (`0x00101dd8`).
inline constexpr std::uint16_t kDuckEvent = 0x24;
inline constexpr std::uint16_t kEarlyBlockEvent = 0x26;
/// A warning reaches a target within this many times the reach of the attacker's anim.
inline constexpr float kWarningReachScale = 2.0F;

/// What an attacker's clip tells its target before the hit.
enum class AttackWarning : std::uint8_t {
    Duck,       ///< Event 0x24, message 0xa4: a blocking target ducks (616) and the attack passes over it.
    EarlyBlock, ///< Event 0x26, message 0xa6: a blocking target plays its block reaction before the hit.
};

/// The warning an anim event of `type` sends, if any.
[[nodiscard]] std::optional<AttackWarning> warningOf(std::uint16_t type);

/// The first warning event of `clip` whose time (its frame / 30 s) falls in (`fromSeconds`, `toSeconds`] of clip time
/// (from `fromSeconds` < 0 the clip's start counts), or nothing.
/// @orig 0x00101dd8 Anim_FireEvents (unknown)
[[nodiscard]] std::optional<AttackWarning> warningBetween(const anim::AnimClip& clip, float fromSeconds,
                                                          float toSeconds);

/// The event type in the duck's own clip (616, frames 6-13) that opens its counter window (message `0xa5`).
inline constexpr std::uint16_t kDuckCounterEvent = 0x25;
/// The duck counters, by where the target stands: 617 front, 618 right, 619 back, 620 left
/// (`BLOCK_COUNTER_FRONT` ... `_LEFT`).
inline constexpr int kDuckCounterFront = 617;
/// The counter keeps a target within this many times the reach of 617.
inline constexpr float kDuckCounterReachScale = 1.25F;

/// Whether an event of `type` in `clip` fires in (`fromSeconds`, `toSeconds`] of clip time (its frame / 30 s).
[[nodiscard]] bool eventBetween(const anim::AnimClip& clip, std::uint16_t type, float fromSeconds, float toSeconds);

/// Whether `command` asks for the duck's counter (`0x0027b988`): square or cross pressed or held (`0xf`, `0x11`,
/// `0x15`, `0x10`, `0x12`, `0x16`).
[[nodiscard]] bool asksDuckCounter(CommandId command);

/// The duck counter for a target standing on `side` of the player (front 617, right 618, back 619, left 620).
[[nodiscard]] int duckCounterClip(Side side);

/// The attack ids that ignore hit armour whoever plays them (617-620).
[[nodiscard]] bool armourBreakingAttack(int animId);

/// Whether a player victim's hit armour holds (`Human_ApplyPendingDamage` step 5): its own attack is in the wind-up
/// (`+0x08` `0x1`) or the chain window (`0x2`), and the attacker does not ignore armour (a player, raging, flag
/// `0x200000` or `0x4000`: `attackerIgnoresArmour`) and is not playing 617-620. The damage lands; no reaction plays.
[[nodiscard]] bool hitArmourHolds(std::uint32_t attackPhase, int attackAnim, bool attackerIgnoresArmour);

/// The damage a hit really takes off a victim at `health` of `maximum` with the health floor (human flag
/// `0x20000000000`, the player's): one hit cannot take health below `floorFraction` of the maximum. **Coney's
/// reading**: the floor stops a hit that starts above it; a victim already at or below it takes the whole hit.
[[nodiscard]] int flooredDamage(int health, int maximum, int damage, float floorFraction);

/// What a held block does with a hit (`0x00269f30`).
struct BlockResult {
    bool holds = false; ///< The block cancels all the damage.
    int reaction = -1;  ///< The block reaction it plays (by the reaction's height and direction).
};

/// The block's answer to a hit picked as `input` (the reaction's rules), from attack `attackAnim`: it holds below a
/// modified strength of 3 and against attacks other than 26-34, playing the block table's reaction.
/// Not here: the weapon rules (Coney has no weapons yet). The table is blockReaction()'s.
[[nodiscard]] BlockResult blockHit(const ReactionInput& input);

/// How much one command made while lying down cuts from the ground time (`0x00256a60`): `groundMs` / 1, 2 or 3 at
/// random (**Coney's choice**: the three evenly).
[[nodiscard]] int mashCut(int groundMs, CombatRandom& random);

} // namespace coney::combat
