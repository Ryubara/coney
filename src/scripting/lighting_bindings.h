// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <string_view>

#include "scripting/lua_vm.h"
#include "scripting/script_bindings.h"

namespace coney::script {

/// The bindings a level's scripts light it with: its lights (`SetLight`, both forms, and `SetLightFlicker`), the
/// world ambient (`SetWorldAmbient`), the colour offset (`SetGammaOffset`) and the fog (`SetFogColor`,
/// `SetFogDistance`). All real; installBindings() registers them.
inline constexpr std::array<std::string_view, 6> kLightingBindings{"SetFogColor", "SetFogDistance",  "SetGammaOffset",
                                                                   "SetLight",    "SetLightFlicker", "SetWorldAmbient"};

/// Registers kLightingBindings in `vm`, working on `context.lighting` as it is at each call (`context` must outlive
/// the state; a null lighting keeps nothing: `SetLight` then returns 0).
///
/// Research: docs/research/lighting.md, docs/references/bindings/effects.md#setlight
void addLightingBindings(LuaVm& vm, const BindingContext& context);

} // namespace coney::script
