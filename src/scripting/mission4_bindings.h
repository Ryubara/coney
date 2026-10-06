// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>
#include <string_view>

#include "scripting/lua_vm.h"
#include "scripting/script_bindings.h"
#include "scripting/script_system.h"

// The bindings the story's fourth mission (`level34`) adds: the object type index and run-time type bits, the
// civilians' call-for-help chance, the forced crime level, a brain's pedestrian type, a human's wound, the gangs'
// spawner limits, the rioters' and stationary throwers' goals, the blocking of a walkable area, a car's explosion and
// the cars' general message handler.
// Research: docs/references/bindings/story.md#level34, docs/references/bindings/world.md,
// docs/references/bindings/util.md, docs/references/bindings/config.md, docs/references/bindings/level.md,
// docs/references/bindings/ai.md, docs/references/bindings/character.md

namespace coney::script {

/// The bindings registered here; installBindings() registers them with addMission4Bindings().
inline constexpr std::array<std::string_view, 13> kMission4Bindings{"BrSetPedType",
                                                                    "CarExplode",
                                                                    "CfgChanceToGetHelp",
                                                                    "ChangeBlocker",
                                                                    "ForceCrimeLevel",
                                                                    "GangSetMaxConcurrent",
                                                                    "GangSetSpawnerMustBeOffScreen",
                                                                    "GetRTTI",
                                                                    "GoalRiot",
                                                                    "GoalStationaryThrower",
                                                                    "HuSetWounded",
                                                                    "ObjGetIndex",
                                                                    "SetGeneralCarMsgHandler"};

/// The run-time type bits `GetRTTI` gives (docs/references/bindings/util.md#getrtti).
namespace rtti {
inline constexpr std::uint32_t kProp = 0x08;  ///< A dynamic object (a spawn record).
inline constexpr std::uint32_t kTag = 0x10;   ///< A tag.
inline constexpr std::uint32_t kHuman = 0x40; ///< A human.
inline constexpr std::uint32_t kFlag = 0x80;  ///< A world flag.
} // namespace rtti

/// Registers kMission4Bindings in `vm`, acting on `context` (its state, object database, humans, flags, spawn records,
/// level objects and story host).
///
/// Research: docs/references/bindings/story.md#level34
void addMission4Bindings(ScriptSystem& scripts, LuaVm& vm, const BindingContext& context);

} // namespace coney::script
