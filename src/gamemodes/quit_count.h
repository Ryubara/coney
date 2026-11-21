// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "gui/pause_menu/pause_menu.h"

namespace coney {

/// Whether `outcome` leaves the level for somewhere else: one of the four quits (QuitToHangout, QuitToRumbleQuick,
/// QuitToRumbleHangout, QuitToMainMenu), which a network host tells its guests of (docs/guides/multiplayer.md#levels).
/// A restart or a resume is no quit.
/// @coney-only the quits network play counts; the original keeps no count
[[nodiscard]] constexpr bool isQuit(gui::PauseOutcome outcome) {
    using gui::PauseOutcome;
    return outcome == PauseOutcome::QuitToHangout || outcome == PauseOutcome::QuitToRumbleQuick ||
           outcome == PauseOutcome::QuitToRumbleHangout || outcome == PauseOutcome::QuitToMainMenu;
}

/// The quits one screen (the pause menu, the mission-failed, Rumble result or Armies game-over screen) has chosen,
/// for network play (docs/guides/multiplayer.md#levels). A screen keeps its choice until it opens again, which clears
/// it, so its owner shows the counter whether the choice is a quit now, every step, and a quit counts once, when it is
/// first seen.
/// @coney-only the quits network play counts; the original keeps no count
class QuitCount {
  public:
    /// Notes whether the screen's choice is a quit now; returns the quits counted so far, this one included.
    /// @coney-only the quits network play counts; the original keeps no count
    std::uint64_t see(bool quitting) {
        if (quitting && !m_seen) {
            ++m_count;
        }
        m_seen = quitting;
        return m_count;
    }

  private:
    std::uint64_t m_count = 0;
    bool m_seen = false; // the choice was a quit when see() last looked
};

} // namespace coney
