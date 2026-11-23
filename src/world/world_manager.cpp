// SPDX-License-Identifier: GPL-3.0-or-later
#include "world/world_manager.h"

#include <utility>

#include "core/assert.h"

namespace coney::world {

WorldManager::WorldManager(SectorBudget& budget, std::optional<std::uint64_t> globalDataSize) : m_budget(&budget) {
    // The pack queue, the draw list, the level and world pointers and the visibility latch start empty: Coney's
    // stand-in holds none of them here yet (the worlds and the level object are LevelScenery's).
    // The Global Data Pool: 101 % of warriors.glr, made in the Sector Pool and never freed by the original.
    if (globalDataSize) {
        const std::uint64_t bytes = globalDataPoolBytes(*globalDataSize);
        if (m_budget->reserve(bytes)) {
            m_globalData = bytes;
        }
    }
    // The global data's contents (warriors.glr's chunks) are read by the platform's loaders; Coney's stand-in only
    // counts the pool here.
}

WorldManager::~WorldManager() { releaseAll(); }

WorldManager::WorldManager(WorldManager&& other) noexcept
    : m_budget(std::exchange(other.m_budget, nullptr)), m_globalData(std::exchange(other.m_globalData, 0)),
      m_levelPool(std::exchange(other.m_levelPool, 0)) {}

WorldManager& WorldManager::operator=(WorldManager&& other) noexcept {
    if (this != &other) {
        releaseAll();
        m_budget = std::exchange(other.m_budget, nullptr);
        m_globalData = std::exchange(other.m_globalData, 0);
        m_levelPool = std::exchange(other.m_levelPool, 0);
    }
    return *this;
}

void WorldManager::loadLevel(std::optional<std::uint64_t> levelFileSize) {
    CONEY_ASSERT(m_budget != nullptr && m_levelPool == 0);
    // The World Level Pool first, before the worlds and the level file: 103 % of the level file, at least 256 KB.
    // Coney's choice: a name without a level file (the world viewer's world-only names) reserves none, where the
    // original has a level file for every level.
    if (levelFileSize) {
        const std::uint64_t bytes = worldLevelPoolBytes(*levelFileSize);
        if (m_budget->reserve(bytes)) {
            m_levelPool = bytes;
        }
    }
}

void WorldManager::unload(bool keepLevelFile) {
    // The worlds, their parts and the water go on both paths; they are LevelScenery's and charge their own heaps.
    // With the level file kept, the World Level Pool stays.
    if (keepLevelFile || m_budget == nullptr) {
        return;
    }
    // The level object and the path data are freed into the level pool (LevelScenery's); then the pool's block goes
    // back to the Sector Pool.
    m_budget->release(std::exchange(m_levelPool, 0));
}

void WorldManager::releaseAll() {
    if (m_budget == nullptr) {
        return;
    }
    unload(false);
    m_budget->release(std::exchange(m_globalData, 0));
}

} // namespace coney::world
