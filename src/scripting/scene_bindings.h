// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <string_view>

#include "scripting/lua_vm.h"
#include "scripting/script_bindings.h"

namespace coney::script {

/// The scene bindings Coney implements: loading a scene, binding humans and objects to it, playing, stopping and
/// asking about it (docs/references/bindings/scene.md), and the goals that join a human to a scene's role
/// (`GoalJoinCinematic`, `GoalJoinFixedScene`, `GoalJoinAnimation`, docs/references/bindings/ai.md). All real; they
/// work on `BindingContext::scenes` and do nothing (returning nil or 0) without one, where installBindings() puts
/// Coney's stand-in in place of the preload and play bindings (a scene loads and ends at once). installBindings()
/// registers them.
inline constexpr std::array<std::string_view, 16> kSceneBindings{
    "GoalJoinAnimation",  "GoalJoinCinematic",  "GoalJoinFixedScene",  "SceneAddObject",
    "SceneDone",          "SceneIsPreloaded",   "SceneLength",         "ScenePlay",
    "ScenePlayAnimation", "ScenePlayCinematic", "ScenePlayFixedScene", "ScenePreload",
    "SceneSetCallback",   "SceneStop",          "SceneTerminate",      "SceneUnload"};

/// Registers kSceneBindings in `vm`, working on `context.scenes`.
///
/// Research: docs/research/scenes.md, docs/research/scenes.md#superrunscene
void addSceneBindings(LuaVm& vm, const BindingContext& context);

} // namespace coney::script
