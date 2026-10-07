// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

// The human flag word (human `+0xe0`, a u64): the switches the scripts set on a human (`HuSetGodMode`,
// `HuSetUnstunnable`, ...) and the ones combat, climbing and the meters read. Only the bits the research names are
// listed; the word keeps any other bit a caller sets.
// Research: docs/research/combat.md#human-flags, docs/research/characters.md#the-record,
// docs/references/bindings/character.md

namespace coney::human::flag {

/// May start a climb from a run (`Human_MakePlayer` sets it; `HuSetFastClimber`).
inline constexpr std::uint64_t kFastClimber = 0x2;
/// Can be revived when down (`HuSetRevivable`): the revive search (`0x00278fa0`) accepts only a knocked-down human with
/// it. `Human_MakePlayer` sets it on every player.
inline constexpr std::uint64_t kRevivable = 0x4;
/// The head does not turn toward its look-at target (`HuBlockLook`, read by `0x002482e0`).
inline constexpr std::uint64_t kBlockLook = 0x1000;
/// The head turns toward its look-at target whatever the human does (`HuForceLook`; `GoalPlayDynAnimation` sets it
/// around its clip).
inline constexpr std::uint64_t kForceLook = 0x10000;
/// Escapes a grab when the grabber's power runs out, and can counter a grab at the end of its intro
/// (`HuSetAutoEscape`, `0x002562d0`, `0x0026c1d8`).
inline constexpr std::uint64_t kAutoEscape = 0x20000;
/// The hat is never knocked off (`HuSetKeepHat`, read by the knock-off `0x00258330`).
inline constexpr std::uint64_t kKeepHat = 0x10000000000;
/// God mode (`HuSetGodMode`): the human takes no damage. Set too once a demi-god's health reaches its floor.
inline constexpr std::uint64_t kGod = 0x10;
/// Nobody can grab the human (`HuSetUngrabbable`; the effect is inferred from the name).
inline constexpr std::uint64_t kUngrabbable = 0x40;
/// As the victim, a hit's reaction strength is capped at 1, so it is never knocked down (`HuSetUngroundable`,
/// `0x00266d00`).
inline constexpr std::uint64_t kUngroundable = 0x80;
/// Never stunned (`HuSetUnstunnable`, `0x0026a6d0`).
inline constexpr std::uint64_t kUnstunnable = 0x100;
/// As the victim, reaction strength - 1 (`HuSetReducedReact`).
inline constexpr std::uint64_t kReducedReact = 0x200;
/// As the victim, combo hits keep their full strength (the player has it).
inline constexpr std::uint64_t kComboStrength = 0x400;
/// A hit takes its health but plays no reaction, so the clip goes on (`HuSetNoReact`, `Human_ApplyPendingDamage`
/// `0x00265f70`); the AI's block and engage goals set it for themselves too.
inline constexpr std::uint64_t kNoReact = 0x800;
/// Keeps its weapon when hit (`HuSetKeepWeapon`; inferred from the name).
inline constexpr std::uint64_t kKeepWeapon = 0x2000;
/// The rage meter stays where it is (`HuSetLockedRage`; inferred from the name).
inline constexpr std::uint64_t kRageLocked = 0x100000;
/// As the attacker, reaction strength + 1 against a victim with kComboStrength, and hit armour ignored
/// (`HuSetIncreasedReact`).
inline constexpr std::uint64_t kIncreasedReact = 0x200000;
/// May gain rage: `Human_AddRage` adds only for a human with it (`Human_MakePlayer` sets it; `HuSetPreventRage`
/// clears it).
inline constexpr std::uint64_t kRageAllowed = 0x2000000;
/// Stamina and power stay full: no sprint drain, no power spent (`HuSetTireless`, `0x00226448`).
inline constexpr std::uint64_t kTireless = 0x4000000;
/// Its movement locked (`HuLockMovement`, `0x00234ef8`). **Coney stand-in**: what reads the bit is not on the page, so
/// a locked human is moved neither by its stick nor by its brain, as an arrested one is; it still fights and acts.
inline constexpr std::uint64_t kMovementLocked = 0x200000000;
/// Cannot be tackled (`HuBlockTackle`; inferred from the name: the tackle code that reads it is not on the page).
inline constexpr std::uint64_t kBlockTackle = 0x100000000;
/// A handcuffed human a player may free (`HuSetUnarrestable`); what else reads it is not on the page.
inline constexpr std::uint64_t kUnarrestable = 0x8000;
/// Never throws the weapon it holds (`HuSetNoThrowWeapon`; inferred from the name).
inline constexpr std::uint64_t kNoThrowWeapon = 0x4000000000;
/// Skipped by the player's automatic target lock (`HuSetNoAutoLock`, `0x002340a8`; inferred from the name: no reader
/// is on the page, so Coney only keeps it).
inline constexpr std::uint64_t kNoAutoLock = 0x8000000000;
/// Driving a wheelchair (`HuSetWheelchairControl`, `0x00234188`): a pad-driven human with it runs the wheelchair's
/// control (docs/research/characters.md#wheelchair).
inline constexpr std::uint64_t kWheelchair = 0x80000000000;
/// Demi-god (`HuSetDemiGodMode`): one hit cannot take health below the floor fraction of the maximum, and reaching it
/// sets kGod (`0x00265f70`, `0x00256f28`).
inline constexpr std::uint64_t kDemiGod = 0x20000000000;
/// Never picked as a target (`HuSetNoTarget`; the target filter `0x00279410`). `HuSetNoAutoLock` sets another bit
/// (`0x8000000000`), not this one.
inline constexpr std::uint64_t kNoTarget = 0x100000000000;
/// Triangle never starts a jump (`HuBlockJump`, `0x00237958`: `Player_TryJump` is skipped); a climb, a context action
/// or an object action can still take the press.
inline constexpr std::uint64_t kBlockJump = 0x10000000;
/// `HuSetAutoCombat` (`0x00234118`): no reader of the bit is on the page, so Coney only keeps it.
inline constexpr std::uint64_t kAutoCombat = 0x200000000000;

/// The flags a player's human starts with: what `Human_MakePlayer` sets (kFastClimber, kRevivable, kRageAllowed) and
/// the combo rule kComboStrength (docs/research/combat.md#human-flags). kDemiGod, also seen on the player at runtime,
/// is the level scripts' (`HuSetDemiGodMode`): a mode whose scripts never set it (the Rumble arena) lets the player be
/// knocked out.
inline constexpr std::uint64_t kPlayerFlags = kFastClimber | kRevivable | kComboStrength | kRageAllowed;

} // namespace coney::human::flag
