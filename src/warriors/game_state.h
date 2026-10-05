// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

#include "core/game_random.h"
#include "core/language.h"
#include "warriors/level_table.h"

namespace coney {

/// The Rumble mode's set-up as the Rumble menu (mode 0x11) leaves it for the arena: the 23 16-bit values
/// `GetRumbleModeData` copies into the arena script's table, and the arena's level number
/// (docs/research/frontend.md#quick-rumble).
struct RumbleSetup {
    /// Values `GetRumbleModeData` copies.
    static constexpr std::size_t kValues = 23;
    std::array<std::uint16_t, kValues> values{}; ///< `0x0063eec0`: game type, gangs, options (meanings not traced).
    int levelNumber = 0;                         ///< `0x0050f4e8`: the chosen arena's level number (`+0x04`).
};

/// The part of the game state (`W_GameState`, 0x57c0 bytes in the original) that the front end and its scripts use:
/// the language, the difficulties, the current level and section, the level table, the saved script numbers, plus a
/// few globals of the game the bindings share (the random index, the start callback, the Rumble set-up). The bindings
/// read and write it (`GetLanguage`, `GetLevelId`, `SetCheckPoint`, `CfgLevelName`, `GetLUASaveDataFloat`); the level
/// flow selects levels in it.
///
/// Research: docs/research/scripting.md#bindings-whose-results-the-front-end-needs,
/// docs/research/frontend.md#the-level-table, docs/research/scripting.md#errors-in-a-fresh-state
struct GameState {
    /// Slots of saved script numbers (`GetLUASaveDataFloat`).
    static constexpr std::size_t kLuaSaveFloats = 8;

    Language language = Language::English; ///< `+0x120`: 0 on the NTSC-U disc.
    double difficulty = 1;                 ///< `+0x154` (`GetDifficulty`): 1 at the front end.
    double profileDifficulty = 1;          ///< `+0x43c` (`GetProfileDifficulty`): 1 at the front end.
    double checkPoint =
        1; ///< `+0x33a` (`GetCheckPoint`, `SetCheckPoint`): the level's section; 1 from the constructor.
    std::size_t currentLevel = 0; ///< `+0x56dc` (`GetCurrentLevelIndex`): 0, the front end, at start-up.
    LevelTable levels;            ///< `+0x14d4`: the level records.
    /// `+0x570c`: the saved script numbers, slot n at index n - 1; the constructor zeroes them.
    std::array<float, kLuaSaveFloats> luaSaveFloats{};

    // Globals of the original kept here because every binding state shares them.
    GameRandom random;             ///< The random table's index (`0x006eb880`) and the table.
    std::string startGameCallback; ///< `0x005e6d88`: the Lua function `InitLevel` calls when the level is ready.
    RumbleSetup rumble;            ///< The Rumble menu's set-up.
};

} // namespace coney
