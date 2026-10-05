// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include "scripting/script_system.h"
#include "warriors/created_humans.h"
#include "warriors/game_state.h"
#include "world_objects/flags.h"

namespace coney {

/// Where a level starts player 1: what the level's own script created for the checkpoint. No data file holds a player
/// start; the level script makes player 1 with `HuCreate` while `InitLevel` runs it, at a literal position or at a
/// flag, and may teleport him onto a flag before play starts (docs/research/characters.md#level-starts).
struct LevelStart {
    std::string level;  ///< The level (`level99`).
    int checkpoint = 1; ///< The checkpoint the script read with `GetCheckPoint()`.
    /// Player 1 as the scripts left him (HumanCreation::teleported set when a `TeleportToFlag` moved him); nothing when
    /// the scripts made no player 1 with a position.
    std::optional<HumanCreation> player;
};

/// What running a level's scripts for its start did, for the log and the tests.
struct LevelScriptRun {
    LevelStart start;
    std::uint64_t scriptErrors = 0; ///< Script errors in the run (ScriptSystem::errors()).
    std::uint64_t skippedCalls = 0; ///< Calls of bindings Coney lacks in the run (ScriptSystem::skippedCalls()).
    std::size_t humans = 0;         ///< Humans the scripts created.
    std::size_t flags = 0;          ///< World flags the scripts and InitLevel made.
};

/// The names of the two flags `InitLevel` adds after the level script, at the origin with heading 0
/// (docs/research/flags.md#sources).
inline constexpr std::string_view kCrimeSceneFlag = "CrimeScene";
inline constexpr std::string_view kGangCallFlag = "GangCall";

/// InitLevel's script steps for `level` in `scripts`, whose state must exist: forgets the humans and flags of the
/// level before, runs `global.lua` then `<level>.lua` (ScriptSystem::enterLevel()), whose `HuCreate` and `AddFlag`
/// calls fill `humans` and `flags`, adds InitLevel's two flags, then calls the start callback the script set
/// (`SetStartGameCallback`, kept in `state` and cleared once called), and returns player 1's start for the checkpoint
/// in `state` (`W_GameState + 0x33a`).
///
/// **Coney's choice:** the original calls the start callback at InitLevel's end, after the level's loading and preload
/// (step 13); Coney calls it right after the script steps, before the loading, because Coney places the player from
/// what the scripts made and nothing the callback reads is loaded by those steps in Coney.
///
/// Research: docs/research/level-loading.md#initlevel, docs/research/flags.md#player-starts
[[nodiscard]] LevelStart runLevelScript(script::ScriptSystem& scripts, GameState& state, CreatedHumans& humans,
                                        world_objects::WorldFlags& flags, std::string_view level);

/// What a level run alone (runLevelScriptAlone()) starts with that the menus would otherwise have set.
struct LevelScriptOptions {
    /// The game's random table, read from the player's disc (GameRandom); empty: Coney's stand-in generator.
    std::span<const std::uint32_t> randomTable;
    /// The Rumble menu's set-up an arena reads (`GetRumbleModeData`); nothing leaves it all 0.
    std::optional<RumbleSetup> rumble;
};

/// The story's way into `level` at `checkpoint` without the menus, for `--play-level`: a script system of its own
/// running what the original runs before a level's script, in its order: the preloads (the legal screen's, which fill
/// the level table), then a fresh Lua state (the front end's unload), then `SetCheckPoint(checkpoint)`, the level's
/// index and runLevelScript(); then the scripts' frames of the first second of play (**Coney's choice**, so that what
/// the start schedules, such as the hub's walk 100 ms in, happens before the play mode places the player). Scripts are
/// read through `source`; `log` gets the scripts' lines. Script errors are counted, not fatal: the start is whatever
/// the scripts got to.
///
/// Research: docs/research/level-loading.md#story-into-level99, docs/research/scripting.md#life-of-the-lua-state
[[nodiscard]] LevelScriptRun runLevelScriptAlone(const script::ScriptSource& source, std::string_view level,
                                                 int checkpoint, const std::function<void(std::string_view)>& log,
                                                 const LevelScriptOptions& options = {});

} // namespace coney
