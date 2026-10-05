// SPDX-License-Identifier: GPL-3.0-or-later
#include "world_objects/flags.h"

#include <algorithm>
#include <utility>

namespace coney::world_objects {

namespace {

// The parent of `flag` as a live object, or nothing when it has none or `locate` cannot find it.
std::optional<Placement> parentOf(const WorldFlag& flag, const ObjectLocator& locate) {
    if (!locate) {
        return std::nullopt;
    }
    return locate(flag.parent);
}

} // namespace

void WorldFlags::createPool(std::size_t flags) {
    m_poolStart = m_flags.size();
    m_capacity = flags + kPoolExtra;
}

const WorldFlag& WorldFlags::add(double handle, std::string_view name, const std::array<float, 3>& position,
                                 float headingDegrees, int kind, int kind2) {
    if (poolCount() >= m_capacity) {
        ++m_overflows;
    }
    WorldFlag flag;
    flag.handle = handle;
    flag.name = std::string(name.substr(0, kNameLength));
    flag.position = position;
    flag.headingDegrees = headingDegrees;
    flag.kind = kind;
    flag.kind2 = kind2;
    m_flags.push_back(std::move(flag));
    return m_flags.back();
}

const WorldFlag* WorldFlags::find(double handle) const {
    const auto found = std::ranges::find(m_flags, handle, &WorldFlag::handle);
    return found == m_flags.end() ? nullptr : &*found;
}

WorldFlag* WorldFlags::find(double handle) {
    const auto found = std::ranges::find(m_flags, handle, &WorldFlag::handle);
    return found == m_flags.end() ? nullptr : &*found;
}

std::optional<double> WorldFlags::findByName(std::string_view name) const {
    // The stored name is at most kNameLength characters, so a longer one can never equal it.
    for (std::size_t i = m_poolStart; i < m_flags.size(); ++i) {
        if (m_flags[i].name == name) {
            return m_flags[i].handle;
        }
    }
    return std::nullopt;
}

std::array<float, 3> WorldFlags::position(const WorldFlag& flag, const ObjectLocator& locate) {
    if (const std::optional<Placement> parent = parentOf(flag, locate)) {
        return parent->position;
    }
    return flag.position;
}

float WorldFlags::headingDegrees(const WorldFlag& flag, const ObjectLocator& locate) {
    if (const std::optional<Placement> parent = parentOf(flag, locate)) {
        return parent->headingDegrees;
    }
    return flag.headingDegrees;
}

void WorldFlags::clear() {
    m_flags.clear();
    m_poolStart = 0;
    m_capacity = 0;
    m_overflows = 0;
}

} // namespace coney::world_objects
