// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <functional>
#include <string_view>

#include "scripting/lua_vm.h"
#include "scripting/script_bindings.h"

namespace coney::script {

/// The bindings a Rumble arena's script needs to play a match beyond the set-up (docs/research/rumble.md#bindings):
/// the intro and countdown (`ShowRumbleModeIntro`), the result screen (`HUDLaunchRumbleWin`), the computer side's
/// tactics (`TacticConfront`, `TacticAttack`), and the fighters' set-up (`BrFlushActions`, `BrFlushGoals`,
/// `HuSetMaxHealth`, `HuGetGang`, `HuDelete`) and the win camera (`CameraCreateWin`, `CamSetFollowHeading`,
/// `CamDelete`). All real; installBindings() registers them.
inline constexpr std::array<std::string_view, 12> kRumbleMatchBindings{
    "BrFlushActions", "BrFlushGoals", "CamDelete",      "CamSetFollowHeading", "CameraCreateWin", "HUDLaunchRumbleWin",
    "HuDelete",       "HuGetGang",    "HuSetMaxHealth", "ShowRumbleModeIntro", "TacticAttack",    "TacticConfront"};

/// Registers kRumbleMatchBindings in `vm`, handing the intro and the result screen to `context.host`, the cameras to
/// `context.cameras` and the rest to `context.ai`, each read at the call (null ones do nothing; `HuGetGang` then
/// answers 65535, `CameraCreateWin` NilHandle). The win camera takes its handle from `nextHandle`.
///
/// Research: docs/research/rumble.md, docs/references/bindings/hud.md#showrumblemodeintro,
/// docs/references/bindings/hud.md#hudlaunchrumblewin
void addRumbleMatchBindings(LuaVm& vm, const BindingContext& context, const std::function<double()>& nextHandle);

} // namespace coney::script
