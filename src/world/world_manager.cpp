// SPDX-License-Identifier: GPL-3.0-or-later
#include "world/world_manager.h"

#include <utility>

namespace coney::world {

// Stubs for the test commit: no reservation is made or given back.
WorldManager::WorldManager(SectorBudget& budget, std::optional<std::uint64_t> /*globalDataSize*/) : m_budget(&budget) {}

WorldManager::~WorldManager() = default;

WorldManager::WorldManager(WorldManager&& other) noexcept : m_budget(std::exchange(other.m_budget, nullptr)) {}

WorldManager& WorldManager::operator=(WorldManager&& other) noexcept {
    m_budget = std::exchange(other.m_budget, nullptr);
    return *this;
}

void WorldManager::loadLevel(std::optional<std::uint64_t> /*levelFileSize*/) {}

void WorldManager::unload(bool /*keepLevelFile*/) {}

void WorldManager::releaseAll() {}

} // namespace coney::world
