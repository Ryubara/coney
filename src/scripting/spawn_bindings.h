// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <functional>
#include <string_view>

#include "scripting/lua_vm.h"
#include "scripting/script_bindings.h"

namespace coney::script {

/// The bindings that configure object types and place dynamic objects: `CfgObj` (one type of the object database)
/// and `ObjSpawn` (a spawn record). Both real; installBindings() registers them.
inline constexpr std::array<std::string_view, 2> kSpawnBindings{"CfgObj", "ObjSpawn"};

/// Registers kSpawnBindings in `vm`, working on `context`'s object types, spawn records and recorded calls (each may
/// be null, which keeps none). `nextHandle` gives each spawn record its handle, from the counter the other world
/// objects' handles come from.
///
/// Research: docs/research/objects.md#dynamic-objects, docs/references/bindings/config.md#cfgobj,
/// docs/references/bindings/world.md#objspawn
void addSpawnBindings(LuaVm& vm, const BindingContext& context, std::function<double()> nextHandle);

} // namespace coney::script
