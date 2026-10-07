// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <functional>
#include <string_view>

#include "scripting/lua_vm.h"
#include "scripting/script_bindings.h"
#include "scripting/script_system.h"

// The bindings the story's seventh mission (`level5`) adds: a two-line exchange, the tagging test, a human's rage and
// reachable mark, the turn to a point, the AI Warriors' weapons switch, a car's parts, an object's physics body and the
// preloaded positional sounds, the room smoke and the HUD's scripted bars.
// Research: docs/references/bindings/story.md#level5, docs/references/bindings/character.md,
// docs/references/bindings/ai.md, docs/references/bindings/config.md, docs/references/bindings/world.md,
// docs/references/bindings/sound.md, docs/references/bindings/effects.md,
// docs/references/bindings/hud.md

namespace coney::script {

/// The bindings registered here; installBindings() registers them with addMission7Bindings().
inline constexpr std::array<std::string_view, 15> kMission7Bindings{
    "ActTurnTo",           "CarRemovePart",     "CfgWarriorWeapons", "EndRoomSmoke", "HUDEnableBar",
    "HUDSetBarPercentage", "HUDSetBarProperty", "HuActionDialog",    "HuIsTagging",  "HuMarkReachable",
    "HuSetRageMode",       "ObjEnablePhysics",  "SoundPreLoad",      "SoundStart",   "StartRoomSmoke"};

/// Registers kMission7Bindings in `vm`, acting on `context` (its humans, brains, cars, spawn records, state and sound);
/// callbacks go through `scripts`, and the preloaded sounds take their handles from `nextHandle`.
///
/// Research: docs/references/bindings/story.md#level5
void addMission7Bindings(ScriptSystem& scripts, LuaVm& vm, const BindingContext& context,
                         const std::function<double()>& nextHandle);

} // namespace coney::script
