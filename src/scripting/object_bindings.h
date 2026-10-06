// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <functional>
#include <string_view>

#include "scripting/lua_vm.h"
#include "scripting/script_bindings.h"
#include "world_objects/object_services.h"

namespace coney::script {

/// The bindings of the breakable glass and the doors: the spawns level scripts place them with, the glass type table,
/// the door commands and queries, the radius breaks, the door links by position and the lock-pick handlers. All real;
/// installBindings() registers them.
inline constexpr std::array<std::string_view, 22> kObjectBindings{"BreakGlassInRadius",
                                                                  "BreakObjectsInRadius",
                                                                  "CfgSetGlassProperties",
                                                                  "CfgSetLockPickHandler",
                                                                  "CfgSetLockPickStageFailHandler",
                                                                  "CloseDoor",
                                                                  "ConvertJumpToDoor",
                                                                  "DisableDoorCollision",
                                                                  "DisableDoorLink",
                                                                  "DoorOpen",
                                                                  "DoorOpenDegree",
                                                                  "EnableDoorLink",
                                                                  "GetHitpoints",
                                                                  "GetLeftDoorHandle",
                                                                  "GetRightDoorHandle",
                                                                  "IsDoorOpen",
                                                                  "ObjectChangeState",
                                                                  "OpenDoor",
                                                                  "OpenDoorAnimated",
                                                                  "SetDoorPickable",
                                                                  "SpawnBreakableGlass",
                                                                  "SpawnDoor"};

/// The object type `name`'s configuration from the first `CfgObj` call `recorded` holds for it (its class, hitpoints,
/// box, material and type); nothing when none named it or `recorded` is null.
[[nodiscard]] std::optional<world_objects::ObjectTypeInfo> objectTypeFromCfgObj(const RecordedCalls* recorded,
                                                                                std::string_view name);

/// Registers kObjectBindings in `vm`, working on `context.objects` (a null one places nothing: the spawns still return
/// a new handle each, so a script keeps working) and reading the object types from `context.recorded`'s `CfgObj`
/// calls. `nextHandle` gives the panes, doors and leaves their handles, from the counter the other world objects'
/// handles come from.
///
/// Research: docs/research/objects.md, docs/research/crimes.md#lockpick
void addObjectBindings(LuaVm& vm, const BindingContext& context, const std::function<double()>& nextHandle);

} // namespace coney::script
