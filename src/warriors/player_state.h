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

/// The screen colour a store sets (`EnterStore` / `ExitStore`): colour preset 10 of the two per-player colour
/// controllers at `0x005fdeb8`, switched to with a 0.25 s blend, and preset 9 again on leaving. That the controllers
/// are the players' screen tint is inferred. **Coney's stand-in:** Coney has no colour controllers, so this keeps what
/// a renderer would blend to (open item on docs/research/crimes.md).
struct StoreTint {
    /// The preset outside a store, and inside.
    static constexpr int kOutside = 9;
    static constexpr int kInStore = 10;
    /// The blend between them, seconds.
    static constexpr double kBlendSeconds = 0.25;

    std::array<float, 4> colour{}; ///< Preset 10's colour {r, g, b, a}, each 0-1.
    int preset = kOutside;         ///< The preset both controllers blend to.
};

/// What the game keeps about the players besides their humans, as the scripts reach it: the inventories (in the
/// original at `W_GameState + 0x480`), the statistics (`0x006fe490`), the unlockables' records (`0x006fe998`; their
/// bits are the profile's, GameState::saved), the mission stopwatch (`*0x0051504c`), the crime fields of the game
/// state, the Lua pad handlers, a store's screen colour and the inventory's and stereo theft's callbacks. GameState
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
    StoreTint storeTint;   ///< The store colour.

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
