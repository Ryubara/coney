// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "characters/anim_set.h"

// How a target limits who swings at him: the active-attacker places (four, of which only the spacing byte's first
// are used), the spacing bytes a gang's `CfgGang` values raise, and the gap after each attack (one swing's length
// shared among those allowed to swing). Pure books; the fight goals keep them per brain.
// Research: docs/research/ai.md#attack-places

namespace coney::ai {

/// The swing time of a kind with no clip of its own, ms (`Attack_GetNextAttackDelay`'s default).
inline constexpr int kDefaultSwingMs = 100;
/// The active-attacker places a target has (`+0x1f0`).
inline constexpr std::size_t kActivePlaces = 4;

/// The anims whose playing time is a kind's swing, and whether the time is their sum (a chain of two clips) or the
/// longest of them (alternatives).
struct SwingClips {
    std::array<std::uint32_t, 4> ids{};
    std::size_t count = 0;
    bool sum = false;
};

/// The clips that time attack kind `kind` (`Attack_GetNextAttackDelay`, `0x00231590`): 11-21 for the strikes 0-9 (by
/// the kind's anim), 25 for the snap, 21 for 11, 193 / 194 for 12 / 13, 237 for 14, 664 for 15, the longer of 653 /
/// 655 for 16 and of 657 / 659 for 17, 0 / 1 for 19 / 20, 3 + 5 for 21, 70 + 72 for 22, 466 + 467 for 23, the throws
/// for 25 and 29, the power strikes 57, 59, 61 for 26-28, 118 for 30, the struggle and escape clips for 31-34, 225
/// for 35-39, 248, 252, 246, 242 for 40, 41, 43, 44; none for the rest. Kind 0 against a downed target is 194, kind 1
/// none. **Coney readings**: the page's "the throw clips for 25-29" is taken as the throws for 25 and 29 and the power
/// strikes (`attacks.md`) for 26-28; 31 is its struggle strikes (96, 108) and 32-34 the escapes (100, 112); the bat
/// and bottle sets' own clips are not used.
[[nodiscard]] SwingClips swingClipsOf(int kind, bool targetDown);

/// The swing time of `kind` in ms: the playing time (duration / rate) of swingClipsOf()'s clips in `anims`, summed or
/// the longest; kDefaultSwingMs when there are none or none is in the set.
/// @orig 0x00231590 Attack_GetNextAttackDelay (unknown)
[[nodiscard]] int swingTimeMs(int kind, bool targetDown, const characters::AnimSet& anims);

/// The gap a target gets after an attack (`Brain_SetAttackableTime`'s `t`): the swing time × 1.0 / `spacing`,
/// rounded; unscaled when `spacing` is 0.
/// @orig 0x00290e78 Brain_SetAttackableTime (unknown)
[[nodiscard]] int attackableGapMs(int swingMs, int spacing);

/// A target's spacing bytes (`+0x14a` standing, `+0x14b` down): how many may swing at him at once.
struct Spacing {
    int standing = 1; ///< `+0x14a`.
    int down = 1;     ///< `+0x14b`.

    /// An attacker of a gang whose `CfgGang` values 2 and 3 are `gangStanding` and `gangDown` took a slot on him: each
    /// rises to at least the gang's; a Warrior target (`targetIsWarrior`) keeps `standing` at 1.
    void raise(int gangStanding, int gangDown, bool targetIsWarrior);
    /// His slot list emptied: both back to 1.
    void reset() { standing = down = 1; }
    /// The byte in use: `down` while he is down, out of the fight or arrested (state any of `0xe0000`).
    [[nodiscard]] int inUse(bool downOrOut) const { return downOrOut ? down : standing; }
};

/// A target's active-attacker places (`+0x1f0`): the attackers about to swing at him. Holders are named by an opaque
/// id (the attacker's brain).
class ActivePlaces {
  public:
    /// Whether `attacker` holds a place.
    [[nodiscard]] bool holds(const void* attacker) const;
    /// How many places are held.
    [[nodiscard]] std::size_t held() const;
    /// `attacker` asks for a place among the first `spacing` (`Brain_ClaimActiveAttacker`): it keeps one it has; else
    /// it takes a free one when the target's next-attack time `attackable` has passed or someone already holds one;
    /// else, as the target's own target (`targetsBack`) once that time has passed, it displaces the first holder.
    /// Returns whether it holds one now.
    /// @orig 0x00291008 Brain_ClaimActiveAttacker (unknown)
    bool claim(const void* attacker, int spacing, bool attackable, bool targetsBack);
    /// `attacker` gives its place up (`Brain_ReleaseActiveAttacker`).
    /// @orig 0x00291178 Brain_ReleaseActiveAttacker (unknown)
    void release(const void* attacker);
    /// Every place freed.
    void clear() { m_places.fill(nullptr); }

  private:
    std::array<const void*, kActivePlaces> m_places{};
};

} // namespace coney::ai
