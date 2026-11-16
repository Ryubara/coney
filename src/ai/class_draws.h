// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string_view>

// What an AI's think does with the last two switches of his power class (`Brain_Think`): one in a fight with nothing
// in hand draws a weapon (byte `+0x41`; a cop with a weapon draws his in or out of a fight), and one in a fight under
// 30 % health uses a flash (byte `+0x42`); and what the two clips' events then do: the draw's `random` object event
// puts the class's weapon (or a switchblade) in the hand, the flash's action event heals him.
// Research: docs/research/characters.md#power-class-bytes

namespace coney::ai {

class Brain;

/// The weapon draw (477 `ANIM_ONE_HANDED_OBJECT_DRAW`).
inline constexpr int kWeaponDrawAnim = 477;
/// The flash (665 `ANIM_SPECIAL_FLASH`), whose action event uses it.
inline constexpr int kFlashAnim = 665;
/// The health, in percent of the maximum, under which an AI of a flashing class uses a flash (`Human_ShouldFleeHurt`).
inline constexpr float kFlashHealthPercent = 30.0F;
/// What a draw puts in the hand when the class's weapon is `none`: a switchblade.
inline constexpr std::string_view kSwitchblade = "dyn_swhbld";
/// The weapon name that means none (`CfgChar`'s `none`, the string at `0x0055feb0`).
inline constexpr std::string_view kNoWeapon = "none";

/// The steps of `Brain_Think` after the type's own think: when the human's actions are not blocked and he is in a fight
/// (Brain::hasFightGoal()), a class that flashes when hurt (`+0x42`) with health under kFlashHealthPercent clears the
/// actions and plays kFlashAnim when he is free, and nothing more; otherwise a cop (with any weapon but `none`), or a
/// fighter of a class that draws (`+0x41`), with nothing in hand clears the actions and plays kWeaponDrawAnim when he
/// is free. **Coney stand-ins**: no train is ever near (`Brain_IsNearTrain`, Coney has no trains); "free" and "blocked"
/// read Coney's states for the original's held mask `0x2fefefff` and state mask `0x7bf9e9f7ff0` (no held record flag,
/// alive, standing, not held; ai::actionsBlocked()); the draw plays as a play-anim action (the original's
/// `GenToIdleAnimAction`, which holds `0x20000` and returns to the idle, is not built).
/// @orig 0x0028f6c0 Brain_Think (unknown)
/// @orig 0x00224ea0 Human_ClassCanFlee (unknown)
/// @orig 0x00224ec8 Human_ShouldFleeHurt (unknown)
/// @orig 0x002282d8 Human_IsFree (unknown)
void thinkClassActions(Brain& brain);

/// The draw's `random` object event (clip event type `0x30` named `random`): with no reaction goal and a goal on the
/// stack, puts the class's weapon in the human's hand (ScriptServices::giveHandObject()), or kSwitchblade when it is
/// `none`. **Coney stand-in**: the four boss goals' own objects (BigThrower, BigBrawler, BigLedgeThrower,
/// StationaryThrower) are not built, so under one of them nothing is given.
/// @orig 0x0028c780 Brain_GiveHandObject (unknown)
/// @orig 0x00101dd8 Anim_FireFrameEvents (unknown)
void giveHandObject(Brain& brain);

/// The flash's action event on an AI (`Flash_Use`): he is healed to his maximum (`Human_Heal` with "full": not
/// wounded, a new body). The interface sound 23 it plays is the caller's.
/// @orig 0x00284280 Flash_Use (unknown)
void useFlash(Brain& brain);

} // namespace coney::ai
