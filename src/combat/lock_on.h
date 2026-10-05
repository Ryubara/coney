// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "combat/combat_tuning.h"

// Moving in a fight: when the fight stance's movement locks onto the target (it then faces the target every update
// and the stick walks it without turning, picking the combat-walk clip by the stick's angle), when the target is
// dropped, and how the stick turns a grabbing player and the victim it holds.
// Research: docs/research/combat.md#targets, docs/research/combat.md#grab-turn

namespace coney::combat {

/// The first of the eight combat-walk clips (380 forward, then clockwise every 45°: 381 forward right ... 387 forward
/// left).
inline constexpr int kCombatWalkFirst = 380;

/// Whether a human in a fight stance is locked onto its target: it has one, and L1 is held with `CfgLockOn` on, or
/// `CfgAutoLock`, `CfgAutoLockAndCombat` or auto-combat is on (CombatTuning).
[[nodiscard]] bool lockedOn(const CombatTuning& tuning, bool hasTarget, bool l1Held);

/// The combat-walk clip for the stick's angle from the facing, clockwise in degrees (record `+0xdc`).
/// **Coney's choice**: eight even 45° sectors centred on the clips' directions (at runtime 348° still gave 387).
[[nodiscard]] int combatWalkClip(float clockwiseDegrees);

/// Whether a target `distance` metres away is kept: within CombatTuning::targetDropDistance, or always while L1 holds
/// it (record `+0x00` `0x8`) or a grab or tackle holds it (`0x4`).
[[nodiscard]] bool keepsTarget(const CombatTuning& tuning, float distance, bool held);

/// The grabbing player's turn this update, radians (at most the remaining angle `remaining`, signed): with `max`
/// CombatTuning::grabTurnMax, `r = min(max, max × (1 − cos(|remaining| × π / 4)) / 2 + previous × k)`, `k`
/// CombatTuning::grabTurnCarry, or CombatTuning::grabTurnReverseCarry when the turn reverses within 45°. `previous`
/// is the last update's turn (signed). The grab's movement (Fighter::moveGrab()) applies it.
[[nodiscard]] float grabTurnStep(const CombatTuning& tuning, float remaining, float previous);

} // namespace coney::combat
