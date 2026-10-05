// SPDX-License-Identifier: GPL-3.0-or-later
#include "gui/rumble_mode_gui/rumble_gang_chooser.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

#include "core/assert.h"

namespace coney::gui {

namespace {

// Copies a gang's name the way the menu does, at most 32 bytes.
// @orig 0x001fe070 RumbleMode_SetGang1Name (unknown)
// @orig 0x001fe0d8 RumbleMode_SetGang2Name (unknown)
void setGangName(std::string& name, const std::string& gang) { name = gang.substr(0, RumbleSetup::kGangNameLength); }

} // namespace

void RumbleGangChooser::start(std::span<RumbleGangEntry> gangs) {
    m_gangs = gangs;
    m_cursor = {0, gangs.empty() ? 0 : 1 % gangs.size()};
    m_locked = {false, false};
    m_active = 0;
}

void RumbleGangChooser::move(int step) {
    if (m_gangs.empty()) {
        return;
    }
    const auto count = static_cast<std::ptrdiff_t>(m_gangs.size());
    const std::ptrdiff_t next = (static_cast<std::ptrdiff_t>(m_cursor.at(m_active)) + step % count + count) % count;
    m_cursor.at(m_active) = static_cast<std::size_t>(next);
}

void RumbleGangChooser::rotate(bool toLeft) {
    if (m_gangs.empty()) {
        return;
    }
    RumbleGangEntry::Roster& roster = m_gangs[m_cursor.at(m_active)].rosters.at(m_active);
    if (toLeft) {
        std::ranges::rotate(roster, roster.begin() + 1);
    } else {
        std::ranges::rotate(roster, roster.end() - 1);
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

bool RumbleGangChooser::unlock() {
    // The side locked last is the one that is not active (both are never locked here: a lock of both leaves the
    // screen).
    const std::size_t other = 1 - m_active;
    if (m_locked.at(m_active)) {
        m_locked.at(m_active) = false;
        return true;
    }
    if (m_locked.at(other)) {
        m_locked.at(other) = false;
        m_active = other;
        return true;
    }
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
