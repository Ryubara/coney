// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <span>

#include "gui/rumble_mode_gui/rumble_data.h"
#include "warriors/game_state.h"

namespace coney::gui {

/// The Choose Gangs screen's state over the gang list (RumbleData::gangs): two sides, each with its own cursor on the
/// list, chosen one after the other. Up and down move the active side's cursor, left and right rotate that side's
/// roster of the gang under it (which picks the warchief, its first member), accept locks the side and passes to the
/// other, and back unlocks the side locked last.
///
/// **Coney's choices** where the page is silent: side 2's cursor starts on the entry after side 1's (the fresh boot's
/// default is the list's first gang against its second, the BASEBALL FURIES against the ORPHANS); the cursor wraps
/// from the last gang to the first and back; rotating left makes the second member the warchief, rotating right the
/// last.
///
/// Research: docs/research/frontend.md#rumble-data
class RumbleGangChooser {
  public:
    /// The two sides.
    static constexpr std::size_t kSides = 2;

    /// Starts over `gangs` (which must outlive the chooser's use): side 1 active, neither side locked.
    void start(std::span<RumbleGangEntry> gangs);

    /// Moves the active side's cursor by `step` entries (-1 up, 1 down). Does nothing without gangs.
    void move(int step);

    /// Rotates the active side's roster of the gang under its cursor one place left (`toLeft`) or right. Does nothing
    /// without gangs.
    /// @orig 0x001ec130 RM_ChooseGangs_RotateLeft (RM_ChooseGangs.cpp)
    /// @orig 0x001ec028 RM_ChooseGangs_RotateRight (RM_ChooseGangs.cpp)
    void rotate(bool toLeft);

    /// Locks the active side; returns true when both sides are now locked, else makes the other side active. Does
    /// nothing and returns false without gangs.
    bool lock();

    /// Unlocks the side locked last and makes it active; returns false, changing nothing, when no side is locked.
    bool unlock();

    /// Writes the chosen gangs into `setup`: each side's pack (gang id - 1), its nine character types (the low 16 bits
    /// of that side's roster) and its name (at most 32 bytes). Both sides must be locked (CONEY_ASSERT).
    void apply(RumbleSetup& setup) const;

    /// The side choosing now (0 or 1).
    [[nodiscard]] std::size_t activeSide() const { return m_active; }
    /// Whether side `side` is locked.
    [[nodiscard]] bool locked(std::size_t side) const { return m_locked.at(side); }
    /// The gang under side `side`'s cursor; null without gangs.
    [[nodiscard]] const RumbleGangEntry* gang(std::size_t side) const;
    /// Side `side`'s roster of the gang under its cursor; null without gangs.
    [[nodiscard]] const RumbleGangEntry::Roster* roster(std::size_t side) const;

  private:
    std::span<RumbleGangEntry> m_gangs;
    std::array<std::size_t, kSides> m_cursor{};
    std::array<bool, kSides> m_locked{};
    std::size_t m_active = 0;
};

} // namespace coney::gui
