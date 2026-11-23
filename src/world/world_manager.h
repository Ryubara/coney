// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <optional>

#include "world/sector_budget.h"

namespace coney::world {

/// The world manager's share of the `Sector Pool`: the `Global Data Pool` it makes for `warriors.glr` when it is made,
/// and the `World Level Pool` each level load makes for the level's file. Coney allocates normally, so both are
/// reservations in a SectorBudget, which decides when streamed parts are evicted; they must come back exactly when a
/// level unloads, or every later level streams with less room than the original's.
///
/// The level's streamed worlds and its level object are not held here: they are the platform's (LevelScenery), and
/// charge and give back their own heaps.
///
/// Research: docs/research/physics-world.md#unit-18, docs/research/physics-world.md#u18-coney
class WorldManager {
  public:
    /// A manager charging `budget` (which must outlive it), reserving the `Global Data Pool` for a `warriors.glr` of
    /// `globalDataSize` bytes (globalDataPoolBytes()); none when the disc has no such file. A reservation that does not
    /// fit is not made.
    /// @orig 0x0040d900 WorldManager_Construct (WorldManagerPS2.cpp)
    WorldManager(SectorBudget& budget, std::optional<std::uint64_t> globalDataSize);

    /// Unloads the level (unload() keeping nothing) and gives back the `Global Data Pool`.
    /// @coney-only the original's manager lives as long as the game and never frees its global data; Coney's lives as
    /// long as the level's scenery, so its reservation goes with it
    ~WorldManager();

    WorldManager(const WorldManager&) = delete;
    WorldManager& operator=(const WorldManager&) = delete;
    /// Takes `other`'s reservations; `other` then holds none.
    WorldManager(WorldManager&& other) noexcept;
    /// Gives back this manager's reservations, then takes `other`'s; `other` then holds none.
    /// @coney-only the original has one world manager for the whole game and never replaces it
    WorldManager& operator=(WorldManager&& other) noexcept;

    /// Reserves the `World Level Pool` for a level file of `levelFileSize` bytes (worldLevelPoolBytes()); none for a
    /// level without a level file (the world viewer's world-only names). A reservation that does not fit is not made.
    /// The level's pool must not be held already (CONEY_ASSERT): every load is paired with an unload keeping nothing.
    /// @orig 0x0040dbb8 WorldManager_LoadLevel (WorldManagerPS2.cpp)
    void loadLevel(std::optional<std::uint64_t> levelFileSize);

    /// Unloads the level: with `keepLevelFile` false, gives back the `World Level Pool`; with it true the pool stays,
    /// as the level file does through a level change's outro movie. The `Global Data Pool` stays either way.
    /// @orig 0x0040f5b8 WorldManager_Unload (WorldManagerPS2.cpp)
    void unload(bool keepLevelFile);

    /// Bytes reserved for the `Global Data Pool`; 0 for none.
    [[nodiscard]] std::uint64_t globalDataBytes() const { return m_globalData; }
    /// Bytes reserved for the `World Level Pool`; 0 while no level is loaded.
    [[nodiscard]] std::uint64_t levelPoolBytes() const { return m_levelPool; }

  private:
    // Gives back every reservation and holds none: what the destructor and a move assignment do.
    // @coney-only the original never gives back its global data
    void releaseAll();

    SectorBudget* m_budget;         // null once moved from
    std::uint64_t m_globalData = 0; // the `Global Data Pool`'s reservation (WorldManager `+0x3c`)
    std::uint64_t m_levelPool = 0;  // the `World Level Pool`'s reservation (`+0x38`)
};

} // namespace coney::world
