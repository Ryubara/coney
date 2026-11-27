// SPDX-License-Identifier: GPL-3.0-or-later
#include "gui/rumble_mode_gui/rumble_gang_chooser.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

#include "core/assert.h"

namespace coney::gui {

namespace {

// The slots' members, left to right (the table RM_Roster_SlotToMember builds).
constexpr std::array<std::size_t, RumbleGangEntry::kMembers> kSlotMembers{8, 6, 4, 2, 0, 1, 3, 5, 7};

// Copies a gang's name into the set-up's name of its side, as the original's copy does.
// @orig 0x001fe070 RumbleMode_SetGang1Name (unknown)
// @orig 0x001fe0d8 RumbleMode_SetGang2Name (unknown)
// Coney's choice: at most 32 bytes, the size of the original's field, which copies the whole name (its length,
// with no limit) and so would write past the field for a longer one.
void setGangName(std::string& name, const std::string& gang) { name = gang.substr(0, RumbleSetup::kGangNameLength); }

} // namespace

std::size_t rosterMemberInSlot(std::size_t slot) {
    CONEY_ASSERT(slot < kSlotMembers.size());
    return kSlotMembers.at(slot);
}

void rotateRosterForward(RumbleGangEntry::Roster& roster) {
    // From the right end leftwards each slot takes its left neighbour's member; the rightmost goes to the leftmost.
    const std::uint32_t rightmost = roster.at(rosterMemberInSlot(RumbleGangEntry::kMembers - 1));
    for (std::size_t slot = RumbleGangEntry::kMembers - 1; slot > 0; --slot) {
        roster.at(rosterMemberInSlot(slot)) = roster.at(rosterMemberInSlot(slot - 1));
    }
    roster.at(rosterMemberInSlot(0)) = rightmost;
}

void rotateRosterBack(RumbleGangEntry::Roster& roster) {
    // From the left end rightwards each slot takes its right neighbour's member; the leftmost goes to the rightmost.
    const std::uint32_t leftmost = roster.at(rosterMemberInSlot(0));
    for (std::size_t slot = 0; slot + 1 < RumbleGangEntry::kMembers; ++slot) {
        roster.at(rosterMemberInSlot(slot)) = roster.at(rosterMemberInSlot(slot + 1));
    }
    roster.at(rosterMemberInSlot(RumbleGangEntry::kMembers - 1)) = leftmost;
}

void RumbleGangChooser::start(std::span<RumbleGangEntry> gangs) {
    m_gangs = gangs;
    m_cursor = {0, gangs.empty() ? 0 : 1 % gangs.size()};
    m_locked = {false, false};
    m_active = 0;
}

bool RumbleGangChooser::move(bool down, bool mayShare) {
    // The original selects the entry past the end when the list is empty; Coney refuses the move.
    if (m_gangs.empty()) {
        return false;
    }
    const std::size_t cursor = m_cursor.at(m_active);
    const std::size_t last = m_gangs.size() - 1;
    if (down ? cursor == last : cursor == 0) {
        return false;
    }
    std::size_t next = down ? cursor + 1 : cursor - 1;
    // Two sides may not hold one gang unless each fights alone: step past the other side's, or stay at the end.
    if (!mayShare && next == m_cursor.at(1 - m_active)) {
        if (down ? next == last : next == 0) {
            return false;
        }
        next = down ? next + 1 : next - 1;
    }
    m_cursor.at(m_active) = next;
    return true;
}

void RumbleGangChooser::rotate(bool forward) {
    if (m_gangs.empty()) {
        return;
    }
    RumbleGangEntry::Roster& roster = m_gangs[m_cursor.at(m_active)].rosters.at(m_active);
    if (forward) {
        rotateRosterForward(roster);
    } else {
        rotateRosterBack(roster);
    }
}

bool RumbleGangChooser::lock() {
    if (m_gangs.empty()) {
        return false;
    }
    m_locked.at(m_active) = true;
    if (m_locked[0] && m_locked[1]) {
        return true;
    }
    m_active = 1 - m_active;
    return false;
}

void RumbleGangChooser::apply(RumbleSetup& setup) const {
    CONEY_ASSERT(m_locked[0] && m_locked[1] && !m_gangs.empty());
    constexpr std::array<std::size_t, kSides> kPak{RumbleSetup::kGang1Pak, RumbleSetup::kGang2Pak};
    constexpr std::array<std::size_t, kSides> kTypes{RumbleSetup::kGang1Types, RumbleSetup::kGang2Types};
    for (std::size_t side = 0; side < kSides; ++side) {
        const RumbleGangEntry& gang = m_gangs[m_cursor.at(side)];
        setup.values.at(kPak.at(side)) = static_cast<std::uint16_t>(gang.id - 1);
        for (std::size_t i = 0; i < RumbleGangEntry::kMembers; ++i) {
            setup.values.at(kTypes.at(side) + i) = static_cast<std::uint16_t>(gang.rosters.at(side).at(i));
        }
        setGangName(setup.gangNames.at(side), gang.name);
    }
}

const RumbleGangEntry* RumbleGangChooser::gang(std::size_t side) const {
    return m_gangs.empty() ? nullptr : &m_gangs[m_cursor.at(side)];
}

const RumbleGangEntry::Roster* RumbleGangChooser::roster(std::size_t side) const {
    return m_gangs.empty() ? nullptr : &m_gangs[m_cursor.at(side)].rosters.at(side);
}

} // namespace coney::gui
