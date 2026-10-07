// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <string_view>

#include "scripting/lua_vm.h"
#include "scripting/script_bindings.h"

namespace coney::script {

/// The world bindings the first mission still needed: the dynamic objects' show, hide, destroy and zones, the trigger
/// spheres, the flag network, the subtitle switch, and two that do nothing in this build (`DoorCRCCheck`,
/// `EnableShadow`).
inline constexpr std::array<std::string_view, 16> kWorldBindings{"CfgSetMaxThrowError",
                                                                 "CfgSubtitles",
                                                                 "ChangeCollision",
                                                                 "DoorCRCCheck",
                                                                 "EnableShadow",
                                                                 "FlagEnable",
                                                                 "FlagNetAddLink",
                                                                 "FlagNetTraverse",
                                                                 "KillHumans",
                                                                 "ObjDestroy",
                                                                 "ObjEnableZone",
                                                                 "ObjHide",
                                                                 "ObjShow",
                                                                 "TriggerSphereCfg",
                                                                 "TriggerSphereEnable",
                                                                 "TriggerSphereSetRadius"};

/// Registers kWorldBindings in `vm`, working on `context` as each call finds it (a level sets its spheres and flag
/// network after the Lua state was made; a null part does nothing).
///
/// Research: docs/references/bindings/world.md, docs/references/bindings/config.md#cfgsubtitles,
/// docs/references/bindings/debug.md#doorcrccheck, docs/references/bindings/effects.md#enableshadow,
/// docs/research/scripting.md#triggers, docs/research/objects.md#dynamic-objects
void addWorldBindings(LuaVm& vm, const BindingContext& context);

} // namespace coney::script
