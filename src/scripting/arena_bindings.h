// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <string_view>

#include "scripting/lua_vm.h"
#include "scripting/script_bindings.h"

// The bindings every Rumble arena script (and later the hub's) calls beyond the Brawl's match bindings
// (scripting/rumble_match_bindings.h): the game mode, the precache queue, the mission-info switch, the automatic
// switch, the humans' movement lock, speech switch, pocket and damage response, the teleport to a point, and King of
// the hill's gang icons, reticules and domination tactic, and Battle royal's knock-out, slow motion and Warrior
// commands switch.
// Research: docs/research/rumble.md#bindings, docs/references/bindings/level.md, docs/references/bindings/character.md

namespace coney::script {

/// The bindings registered here; installBindings() registers them with addArenaBindings().
inline constexpr std::array<std::string_view, 20> kArenaBindings{
    "BrSetDamageResponse",    "CNSEnableMissionInfo",
    "GangAttachSpinningIcon", "GangRemoveSpinningIcon",
    "GangSetDamageResponse",  "GetGameMode",
    "HuEnableSoundCommands",  "HuForceEnableReticule",
    "HuLockMovement",         "HuPutItemInPocket",
    "HuSetConscious",         "HuSetSlowMo",
    "HuRemoveItemInPocket",   "PrecacheWorld",
    "QueueFileToPrecache",    "SetGameMode",
    "TacticDomination",       "Teleport",
    "TurnWarriorCommands",    "WCEnableAutomaticSwitching"};

/// Registers kArenaBindings in `vm`: the game mode, the switches and the precache queue on `context.state`; the humans'
/// calls through `context.ai`'s AiBindingHost::humans() as it is at each call (none without it); `Teleport` as
/// `TeleportToFlag` moves a human (`context.humans`, `context.ai`).
///
/// Research: docs/research/rumble.md#bindings
void addArenaBindings(LuaVm& vm, const BindingContext& context);

} // namespace coney::script
