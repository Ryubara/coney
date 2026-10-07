// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <string>

#include "core/pad_handlers.h"
#include "warriors/crime_reports.h"
#include "warriors/inventory.h"
#include "warriors/player_stats.h"
#include "warriors/stop_watch.h"
#include "warriors/unlock_records.h"

namespace coney {

/// What the game keeps about the players besides their humans, as the scripts reach it: the inventories (in the
/// original at `W_GameState + 0x480`), the statistics (`0x006fe490`), the unlockables' records (`0x006fe998`; their
/// bits are the profile's, GameState::saved), the mission stopwatch (`*0x0051504c`), the crime fields of the game
/// state, the Lua pad handlers and the inventory's and stereo theft's callbacks. GameState
/// holds one.
///
/// `SetCheckPoint` takes a checkpoint copy of the inventories and the statistics (`0x0041e0b8`, `0x00422c60`) that a
/// restart from the checkpoint puts back (saveCheckpoint(), restoreCheckpoint()).
///
/// Research: docs/research/player-state.md, docs/research/scripting.md#level99
struct PlayerState {
    Inventory inventory;   ///< The two players' items and money.
    PlayerStats stats;     ///< The mission's statistics and points.
    UnlockRecords unlocks; ///< The unlockables' records.
    StopWatch stopWatch;   ///< The mission stopwatch.
    CrimeReports crimes;   ///< Reporting, responders, wanted timers.
    PadHandlers pads;      ///< The Lua pad handlers.

    /// `CfgInventoryCallback(fn)` (inventory `+0xfd4`): called when a player picks up an inventory item.
    std::string pickupCallback;
    /// `CfgHuInventoryCallback(fn)` (inventory `+0xff4`): called with (player index, item) after pickupCallback.
    std::string huInventoryCallback;
    /// `CfgMoneyCallback(fn)` (inventory `+0x1034`): called with (player index, amount) whenever money changes.
    std::string moneyCallback;
    /// `CfgSetSteroTheftHandler(fn)` (`0x0051027c`): called with the human and the car when a stereo is stolen.
    std::string stereoTheftHandler;
    /// `CfgMultiplayerJoin(on)` (game state `+0x56e8`): a second player may join.
    bool multiplayerJoin = false;
    /// `SetMultiplayerCallback(fn)` (game state `+0x3a4`): called with (human, joined) when the two-player sync
    /// (`0x0041a460`) makes a Warrior player 2 or drops him; empty for none. **Coney stand-in**: Coney has one player,
    /// so the sync never runs and the function is kept, not called.
    std::string multiplayerCallback;

    /// Takes the checkpoint copy of the inventories and the statistics.
    void saveCheckpoint() {
        m_checkpointInventory = inventory;
        m_checkpointStats = stats;
    }
    /// Puts the checkpoint copy back (a restart from the checkpoint).
    void restoreCheckpoint() {
        inventory = m_checkpointInventory;
        stats = m_checkpointStats;
    }

  private:
    Inventory m_checkpointInventory;
    PlayerStats m_checkpointStats;
};

} // namespace coney
