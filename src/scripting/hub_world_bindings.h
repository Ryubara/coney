// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <string_view>

#include "scripting/lua_vm.h"
#include "scripting/script_bindings.h"
#include "scripting/script_system.h"

// The world, effects, HUD, sound, level and configuration bindings the hub (`level95`) adds: the cars' and particles'
// removal, the flag network's reset, the object zones' removal marks, the sprite batches, the motion blur's strength,
// the clubhouse's action text and the prompt's icon cycle, the mission select and statistics screens, the ambient
// emitters' switch, the sound matrix and the positional sounds, the two-player check, the store's reset, the saved
// script flags, the autosave, the unlockables by type, and the configuration of the context actions, the crimes, the
// turf, the money callback, the loot's value and the stick.
// Research: docs/references/bindings/story.md#level95, docs/references/bindings/world.md,
// docs/references/bindings/hud.md, docs/references/bindings/sound.md, docs/references/bindings/level.md,
// docs/references/bindings/config.md

namespace coney::script {

/// The bindings registered here; installBindings() registers them with addHubWorldBindings().
inline constexpr std::array<std::string_view, 27> kHubWorldBindings{"CarDestroy",
                                                                    "CfgActionDistance",
                                                                    "CfgEnableCrimeType",
                                                                    "CfgEnableTurfInvasion",
                                                                    "CfgObjectValueMod",
                                                                    "CfgPlayerCombatWalkOnly",
                                                                    "CfgPowerupPickup",
                                                                    "CfgStickDeflection",
                                                                    "CheckMultiplayer",
                                                                    "EnableAmbientEmitter",
                                                                    "FlagNetClear",
                                                                    "GetLUASaveDataBool",
                                                                    "GetPTank",
                                                                    "HUDEnableClubActionText",
                                                                    "HUDShowMissionSelect",
                                                                    "HUDTurnOffActionCycleAnim",
                                                                    "HUDTurnOnActionCycleAnim",
                                                                    "KillParticle",
                                                                    "ObjMarkZone",
                                                                    "ReleasePTank",
                                                                    "ResetStore",
                                                                    "SetLUASaveDataBool",
                                                                    "SetMotionAlpha",
                                                                    "ShowGameStatsInterface",
                                                                    "SndLoadMatrix",
                                                                    "SoundPlay",
                                                                    "SSMC_StartSaveSequence"};

/// Registers kHubWorldBindings in `vm`, acting on `context` (its state, HUD, sound, effects, cars, flags, flag network,
/// spawn records and binding host); `SndLoadMatrix` runs its preload through `scripts`.
///
/// Research: docs/references/bindings/story.md#level95
void addHubWorldBindings(ScriptSystem& scripts, LuaVm& vm, const BindingContext& context);

} // namespace coney::script
