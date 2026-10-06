// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <functional>
#include <string_view>

#include "scripting/lua_vm.h"
#include "scripting/script_bindings.h"

namespace coney::script {

/// The parked cars' bindings (docs/research/cars.md): `CarSpawn` makes a car, `CarSetColor` paints it,
/// `CarMakeGoodAsNew` repairs it and `CarSpawnRadio` puts a stealable stereo in it. All real; installBindings()
/// registers them.
inline constexpr std::array<std::string_view, 5> kCarBindings{"CarMakeGoodAsNew", "CarPlaceInTrunkOnDetach",
                                                              "CarSetColor", "CarSpawn", "CarSpawnRadio"};

/// Registers kCarBindings in `vm`, working on `context.cars` (null: `CarSpawn` still gives a handle, and nothing is
/// kept). `nextHandle` gives each car its handle, from the counter every world object's handle comes from.
///
/// Research: docs/references/bindings/world.md#carspawn, docs/references/bindings/world.md#carsetcolor,
/// docs/references/bindings/world.md#carmakegoodasnew, docs/references/bindings/world.md#carspawnradio
void addCarBindings(LuaVm& vm, const BindingContext& context, std::function<double()> nextHandle);

} // namespace coney::script
