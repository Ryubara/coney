// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "core/game_random.h"
#include "core/language.h"
#include "warriors/character_rules.h"
#include "warriors/level_table.h"
#include "warriors/player_state.h"
#include "warriors/profile_record.h"
#include "warriors/story_state.h"
#include "warriors/unlockables.h"

namespace coney {

/// The Rumble mode's set-up as the Rumble menu (mode 0x11) leaves it for the arena: the 23 16-bit values
/// `GetRumbleModeData` copies into the arena script's table, the two gangs' names `GetRumbleModeGangName` returns, and
/// the arena's level number (docs/research/frontend.md#rumble-setup).
struct RumbleSetup {
    /// Values `GetRumbleModeData` copies.
    static constexpr std::size_t kValues = 23;
    /// The longest gang name kept: the menu copies at most 32 bytes.
    static constexpr std::size_t kGangNameLength = 32;
    /// Where each value lives (the C index; the arena script reads index + 1).
    static constexpr std::size_t kGameMode = 0;    ///< 3 one player against the computer, 2 co-op, 1 versus.
    static constexpr std::size_t kGameType = 1;    ///< The mode's `RM_*` number (12 is "1 ON 1").
    static constexpr std::size_t kGangSize = 2;    ///< Fighters per side.
    static constexpr std::size_t kGang1Pak = 3;    ///< Side 1's gang pack - 1.
    static constexpr std::size_t kGang2Pak = 4;    ///< Side 2's gang pack - 1.
    static constexpr std::size_t kGang1Types = 5;  ///< Side 1's nine character types start here.
    static constexpr std::size_t kGang2Types = 14; ///< Side 2's nine character types start here.
    static constexpr std::size_t kGangMembers = 9; ///< Character types per side.

    std::array<std::uint16_t, kValues> values{}; ///< `0x0063eec0`: players, mode, gang size, gangs, their types.
    /// `0x0063eef0` and `0x0063ef10`: side 1's and side 2's gang names; empty until the gangs are confirmed.
    std::array<std::string, 2> gangNames;
    int levelNumber = 0; ///< `0x0050f4e8`: the chosen arena's level number (`+0x04`).
    /// `0x0063ef30`: the chosen mode's title with a `:` before it (`:1 ON 1`), which the Game Mode screen copies.
    std::string modeLabel;
    /// `0x0063ef6c`, `0x0063ef70`, `0x0063ef74`: whether the chosen mode offers one player, co-op and versus.
    std::array<bool, 3> playerOptions{};
    /// `0x0063ef78`: the chosen mode has preset fighters, so no gangs are chosen.
    bool presetGangs = false;
};

/// The game mode `SetGameMode` sets (docs/references/bindings/level.md#setgamemode): 0 in the story; each Rumble
/// arena passes its own. What the three parameters select is an open question (docs/research/rumble.md).
struct GameModeSetting {
    std::uint32_t mode = 0;     ///< `+0x158`: `GetGameMode`; the hand-over's kind-0 fallback needs 0.
    std::uint32_t a = 0;        ///< `+0x15c`: the arenas pass 3.
    std::uint32_t b = 0;        ///< `+0x160`: the arenas pass 19.
    std::uint32_t gangSize = 0; ///< `+0x164`: the arenas pass their gang size.
    bool versus = false;        ///< `+0x56f0`: set for modes 1 and 2 (inferred: two players).
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
    double profileDifficulty = 1;          ///< `+0x43c` (`GetProfileDifficulty`): 1 at the front end; PM_Difficulty.
    bool subtitles = false;                ///< `+0x438`: subtitles on, PM_Subtitles' choice.
    /// `+0x57a4`: the brightness `Gamma_Set` keeps, 0-100; PM_Light changes it. Coney's start value is PM_Light's 40.
    int brightness = 40;
    /// Two players: what PM_NumPlayers passes to `0x00419ac0` (the field it sets is not on the page).
    bool twoPlayers = false;
    double checkPoint =
        1; ///< `+0x33a` (`GetCheckPoint`, `SetCheckPoint`): the level's section; 1 from the constructor.
    std::size_t currentLevel = 0; ///< `+0x56dc` (`GetCurrentLevelIndex`): 0, the front end, at start-up.
    LevelTable levels;            ///< `+0x14d4`: the level records.
    /// `+0x570c`: the saved script numbers, slot n at index n - 1; the constructor zeroes them.
    std::array<float, kLuaSaveFloats> luaSaveFloats{};
    /// The rest of what a profile saves (the banked money `+0x480`, the options, the unlockables' bits, the script
    /// flags `+0x572c`, the mission bests, the Rumble data): a fresh profile's until one is loaded
    /// (docs/research/save.md#record).
    SavedProgress saved;

    // Globals of the original kept here because every binding state shares them.
    GameRandom random;             ///< The random table's index (`0x006eb880`) and the table.
    std::string startGameCallback; ///< `0x005e6d88`: the Lua function `InitLevel` calls when the level is ready.
    RumbleSetup rumble;            ///< The Rumble menu's set-up.
    CharacterRules characters; ///< The characters' rules the scripts set (`CfgPlayerMugging`, `CfgRageHandlers`...).
    /// The unlockables manager (`0x006fe998`) as the Rumble menu asks it: a fresh profile's until Coney has saves.
    Unlockables unlockables = Unlockables::freshProfile();
    /// The inventories, statistics, unlockables' records, stopwatch, crime fields and Lua pad handlers the bindings act
    /// on (docs/research/player-state.md).
    PlayerState player;
    /// What the story missions' scripts set beyond the characters' rules (the Warrior commands' callback, the music
    /// switches, the tagging set-up...).
    StoryState story;
    /// `SetGameMode`'s mode and parameters.
    GameModeSetting gameMode;
    /// `+0x431` (`WCEnableAutomaticSwitching`): the game may move the player to another gang member by itself
    /// (inferred from the name; its reader is not on the page, so Coney only keeps it). **Coney choice** until set: on.
    bool autoSwitch = true;
    /// `0x005104f8` (`HuForceEnableReticule`): every player's reticule drawn at full strength. **Coney stand-in**:
    /// Coney draws no reticules yet, so it is only kept.
    bool forceReticules = false;
    /// `0x005109ac` (`CNSEnableMissionInfo`): nothing in the original reads it.
    bool missionInfo = false;
    /// The world manager's precache queue (`+0x0c`, `QueueFileToPrecache`): the files the next `PrecacheWorld` loads.
    std::vector<std::string> precacheQueue;
};

} // namespace coney
