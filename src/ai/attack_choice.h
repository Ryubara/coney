// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <functional>
#include <optional>

#include "ai/attack_kinds.h"
#include "combat/stick.h"

// How an AI chooses its attack: which of the 45 kinds the attacker A may use on the target T now (the per-kind test)
// and may start now (the same plus the guard and the reach), and the weighted draw with its adjustments for a crowd,
// a busy target, a weapon in hand, a rear-grabbed target and the player's attack pattern. Pure decisions over what the
// two humans are; ai::Brain's fight goals fill the views from their humans.
// Research: docs/research/ai.md#pick-attack

namespace coney::ai {

/// The kind meaning "none chosen" (`+0x10` = 45).
inline constexpr int kNoAttackKind = 45;

/// What the per-kind test reads of the attacker (`Human_CanUseAttackKind`'s A). "Free" is the state word with none of
/// `0x7bf9e9f7ff0`: on its feet, in no hold, no reaction playing.
struct AttackerView {
    bool free = true;            ///< None of `0x7bf9e9f7ff0`.
    bool atRunSpeed = false;     ///< Gait 4 or 5 at the run speed or faster (the charge and the dive).
    bool padControlled = false;  ///< Per-player `+0x1b` (kind 15).
    int heldObjectType = 0;      ///< The held world object's type byte `+0x87`; 0 empty-handed.
    bool holdsThrowable = false; ///< A throwable object in hand (kind 23).
    bool holdsSprayPaint = false;
    bool carriesCuffs = false;
    bool grabPower = false;       ///< The power meter at a fifth of its maximum or more (kind 22).
    bool grabbing = false;        ///< State `0xc0`.
    bool tackling = false;        ///< State `0x400`.
    bool grabbed = false;         ///< State `0x30`.
    bool grabbedFromRear = false; ///< State `0x20`.
    bool tackled = false;         ///< State `0x800`.
    bool knockedDown = false;     ///< State `0x80000`.
    bool groundedGoal = false;    ///< The Grounded reaction goal (`0x17`) runs on its brain.
    /// None of `0x7bf9e9f7f00` (the grab and grabbed moves' own test); `0x7bf9e9f73f0` for the mount's.
    bool holdMovesFree = false;
    bool holdsTarget = false;  ///< The human it holds (`+0xc4`) is T.
    bool heldByTarget = false; ///< T holds it (T's `+0xc4` is A).
    /// It holds an attack slot on the human beside or behind it (sector 2-6 of its sector record, flag bit 1, within
    /// 2.5 m): the snap's target.
    bool snapTargetAside = false;
    int characterClass = -1;    ///< Its class (class `0x77` keeps a kind-4/6 object from 16-18).
    bool holdsKind4or6 = false; ///< An object of kind 4 or 6 in hand.
};

/// What the per-kind test reads of the target (T).
struct TargetView {
    bool isAttacker = false;     ///< T is A himself (kind 14, kind 42 on himself).
    bool downOrOut = false;      ///< State any of `0xe3000`.
    bool highOrBusy = false;     ///< `Human_IsHighOrBusy` (`0x00225200`).
    bool state2000 = false;      ///< State `0x2000`.
    bool held400000 = false;     ///< Held flag `0x400000`.
    bool knockedDown = false;    ///< State `0x80000`.
    bool notDown = true;         ///< `Human_IsNotDown` (`0x00225500`).
    bool ungrabbable = false;    ///< Human flag `+0xe0` `0x40`.
    bool free = true;            ///< None of `0x7bf9e9f7ff0`.
    bool heldFlag40 = false;     ///< Held flag `0x40`.
    bool specialRefused = false; ///< State any of `0x40100f0800` (16-18).
    bool grabRefused = false;    ///< State any of `0x7bfdc8f7fd0` (22).
};

/// Whether A may use attack kind `kind` on T now (`Human_CanUseAttackKind`): the table on the page, by kind.
/// @orig 0x002240e8 Human_CanUseAttackKind (unknown)
[[nodiscard]] bool canUseAttackKind(const AttackerView& attacker, const TargetView& target, int kind);

/// The guard `Human_CanStartAttack` runs before its per-kind test.
struct StartGuard {
    bool targetHeldBusy = false;   ///< T's held flags any of `0xc08200`...
    bool targetPinned = false;     ///< ... unless T has state `0x2000` and A state `0x1000`.
    bool attackerHeldBusy = false; ///< A's held flags any of `0x5c7eee0`.
    bool damagePending = false;    ///< A has damage pending (record `+0x118`).
    bool inReach = true;           ///< T within the kind's far reach.
};

/// Whether A may start attack kind `kind` on T now (`Human_CanStartAttack`): the guard, then canUseAttackKind().
/// **Coney reading**: the per-kind cases were read for 10-15 only and are taken to mirror canUseAttackKind().
/// @orig 0x00224778 Human_CanStartAttack (unknown)
[[nodiscard]] bool canStartAttack(const StartGuard& guard, const AttackerView& attacker, const TargetView& target,
                                  int kind);

/// Whether `kind` is a charge kind (`AttackKind_IsCharge`): 0 (`X1`), 19 (the charge), 20 (the dive), 21 (the
/// tackle): what a running AI may use, and what ends the run-in with an attack.
/// @orig 0x00229b60 AttackKind_IsCharge (unknown)
[[nodiscard]] bool isChargeKind(int kind);

/// What the draw's adjustments read (`Brain_PickAttack`'s set-up).
struct PickContext {
    int targetSlotsTaken = 0;       ///< `n`: attack slots taken on T.
    int targetSlotsMax = 4;         ///< `m`: T's maximum.
    int ownSlotsTaken = 0;          ///< `c`: slots taken on A himself.
    int ownSlotsMax = 4;            ///< `M`: A's maximum.
    bool running = false;           ///< A at gait 4 or 5 with no held flags.
    bool armed = false;             ///< A holds an object.
    bool targetRearGrabbed = false; ///< T grabbed from the rear (state `0x20`).
    /// The pattern read: `f` (the larger of T's pattern bytes × 0.05) when T is pad-controlled, targets A and A's
    /// class threshold `(16 − class +0x37) / 16` is above 0 and at most `f`; none otherwise.
    std::optional<float> pattern;
    bool targetChasable = true;     ///< `Brain_CanBeChased` on T and brain `+0x2d3`.
    bool swingsObject = false;      ///< A holds a throwable object or one of kind 4 or 6 (kind 0 when not chasable).
    int armedBonus = 100;           ///< 100, or 0 for a class whose `+0x11b` is 9, 10 or 13.
    bool hurtAndAttackable = false; ///< A hurt and his brain attackable: kinds 31-34 and 42-44 are left out.
};

/// The weighted draw (`Brain_PickAttack`): a kind that passes `filter`, drawn by its adjusted weight, or none (45).
/// One draw of `random` when there is a candidate with weight.
/// @orig 0x0028e708 Brain_PickAttack (unknown)
/// @orig 0x002911f8 Brain_GetAttackWeight (unknown)
[[nodiscard]] std::optional<int> pickAttackKind(const AttackWeights& weights, const PickContext& context,
                                                const std::function<bool(int)>& filter, combat::CombatRandom& random);

} // namespace coney::ai
