// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <string_view>

#include "scripting/lua_vm.h"
#include "scripting/script_bindings.h"

namespace coney::script {

/// The gang bindings Coney implements here: membership, the brains' switches for a whole gang, enemies and friends,
/// the message handlers, suspension and the counts. All real; installBindings() registers them. `GangCreate`, whose
/// handle falls back to the stubs' counter without an AI host, is made with the other bindings
/// (scripting/script_bindings.cpp).
inline constexpr std::array<std::string_view, 12> kGangBindings{
    "GangAddMember",     "GangBrDead",           "GangBrFlush",           "GangDelete",
    "GangGetHeadCount",  "GangGetStandingCount", "GangMakeEnemies",       "GangMakeFriends",
    "GangSetAttackable", "GangSetMsgHandler",    "GangSetThreatResponse", "GangSuspend"};

/// Registers kGangBindings in `vm`, handing each call to `context.ai` (a null one does nothing; the counts are then 0).
///
/// Research: docs/research/ai.md#gangs, docs/references/bindings/gang.md
void addGangBindings(LuaVm& vm, const BindingContext& context);

} // namespace coney::script
