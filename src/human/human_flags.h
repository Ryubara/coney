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
/// Set by `Human_MakePlayer` with kFastClimber; its meaning is not researched.
inline constexpr std::uint64_t kMadePlayer = 0x4;
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
/// Never throws the weapon it holds (`HuSetNoThrowWeapon`; inferred from the name).
inline constexpr std::uint64_t kNoThrowWeapon = 0x4000000000;
/// Demi-god (`HuSetDemiGodMode`): one hit cannot take health below the floor fraction of the maximum, and reaching it
/// sets kGod (`0x00265f70`, `0x00256f28`).
inline constexpr std::uint64_t kDemiGod = 0x20000000000;
/// Never picked as a target (`HuSetNoTarget`, `HuSetNoAutoLock`; `0x00279410`).
inline constexpr std::uint64_t kNoTarget = 0x100000000000;

/// The flags a player's human starts with: what `Human_MakePlayer` sets (kFastClimber, kMadePlayer, kRageAllowed) and
/// the combo rule kComboStrength (docs/research/combat.md#human-flags). kDemiGod, also seen on the player at runtime,
/// is the level scripts' (`HuSetDemiGodMode`): a mode whose scripts never set it (the Rumble arena) lets the player be
/// knocked out.
inline constexpr std::uint64_t kPlayerFlags = kFastClimber | kMadePlayer | kComboStrength | kRageAllowed;

} // namespace coney::human::flag
