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
/// work on `BindingContext::scenes` as it is at the call (gameplay sets it while a level is in play) and do nothing,
/// returning nil, without one; the preload and play bindings then run the SceneStandIn instead. installBindings()
/// registers them.
inline constexpr std::array<std::string_view, 16> kSceneBindings{
    "GoalJoinAnimation",  "GoalJoinCinematic",  "GoalJoinFixedScene",  "SceneAddObject",
    "SceneDone",          "SceneIsPreloaded",   "SceneLength",         "ScenePlay",
    "ScenePlayAnimation", "ScenePlayCinematic", "ScenePlayFixedScene", "ScenePreload",
    "SceneSetCallback",   "SceneStop",          "SceneTerminate",      "SceneUnload"};

/// What the preload and the play bindings do while the context has no scene system: installBindings() gives Coney's
/// stand-in, where a scene loads and ends at once (empty: they do nothing).
struct SceneStandIn {
    NativeFunction preload; ///< `ScenePreload`.
    NativeFunction play;    ///< `ScenePlayCinematic`, `ScenePlayFixedScene` and `ScenePlayAnimation`.
};

/// Registers kSceneBindings in `vm`, working on `context.scenes` (`context` must outlive the state).
///
/// Research: docs/research/scenes.md, docs/research/scenes.md#superrunscene
void addSceneBindings(LuaVm& vm, const BindingContext& context, const SceneStandIn& standIn = {});

} // namespace coney::script
