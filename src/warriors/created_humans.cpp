// SPDX-License-Identifier: GPL-3.0-or-later
#include "warriors/created_humans.h"

#include <algorithm>
#include <cstddef>
#include <utility>

namespace coney {

bool CreatedHumans::add(HumanCreation human) {
    if (m_humans.size() >= kCapacity) {
        return false;
    }
    m_humans.push_back(std::move(human));
    return true;
}

const HumanCreation* CreatedHumans::player(int index) const {
    // A hand-over's human first.
    if (index >= 0 && static_cast<std::size_t>(index) < m_players.size()) {
        if (const double handed = m_players.at(static_cast<std::size_t>(index)); handed != 0.0) {
            const auto found = std::ranges::find(m_humans, handed, &HumanCreation::handle);
            if (found != m_humans.end()) {
                return &*found;
            }
        }
    }
    const auto found = std::ranges::find(m_humans, index, &HumanCreation::playerIndex);
    return found == m_humans.end() ? nullptr : &*found;
}

void CreatedHumans::setPlayer(int index, double handle) {
    if (index < 0 || static_cast<std::size_t>(index) >= m_players.size() ||
        std::ranges::find(m_humans, handle, &HumanCreation::handle) == m_humans.end()) {
        return;
    }
    m_players.at(static_cast<std::size_t>(index)) = handle;
}

HumanCreation* CreatedHumans::find(double handle) {
    const auto found = std::ranges::find(m_humans, handle, &HumanCreation::handle);
    return found == m_humans.end() ? nullptr : &*found;
}

std::optional<world_objects::Placement> CreatedHumans::placement(double handle) const {
    const auto found = std::ranges::find(m_humans, handle, &HumanCreation::handle);
    if (found == m_humans.end()) {
        return std::nullopt;
    }
    if (found->teleported) {
        return found->teleported;
    }
    const auto& position = found->position;
    if (!position) {
        return std::nullopt;
    }
    return world_objects::Placement{.position = *position, .headingDegrees = found->headingDegrees};
}

} // namespace coney
