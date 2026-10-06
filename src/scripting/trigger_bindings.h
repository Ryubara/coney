// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <functional>
#include <string_view>

#include "scripting/lua_vm.h"
#include "scripting/script_bindings.h"

namespace coney::script {

/// The bindings that give objects message handlers and make the volume boxes that send them: what drives most of a
/// mission's progress (docs/research/scripting.md#triggers).
inline constexpr std::array<std::string_view, 4> kTriggerBindings{"AddVolumeBox", "RotateVolumeBox", "SetMsgHandler",
                                                                  "SetMsgHandlerEx"};

/// Registers kTriggerBindings in `vm`, working on `context`'s message handlers and volume boxes (either may be null,
/// which keeps none). `nextHandle` gives each new box its handle, from the counter the other world objects' handles
/// come from.
///
/// Research: docs/research/scripting.md#message-handlers, docs/research/scripting.md#triggers,
/// docs/references/bindings/script.md#setmsghandler, docs/references/bindings/world.md#addvolumebox
void addTriggerBindings(LuaVm& vm, const BindingContext& context, std::function<double()> nextHandle);

} // namespace coney::script
