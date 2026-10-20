// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

// The revive's rules as numbers and yes-or-no answers, kept apart from the humans and the game state so they can be
// tested on their own: whether a reviver may revive a downed partner (`Player_TryRevive`) and whose flash pays for it
// (`Revive_Finish`). The game mode gathers the facts and plays the clips (gamemodes/gameplay_revive.cpp).
// Research: docs/research/combat-rage.md#revive

namespace coney::combat {

/// The reviver's clips (docs/research/combat-rage.md#revive): 351 `gen_revive_begin` (its end spends the flash and
/// heals), then 353 `gen_revive_end`; the revived gets up with 354 `gen_revive`. 352 `gen_revive_cycle` is never
/// played.
inline constexpr std::uint32_t kReviveBeginAnim = 351;
inline constexpr std::uint32_t kReviveEndAnim = 353;
inline constexpr std::uint32_t kRevivedAnim = 354;
/// The record bits the reviver's clips hold (`0x8000000`) and the revived's 354 holds (`0x8000`).
inline constexpr std::uint32_t kReviverHeld = 0x8000000;
inline constexpr std::uint32_t kRevivedHeld = 0x8000; ///< The same bit as the rise's `kPhaseRise` (attacks.h).
/// The fade the reviver's chain starts with: a blend over 0.01 s.
inline constexpr float kReviveFadeSeconds = 0.01F;
/// The speech commands (docs/references/speech.md): the reviver's 76 `revive_reasure` as he starts, the revived's
/// 75 `revive_thank` as he is healed.
inline constexpr std::uint32_t kReviveReassureCommand = 76;
inline constexpr std::uint32_t kReviveThankCommand = 75;
/// The interface cue of a revive completed: 23, the flash's (`vags/misc/flash`).
inline constexpr int kReviveSoundCue = 23;

/// What `Player_TryRevive` reads of the reviver and of the downed human `Human_FindRevivableNear` found.
struct ReviveFacts {
    bool reviverHeld = false;      ///< The reviver holds any held flag on his record (`+0x08`).
    bool reviverBlocked = false;   ///< The reviver's actions are blocked (state mask `0x7bf9e9f7ff0`).
    bool found = false;            ///< `Human_FindRevivableNear` found a downed human.
    bool downedHeld = false;       ///< The downed human holds any held flag.
    bool wayClear = true;          ///< `Human_IsWayClearTo(reviver, downed)`.
    bool downedIsPlayer = false;   ///< The downed human is a player (his `+0x1b0` is not −1).
    bool reviverIsWarrior = false; ///< The reviver's brain is type 3, a Warrior.
    bool reviverHasFlash = false;  ///< The reviver's panel inventory holds item 1.
    bool downedHasFlash = false;   ///< The downed human's panel inventory holds item 1 (only a co-op partner has one).
};

/// Whether the revive may start (`Player_TryRevive`, confirmed (code)): refused while the reviver holds anything or
/// is blocked, with nobody found, while the downed human holds anything; with the way not clear, a downed AI human is
/// refused and a downed player accepts only a Warrior; and one of the two must carry a flash.
/// @orig 0x00286ba8 Player_TryRevive (unknown)
[[nodiscard]] bool mayRevive(const ReviveFacts& facts);

/// Whose flash a finished revive spends.
enum class ReviveFlashFrom : std::uint8_t { Downed, Reviver };

/// `Revive_Finish`'s choice: the downed human's flash when his inventory has one, else the reviver's.
[[nodiscard]] constexpr ReviveFlashFrom reviveFlashFrom(bool downedHasFlash) {
    return downedHasFlash ? ReviveFlashFrom::Downed : ReviveFlashFrom::Reviver;
}

} // namespace coney::combat
