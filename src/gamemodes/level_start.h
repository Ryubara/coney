// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

#include "scripting/script_system.h"
#include "warriors/created_humans.h"
#include "warriors/game_state.h"

namespace coney {

/// Where a level starts player 1: what the level's own script created for the checkpoint. No data file holds a player
/// start; the level script makes player 1 with `HuCreate` while `InitLevel` runs it, before the preload, so the start
/// is whatever that call was given (docs/research/characters.md#level-starts).
struct LevelStart {
    std::string level;  ///< The level (`level99`).
    int checkpoint = 1; ///< The checkpoint the script read with `GetCheckPoint()`.
    /// Player 1 as the script created him, with a position; nothing when the script made no player 1 Coney can place
    /// (no `HuCreate` with player index 1, or one whose position came from a binding Coney lacks, such as the hub's
    /// and the Rumble arenas' flags).
    std::optional<HumanCreation> player;
};

/// What running a level's scripts for its start did, for the log and the tests.
struct LevelScriptRun {
    LevelStart start;
    std::uint64_t scriptErrors = 0; ///< Script errors in the run (ScriptSystem::errors()).
    std::uint64_t skippedCalls = 0; ///< Calls of bindings Coney lacks in the run (ScriptSystem::skippedCalls()).
    std::size_t humans = 0;         ///< Humans the scripts created.
};

/// InitLevel's script step for `level` in `scripts`, whose state must exist: forgets the humans of the level before,
/// runs `global.lua` then `<level>.lua` (ScriptSystem::enterLevel()), whose `HuCreate` calls fill `humans`, and returns
/// player 1's start for the checkpoint in `state` (`W_GameState + 0x33a`).
[[nodiscard]] LevelStart runLevelScript(script::ScriptSystem& scripts, const GameState& state, CreatedHumans& humans,
                                        std::string_view level);

/// The story's way into `level` at `checkpoint` without the menus, for `--play-level`: a script system of its own
/// running what the original runs before a level's script, in its order: the preloads (the legal screen's, which fill
/// the level table), then a fresh Lua state (the front end's unload), then `SetCheckPoint(checkpoint)`, the level's
/// index and runLevelScript(). Scripts are read through `source`; `log` gets the scripts' lines. Script errors are
/// counted, not fatal: the start is whatever the scripts got to.
///
/// Research: docs/research/level-loading.md#story-into-level99, docs/research/scripting.md#life-of-the-lua-state
[[nodiscard]] LevelScriptRun runLevelScriptAlone(const script::ScriptSource& source, std::string_view level,
                                                 int checkpoint, const std::function<void(std::string_view)>& log);

} // namespace coney
