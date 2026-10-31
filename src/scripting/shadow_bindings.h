// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <string_view>

#include "scripting/lua_vm.h"
#include "scripting/script_bindings.h"

namespace coney::script {

/// The level's shadow light and who casts a shadow: `SetShadowColor`, `SetShadowLightOffset` (both called once per
/// level by `global.lua`) and `EnableShadow`. Empty on the PS2; Coney keeps their values as the Xbox port keeps them,
/// render-side, in the level's lighting (graphics::ShadowLight, graphics::ShadowCasters), which the simulation never
/// reads. installBindings() registers them.
inline constexpr std::array<std::string_view, 3> kShadowBindings{"EnableShadow", "SetShadowColor",
                                                                 "SetShadowLightOffset"};

/// Registers kShadowBindings in `vm`, working on `context.lighting` as it is at each call (`context` must outlive the
/// state; a null lighting keeps nothing).
///
/// Research: docs/research/xbox-lighting.md#shadow-light, docs/references/bindings/effects.md#setshadowcolor
void addShadowBindings(LuaVm& vm, const BindingContext& context);

} // namespace coney::script
