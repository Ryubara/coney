// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <expected>
#include <string_view>

#include "gamemodes/level_start.h"

namespace coney {

/// Runs a level's own scene-test path with a scene-name string set after global.lua and before the level script.
/// The script state must exist. Empty names are rejected before changing it; script failures are returned.
/// @coney-only Debug entry for the dormant scene-test scripts, docs/research/scenes.md#scene-test.
[[nodiscard]] std::expected<LevelStart, Error>
runSceneTestLevelScript(script::ScriptSystem& scripts, GameState& state, CreatedHumans& humans,
                        world_objects::WorldFlags& flags, std::string_view level, std::string_view scene,
                        world_objects::SpawnRecords* records = nullptr, effects::ParticleSystems* particles = nullptr);

} // namespace coney
