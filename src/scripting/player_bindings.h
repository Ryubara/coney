// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <string_view>

#include "scripting/lua_vm.h"
#include "scripting/script_bindings.h"

namespace coney::script {

/// The bindings over the players' state (GameState::player, warriors/player_state.h): the inventory and money, the
/// statistics, the unlockables' records, the mission stopwatch, crime reporting, the Lua pad handlers, a store's screen
/// colour and the second player's join flag. All real; installBindings() registers them.
inline constexpr std::array<std::string_view, 40> kPlayerBindings{
    "CfgInventoryCallback",
    "CfgInventoryItem",
    "CfgMultiplayerJoin",
    "CfgSetStatTypeMax",
    "CfgSetStatValue",
    "CfgSetSteroTheftHandler",
    "EnterStore",
    "ExitStore",
    "GiveMoney",
    "InvGetMoney",
    "InvGetSpraycanCharges",
    "InvGiveItem",
    "InvGiveRevive",
    "InvGiveSkeletonKey",
    "InvNumberOf",
    "InvNumberRevives",
    "InvNumberSkeletonKeys",
    "InvPlayerHasItem",
    "InvSetMoney",
    "InvSetSpraycanCharges",
    "PadSetHandler",
    "PadSetHandlerEx",
    "ReportCrime",
    "StatAdd",
    "StatGetScore",
    "StatReset",
    "StatResetPlayer",
    "TakeMoney",
    "UM_GetRecordData",
    "UM_IsDataDirty",
    "UM_IsDataUnlocked",
    "UM_IsLevelComplete",
    "UM_IsTypeDirty",
    "UM_Reset",
    "UM_SetNumUnlockables",
    "UM_SetUnlockable",
    "UM_Unlock",
    "W_GetStopWatchTime",
    "W_SetStopWatch",
    "W_StartStopWatch",
};

/// Registers kPlayerBindings in `vm`, working on `context`'s game state (its `player` and the profile's unlock bits),
/// its humans (a statistic's human is a player's; null: none is) and `scripts` (the money callback, the stopwatch's
/// start time).
///
/// Research: docs/research/player-state.md, docs/references/bindings/level.md, docs/references/bindings/input.md,
/// docs/research/scripting.md#stopwatch, docs/research/ai.md#crimes
void addPlayerBindings(ScriptSystem& scripts, LuaVm& vm, const BindingContext& context);

} // namespace coney::script
