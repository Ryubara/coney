// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <functional>
#include <string_view>

#include "scripting/lua_vm.h"
#include "scripting/script_bindings.h"

namespace coney::script {

/// The camera bindings level99 calls: the follow camera's set-up, configuration, zoom, pitch and watched human, the
/// locked cameras, making a camera current with or without a blend, resets, the switches and the shared target list.
/// All real; installBindings() registers them.
inline constexpr std::array<std::string_view, 10> kCameraBindings{
    "CamEnable",        "CameraCreateLocked", "CameraMakeActive", "CameraReset", "CamSetFollowAngle",
    "CamSetFollowZoom", "CamSetSecondary",    "CamSetupFollow",   "CamTarget",   "CfgFollowCamera"};

/// Registers kCameraBindings in `vm`, working on `context.cameras` (null keeps no cameras: the making bindings still
/// return new handles, the rest do nothing). `nextHandle` gives each new camera its handle, from the counter the other
/// world objects' handles come from. Sets the cameras' locator to the context's humans, so `CamSetSecondary` can
/// find the human it keeps in view.
///
/// Research: docs/research/camera.md#script-calls, docs/references/bindings/camera.md
void addCameraBindings(LuaVm& vm, const BindingContext& context, std::function<double()> nextHandle);

} // namespace coney::script
