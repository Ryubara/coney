// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <functional>
#include <string_view>

#include "scripting/lua_vm.h"
#include "scripting/script_bindings.h"

namespace coney::script {

/// The effects bindings: `SpawnParticle` (a particle system of a type, docs/research/particles.md),
/// `QueueMotionBlurEffect` (the first view's motion blur, docs/research/graphics.md#looks) and the screen tint's
/// `SetLevelColour`, `EnterStore` and `ExitStore` (docs/research/rendering.md#tint). All real; installBindings()
/// registers them.
inline constexpr std::array<std::string_view, 5> kEffectsBindings{"EnterStore", "ExitStore", "QueueMotionBlurEffect",
                                                                  "SetLevelColour", "SpawnParticle"};

/// Registers kEffectsBindings in `vm`, working on `context.effects` (null: a particle system still gets a handle, and
/// nothing is drawn, blurred or tinted). `nextHandle` gives each particle system its handle, from the counter every
/// world object's handle comes from.
///
/// Research: docs/references/bindings/effects.md#spawnparticle,
/// docs/references/bindings/effects.md#queuemotionblureffect
void addEffectsBindings(LuaVm& vm, const BindingContext& context, std::function<double()> nextHandle);

} // namespace coney::script
