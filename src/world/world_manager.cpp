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
        m_globalData.take(*m_budget, globalDataPoolBytes(*globalDataSize));
    }
    // The global data's contents (warriors.glr's chunks) are read by the platform's loaders; Coney's stand-in only
    // counts the pool here.
}

void WorldManager::loadLevel(std::optional<std::uint64_t> levelFileSize) {
    // The World Level Pool first, before the worlds and the level file: 103 % of the level file, at least 256 KB.
    // Coney's choice: a name without a level file (the world viewer's world-only names) reserves none, where the
    // original has a level file for every level.
    if (levelFileSize) {
        m_levelPool.take(*m_budget, worldLevelPoolBytes(*levelFileSize));
    }
}

void WorldManager::unload(bool keepLevelFile) {
    // The worlds, their parts and the water go on both paths; they are LevelScenery's and charge their own heaps.
    // With the level file kept, the World Level Pool stays.
    if (keepLevelFile) {
        return;
    }
    // The level object and the path data are freed into the level pool (LevelScenery's); then the pool's block goes
    // back to the Sector Pool.
    m_levelPool.giveBack();
}

WorldManager::Reservation::Reservation(Reservation&& other) noexcept
    : m_budget(std::exchange(other.m_budget, nullptr)), m_bytes(std::exchange(other.m_bytes, 0)) {}

WorldManager::Reservation& WorldManager::Reservation::operator=(Reservation&& other) noexcept {
    if (this != &other) {
        giveBack();
        m_budget = std::exchange(other.m_budget, nullptr);
        m_bytes = std::exchange(other.m_bytes, 0);
    }
    return *this;
}

WorldManager::Reservation::~Reservation() { giveBack(); }

void WorldManager::Reservation::take(SectorBudget& budget, std::uint64_t bytes) {
    // Every load is paired with an unload keeping nothing, so a pool is never made twice.
    CONEY_ASSERT(m_budget == nullptr);
    if (budget.reserve(bytes)) {
        m_budget = &budget;
        m_bytes = bytes;
    }
}

void WorldManager::Reservation::giveBack() {
    if (m_budget != nullptr) {
        m_budget->release(std::exchange(m_bytes, 0));
        m_budget = nullptr;
    }
}

} // namespace coney::world
