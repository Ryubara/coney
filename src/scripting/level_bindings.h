// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <functional>
#include <string_view>

#include "scripting/lua_vm.h"
#include "scripting/script_bindings.h"

namespace coney::script {

/// The bindings a level script needs to place its world and its players: the world flags (`AddFlag`, `FindFlag`,
/// `GetFlagPos`, `TeleportToFlag` and the pool `CfgSetDatabaseSizes` sizes), where an object is (`GetPosition`), the
/// saved script numbers
/// (`GetLUASaveDataFloat`, `SetLUASaveDataFloat`), the start callback `InitLevel` calls (`SetStartGameCallback`) and
/// the Rumble menu's set-up (`GetRumbleModeData`, `GetRumbleModeGangName`). All real; installBindings() registers them.
inline constexpr std::array<std::string_view, 12> kLevelBindings{"AddFlag",
                                                                 "CfgSetDatabaseSizes",
                                                                 "FindFlag",
                                                                 "GetFlagPos",
                                                                 "GetLUASaveDataFloat",
                                                                 "GetPosition",
                                                                 "GetRumbleModeData",
                                                                 "GetRumbleModeGangName",
                                                                 "HuGetPosition",
                                                                 "SetLUASaveDataFloat",
                                                                 "SetStartGameCallback",
                                                                 "TeleportToFlag"};

/// Registers kLevelBindings in `vm`, working on `context` (its state, flags, humans and recorded calls; flags and
/// humans may be null, which keeps none). `nextHandle` gives each new flag its handle, from the counter the other world
/// objects' handles come from.
///
/// Research: docs/research/flags.md, docs/research/scripting.md#errors-in-a-fresh-state,
/// docs/research/frontend.md#quick-rumble
void addLevelBindings(LuaVm& vm, const BindingContext& context, std::function<double()> nextHandle);

} // namespace coney::script
