// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>

#include "core/renderer_choice.h"
#include "debug/tunables.h"

namespace coney::debug {

/// Registers the game's tunables in `registry`, each over the live value the game reads and defaulting to its
/// researched value:
///
/// - **Movement** (src/human/locomotion.h, docs/research/characters-locomotion.md#movement-constants): the stick's dead
/// zone,
///   the run threshold, the acceleration, the turn limit of each gait, the lean's factors and a scale on each of the
///   player's gait speeds (the speeds come from the clips' root motion,
///   docs/research/characters-classes.md#speed-classes);
/// - **Body** (src/human/body.h, docs/research/characters-ground.md#walls): the walking sphere and the 0.25 m step
/// rule;
/// - **Sprint**, **Jump** and **Climb** (src/human/stamina.h, jump.h, climb.h,
/// docs/research/characters-traversal.md#sprint,
///   #jump, #climb): stamina's maximum, drain and refill; the jump's speeds and checks; the climbs' probes and windows;
/// - **World** (src/human/humans.h, docs/guides/debug-menu.md#world): the humans' gravity, the original's 15.68 m/s²;
/// - **Follow camera** (src/camera/follow_camera.h, docs/research/camera.md): the position lag, the collision margins,
///   and the leash band, the pitch and the look-at height a new camera starts with;
/// - **Display** (src/scenes/letterbox.h, docs/guides/enhancements.md#where-the-settings-live): Coney's render-side
///   settings: whether the cutscene letterbox's bars are drawn (default on, the original's look), and the renderer the
///   next start draws with (`Display/Renderer`, core/renderer_choice.h: 0 GL3, the default; 1 the GPU renderer),
///   whether the next start paces for a VRR display (`Display/VRR`, core/vrr_pacing.h: off by default), and the
///   display preset with the values it sets (`Display/Preset`, core/display_preset.h: 0 *PS2*, the default).
///
/// Call it once at start-up; the values live as long as the program. Unregister with
/// TunableRegistry::removeCategory().
void registerGameTunables(TunableRegistry& registry);

/// The renderer the overrides file at `path` saves (`Display/Renderer`), read before the renderer starts and so before
/// the game's tunables register: GL3 when the file is missing, does not set it, or does not load (the full load after
/// the start reports a bad file and stops the run).
[[nodiscard]] RendererKind savedRenderer(const std::string& path);

/// Whether the overrides file at `path` saves the VRR frame-pacing mode on (`Display/VRR`, core/vrr_pacing.h), read
/// before the frame pacer starts, as savedRenderer() is: off when the file is missing, does not set it, or does not
/// load.
[[nodiscard]] bool savedVrr(const std::string& path);

} // namespace coney::debug
