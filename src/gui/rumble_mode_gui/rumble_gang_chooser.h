// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <span>

#include "gui/rumble_mode_gui/rumble_data.h"
#include "warriors/game_state.h"

namespace coney::gui {

/// The member of a roster shown in slot `slot` (0-8, left to right) on the Choose Gangs screen: 8, 6, 4, 2, 0, 1, 3,
/// 5, 7, so the warchief (member 0) stands in the centre (CONEY_ASSERT that `slot` is below 9).
/// @orig 0x001ebfc0 RM_Roster_SlotToMember (RM_ChooseGangs.cpp)
[[nodiscard]] std::size_t rosterMemberInSlot(std::size_t slot);

/// Rotates `roster` one slot to the right in display order: each slot takes the member of the slot to its left and
/// the rightmost wraps to the leftmost, so member 2 (left of the centre) becomes the warchief.
/// @orig 0x001ec028 RM_GangData_RotateForward (RM_ChooseGangs.cpp)
void rotateRosterForward(RumbleGangEntry::Roster& roster);

/// Rotates `roster` one slot to the left in display order, the opposite of rotateRosterForward(): member 1 (right of
/// the centre) becomes the warchief.
/// @orig 0x001ec130 RM_GangData_RotateBack (RM_ChooseGangs.cpp)
void rotateRosterBack(RumbleGangEntry::Roster& roster);

/// The Choose Gangs screen's state over the gang list (RumbleData::gangs): two sides, each with its own cursor on the
/// list and its own copy of each gang's roster. The screen's input (RumbleMenu) decides what each command does; this
/// keeps the cursors, the rosters, the locks and which side chooses.
///
/// **Coney's choice** where the page is silent: side 2's cursor starts on the entry after side 1's (the fresh boot's
/// default is the list's first gang against its second, the BASEBALL FURIES against the ORPHANS). The cursor does not
/// wrap (move()).
///
/// Research: docs/research/frontend.md#rumble-data
class RumbleGangChooser {
  public:
    /// The two sides.
    static constexpr std::size_t kSides = 2;

    /// Starts over `gangs` (which must outlive the chooser's use): side 1 active, neither side locked.
    void start(std::span<RumbleGangEntry> gangs);

    /// Moves the active side's cursor one entry up (`down` false) or down, past the other side's gang unless
    /// `mayShare` (one fighter a side). Returns false, and stays, when that would leave the list: at either end, or
    /// when the only entry beyond is the other side's gang; also without gangs.
    /// @orig 0x001ef5a8 RM_ChooseGangs_MoveUp (RM_ChooseGangs.cpp)
    /// @orig 0x001ef6d8 RM_ChooseGangs_MoveDown (RM_ChooseGangs.cpp)
    bool move(bool down, bool mayShare);

    /// Rotates the active side's roster of the gang under its cursor one slot forward (rotateRosterForward()) or back.
    /// Does nothing without gangs.
    /// @coney-only the chooser's state: the original's input calls the rotation on the gang record itself
    void rotate(bool forward);

    /// Locks the active side; returns true when both sides are now locked, else makes the other side active. Does
    /// nothing and returns false without gangs.
    bool lock();

    /// Unlocks side `side`.
    void unlock(std::size_t side) { m_locked.at(side) = false; }

    /// Makes side `side` the one choosing.
    void setActive(std::size_t side) { m_active = side; }

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
