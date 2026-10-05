// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <string_view>

#include "scripting/lua_vm.h"
#include "scripting/script_bindings.h"

namespace coney::script {

/// The bindings the Rumble menu's chunks build its lists with: the modes (`CfgRumbleGame`), the gangs
/// (`CfgRumbleGang`), the arenas (`CfgRumbleArena`) and the characters' descriptions (`CfgRumbleChar`). All real;
/// installBindings() registers them.
inline constexpr std::array<std::string_view, 4> kRumbleBindings{"CfgRumbleArena", "CfgRumbleChar", "CfgRumbleGame",
                                                                 "CfgRumbleGang"};

/// Registers kRumbleBindings in `vm`. They add to `context.rumble` (a null one keeps nothing), check the unlocks in
/// `context.state` and, for an arena, read the chosen mode and the level table there.
///
/// Research: docs/research/frontend.md#rumble-data, docs/references/bindings/config.md#cfgrumblegame
void addRumbleBindings(LuaVm& vm, const BindingContext& context);

} // namespace coney::script
