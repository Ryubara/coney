// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace coney::gui {

/// The global strings (docs/research/gui.md#strings) the pause and mission-failed menus show, by id. Names are their
/// English text as docs/research/pause.md gives it.
///
/// Research: docs/research/pause.md#the-items, docs/research/pause.md#the-items-screens
namespace pause_strings {
inline constexpr std::uint32_t kYes = 0x3b;
inline constexpr std::uint32_t kNo = 0x3c;
inline constexpr std::uint32_t kUsageObjectives = 0x19; ///< up/down select, triangle back
inline constexpr std::uint32_t kUsageFailed = 0x1b;     ///< the mission-failed screen's usage line
inline constexpr std::uint32_t kTitleA = 0xe7;          ///< the top header's mission title (no `<ROBJ_N>`)
inline constexpr std::uint32_t kTitleB = 0xe8;          ///< the top header's mission title (with `<ROBJ_N>`)
inline constexpr std::uint32_t kStats = 0xf8;
inline constexpr std::uint32_t kOptions = 0xf7;
inline constexpr std::uint32_t kControls = 0xf9;
inline constexpr std::uint32_t kObjectives = 0xfa;
inline constexpr std::uint32_t kRules = 0xfb;
inline constexpr std::uint32_t kResume = 0xfc;
inline constexpr std::uint32_t kQuit = 0xfd;
inline constexpr std::uint32_t kRestart = 0xfe;
inline constexpr std::uint32_t kReplay = 0xff;
inline constexpr std::uint32_t kQuitQuestion = 0x102;       ///< "quit? all progress on this level lost"
inline constexpr std::uint32_t kCheckpointQuestion = 0x103; ///< restart from the last checkpoint?
inline constexpr std::uint32_t kReplayQuestion = 0x105;     ///< "replay? all progress on this level lost"
inline constexpr std::uint32_t kOverview = 0x106;
inline constexpr std::uint32_t kBonusObjectives = 0x107;
inline constexpr std::uint32_t kCurrentObjectives = 0x108;
inline constexpr std::uint32_t kNone = 0x109; ///< an empty objectives list
inline constexpr std::uint32_t kMainMenuHelp = 0x10d;
inline constexpr std::uint32_t kHangoutHelp = 0x10e;
inline constexpr std::uint32_t kMainMenu = 0x10f;
inline constexpr std::uint32_t kToRumbleMode = 0x110;
inline constexpr std::uint32_t kToHangout = 0x111;
inline constexpr std::uint32_t kRestartLevel = 0x112;
inline constexpr std::uint32_t kRestartLevelHelp = 0x113;
inline constexpr std::uint32_t kRestartCheckpoint = 0x114;
inline constexpr std::uint32_t kRestartCheckpointHelp = 0x115;
inline constexpr std::uint32_t kOptionsHeader = 0x116;
inline constexpr std::uint32_t kControlsHeader = 0x173;
inline constexpr std::uint32_t kLastCheckpoint = 0xd7; ///< mission failed
inline constexpr std::uint32_t kRestartLevelFailed = 0xd8;
inline constexpr std::uint32_t kQuitFailed = 0xd9;
inline constexpr std::uint32_t kUnknownReason = 0xda;
inline constexpr std::uint32_t kToHangoutFailed = 0xdf;
} // namespace pause_strings

/// The codes of the pause menu's grid items, which PauseMenu_SelectItem acts on.
enum class PauseItem : std::uint8_t {
    Objectives = 0, ///< Rules in a Rumble level.
    Stats = 1,
    Options = 2,
    Controls = 3,
    Restart = 4, ///< Replay in a Rumble level.
    Resume = 5,
    Quit = 6,
};

/// One grid item as the pause menu adds it.
struct PauseItemEntry {
    PauseItem code = PauseItem::Objectives;
    std::uint32_t text = 0; ///< Its global string.
    bool separator = false; ///< Followed by `" : "`.
};

/// The pause menu's grid: its items and how many go in each row.
struct PauseGridLayout {
    std::vector<PauseItemEntry> items;
    std::vector<std::size_t> rows;
};

/// The level number of the hangout (`level95`), which has no Restart item.
inline constexpr int kHangoutLevel = 95;

/// Whether level number `level` is a Rumble level (100 or more, `0x0041d160`).
/// @orig 0x0041d160 Level_IsRumble (unknown)
[[nodiscard]] constexpr bool isRumbleLevel(int level) { return level >= 100; }

/// Whether level number `level` uses the Armies of the Night pause menu (60-69, `0x0041d110`).
/// @orig 0x0041d110 Level_IsArmies (unknown)
[[nodiscard]] constexpr bool usesArmiesPauseMenu(int level) { return level >= 60 && level <= 69; }

/// Whether the pause menu's Quit offers "To Hangout" in level number `level` (`+0x1bac`): one of the 15 story levels
/// `0x0041d1b0` lists. The other source, a level the stats object marks, is not modelled (Coney has no stats object).
///
/// Research: docs/research/pause.md#the-items-screens
/// @orig 0x0041d1b0 Level_OffersHangout (unknown)
[[nodiscard]] bool offersHangout(int level);

/// The grid `PauseMenu_Open` builds for level number `level`: Objectives : Stats : Options / Controls : Restart :
/// Resume : Quit in a story level (no Restart in `level95`); Rules : Options Controls : Replay : Resume : Quit in a
/// Rumble level. Rows of 3 and 4, or 3 and 3 in `level95` and Rumble levels.
///
/// Research: docs/research/pause.md#the-items, docs/research/pause.md#layout-gui-coordinates
/// @orig 0x001dbee0 PauseMenu_Open (unknown)
[[nodiscard]] PauseGridLayout pauseGrid(int level);

} // namespace coney::gui
