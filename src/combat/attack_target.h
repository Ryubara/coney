// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "combat/anim_ids.h"

// Which human each attack goes for: whether it keeps the current target or searches afresh, and with which search and
// range. The searches themselves (Player_PickTarget, Player_FindAttackTarget) walk the humans in the fighter
// (human::Fighter); this is their choice by move.
// Research: docs/research/combat-moves.md#targeting

namespace coney::combat {

/// The range of `Player_PickTarget` square and cross search with unarmed (metres).
inline constexpr float kAttackPickRange = 2.0F;
/// The range armed square and cross search with (metres).
inline constexpr float kArmedPickRange = 2.5F;
/// Square in the stance keeps a current target up to this far (metres, `0x005104c0` = 9 squared).
inline constexpr float kKeepTargetRange = 3.0F;

/// How an attack finds its target.
enum class TargetSearch : std::uint8_t {
    /// The current target, searched afresh (PickTarget at AttackTarget::range) when there is none or it is beyond
    /// kKeepTargetRange: square in the stance, and square's chain steps (which keep the target).
    KeepCurrent,
    /// A fresh `Player_PickTarget(range)`: square's moving attacks, cross, the armed swings.
    Pick,
    /// `Player_FindAttackTarget` at the far range of AttackTarget::farId: a cross chain step (when not locked), the
    /// special and the strong grapple.
    FindAttack,
    /// The snap's own search (done before the attack starts).
    Snap,
};

/// What attack `animId` searches for.
struct AttackTarget {
    TargetSearch search = TargetSearch::Pick;
    float range = kAttackPickRange; ///< PickTarget's range (Pick, KeepCurrent).
    int farId = anim_id::kNone;     ///< FindAttack: the id whose far range is searched.
};

/// What `animId` searches for: `cross` when cross started it (else square, a chain step or a command), `armed` with a
/// knife, baton or bat in hand (sets 1-3), `chainStep` when the chain's buffer played it, `locked` when the fighter is
/// locked onto its target. The rules (docs/research/combat-moves.md#targeting): square in the stance keeps the current
/// target (re-picking within 2.0 m without one or beyond 3 m); square's moving attacks and every cross attack pick
/// afresh at 2.0 m; armed swings pick at 2.5 m; a cross chain step (SX2, XX2, SSX3) re-searches with
/// FindAttackTarget at its own far range unless locked, a square step keeps the target; the snaps search their own way;
/// the special 653 / 645 searches at the far range of id 0 (the charge).
/// @orig 0x00286cc8 Player_Square (unknown)
/// @orig 0x00287a18 Player_Cross (unknown)
[[nodiscard]] AttackTarget attackTarget(int animId, bool cross, bool armed, bool chainStep, bool locked);

} // namespace coney::combat
