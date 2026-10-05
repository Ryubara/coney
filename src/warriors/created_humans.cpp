// SPDX-License-Identifier: GPL-3.0-or-later
#include "warriors/created_humans.h"

#include <algorithm>
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
    const auto found = std::ranges::find(m_humans, index, &HumanCreation::playerIndex);
    return found == m_humans.end() ? nullptr : &*found;
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
