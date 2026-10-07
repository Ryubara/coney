// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <initializer_list>
#include <optional>

#include "ai/attack_choice.h"

// The attack choice's views filled from two of Coney's humans, and the pick an AI fighter makes with them. Coney's
// humans have no state word: each view's flag is read from what Coney keeps (the fighter's mode and holds, its
// victim's ground and stun, the animator's held flags), and the flags with no Coney counterpart are false.
// Research: docs/research/ai.md#pick-attack

namespace coney::ai {

class Brain;

/// A's view (`Human_CanUseAttackKind`'s attacker) against `target` (null for none). The snap's human is the nearest in
/// sector 2-6 of A's sector record (refreshed when older than kSectorAgeMs) who holds an attack slot on A.
/// **Coney stand-ins**: "free" is on its feet in no pair and not reacting; no world objects, spray paint or cuffs are
/// held.
[[nodiscard]] AttackerView attackerViewOf(Brain& attacker, const Brain* target);

/// T's view against `attacker`. **Coney stand-ins**: `Human_IsHighOrBusy` and held flags `0x400000` / `0x40` are
/// false; the specials' and the grab's refusing states are "not on its feet or in a pair".
[[nodiscard]] TargetView targetViewOf(const Brain& target, const Brain& attacker);

/// The draw's context for A against T.
[[nodiscard]] PickContext pickContextOf(const Brain& attacker, const Brain& target);

/// The distance within which A can start `kind` at a target: the grab's or the tackle's search range for 22 and 21
/// (the dispatcher's own, so the press finds him), else ai::attackReach().
[[nodiscard]] float kindReach(const Brain& attacker, int kind);

/// `Human_CanStartAttack`'s guard for A on T with `kind`.
[[nodiscard]] StartGuard startGuardOf(const Brain& attacker, const Brain& target, int kind);

/// Whether Coney's dispatcher takes `kind`'s command from an AI. **Coney stand-in**: the kinds whose commands reach
/// no handler yet (11 `0x36`, 12 / 13 `0x37` / `0x38`, 15 `0x24`, 23 `0x39`, 34 / 44 `0x19`, 36 `5`, 41 `0x31`) and
/// the `SSS3` holds (8, 9: `0x14`, `0x13`) are left out of every pick.
[[nodiscard]] bool dispatchable(int kind);

/// What filter a pick runs: `Human_CanUseAttackKind` or `Human_CanStartAttack`.
enum class PickFilter : std::uint8_t { CanUse, CanStart };

/// `Brain_PickAttack(B, T, filter)` for `attacker` against `target`, with the brain's weights; none for 45.
[[nodiscard]] std::optional<int> pickAttackFor(Brain& attacker, const Brain& target, PickFilter filter);
/// The same draw with `weights` in place of the brain's (the block's punishing table).
[[nodiscard]] std::optional<int> pickAttackFor(Brain& attacker, const Brain& target, PickFilter filter,
                                               const AttackWeights& weights);

/// The human holding `target`'s human from the rear (state `0x20`): the brain among its attack slots whose fighter
/// holds him from behind; null when none does. **Coney stand-in**: a grabber always holds a slot on his victim.
[[nodiscard]] const Brain* rearGrabberOf(const Brain& target);

/// Whether `target`'s human is held from the rear, by a brain (rearGrabberOf()) or as the player in an AI's grab.
[[nodiscard]] bool grabbedFromRear(const Brain& target);

/// Whether `who` is the nearest human in one of `sectors` of `owner`'s sector record, refreshed when older than
/// kSectorAgeMs (docs/research/ai.md#neighbour-sectors).
[[nodiscard]] bool nearestIn(Brain& owner, const Brain& who, std::initializer_list<int> sectors);

/// Whether `attacker` is behind `target`: the nearest human in sector 3, 4 or 5 of T's record (`Brain_CheckAttack`'s
/// grab row).
[[nodiscard]] bool behind(const Brain& attacker, Brain& target);

/// The sector of A's record the snap aims at (`FightGoal_TryGrab`): the first of 4, 5, 3, 6 and 2 whose nearest human
/// (flag bit 1) holds an attack slot on A (`Brain_HoldsAttackSlot`: he is in A's own slot list, attacking A); none
/// without one.
[[nodiscard]] std::optional<int> snapSectorOf(Brain& attacker);

/// `brain`'s human's capsule radius, metres (the body's radius × the human's scale).
[[nodiscard]] float capsuleRadius(const Brain& brain);

/// One tackle-meter think for a cop, a gang soldier or a Warrior (`0x00300908`, `0x00304840`, `0x0030543c`, every
/// 0.2 s): it rises by the target's gait while the target moves and holds a weapon or moves away from the thinker's
/// facing, and falls otherwise; nothing without a target.
void thinkTackle(Brain& brain);

} // namespace coney::ai
