// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>

#include "core/language.h"
#include "warriors/level_table.h"

namespace coney {

/// The part of the game state (`W_GameState`, 0x57c0 bytes in the original) that the front end and its scripts use:
/// the language, the difficulties, the current level and section, and the level table. The bindings read and write it
/// (`GetLanguage`, `GetLevelId`, `SetCheckPoint`, `CfgLevelName`); the level flow selects levels in it.
///
/// Research: docs/research/scripting.md#bindings-whose-results-the-front-end-needs,
/// docs/research/frontend.md#the-level-table
struct GameState {
    Language language = Language::English; ///< `+0x120`: 0 on the NTSC-U disc.
    double difficulty = 1;                 ///< `+0x154` (`GetDifficulty`): 1 at the front end.
    double profileDifficulty = 1;          ///< `+0x43c` (`GetProfileDifficulty`): 1 at the front end.
    double checkPoint =
        1; ///< `+0x33a` (`GetCheckPoint`, `SetCheckPoint`): the level's section; 1 from the constructor.
    std::size_t currentLevel = 0; ///< `+0x56dc` (`GetCurrentLevelIndex`): 0, the front end, at start-up.
    LevelTable levels;            ///< `+0x14d4`: the level records.
};

} // namespace coney
