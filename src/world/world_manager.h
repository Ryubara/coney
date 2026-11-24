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

    /// Unloads the level keeping nothing (unload()), as the original's level exit does, then gives back the `Global
    /// Data Pool`. A moved-from manager holds nothing and gives nothing back.
    /// @coney-only the original's manager lives as long as the game and never frees its global data; Coney's lives as
    /// long as the level's scenery, so its reservation goes with it
    ~WorldManager();

    WorldManager(const WorldManager&) = delete;
    WorldManager& operator=(const WorldManager&) = delete;
    /// Takes `other`'s reservations; `other` then holds none.
    /// @coney-only the original has one world manager for the whole game and never moves it
    WorldManager(WorldManager&& other) noexcept = default;
    /// Gives back this manager's reservations, then takes `other`'s; `other` then holds none.
    /// @coney-only the original has one world manager for the whole game and never replaces it
    WorldManager& operator=(WorldManager&& other) noexcept = default;

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
    [[nodiscard]] std::uint64_t globalDataBytes() const { return m_globalData.bytes(); }
    /// Bytes reserved for the `World Level Pool`; 0 while no level is loaded.
    [[nodiscard]] std::uint64_t levelPoolBytes() const { return m_levelPool.bytes(); }

  private:
    // Bytes held in a budget until they are given back or the holder goes; a move hands them over, so a moved-from
    // manager holds nothing and gives nothing back twice.
    class Reservation {
      public:
        Reservation() = default;
        // @coney-only the pools' reservations stand for the original's clumps, which nothing moves
        Reservation(Reservation&& other) noexcept;
        // @coney-only the pools' reservations stand for the original's clumps, which nothing moves
        Reservation& operator=(Reservation&& other) noexcept;
        Reservation(const Reservation&) = delete;
        Reservation& operator=(const Reservation&) = delete;
        // @coney-only gives the bytes back with their holder
        ~Reservation();

        // Reserves `bytes` in `budget` when they fit; holds nothing otherwise. Nothing must be held already.
        // @coney-only the budget's bookkeeping for a clump the original makes
        void take(SectorBudget& budget, std::uint64_t bytes);
        // Gives the bytes back, if any, and holds nothing.
        // @coney-only the budget's bookkeeping for a clump the original destroys
        void giveBack();
        [[nodiscard]] std::uint64_t bytes() const { return m_bytes; }

      private:
        SectorBudget* m_budget = nullptr; // null while nothing is held
        std::uint64_t m_bytes = 0;
    };

    SectorBudget* m_budget;   // the budget both pools are reserved in
    Reservation m_globalData; // the `Global Data Pool` (WorldManager `+0x3c`); declared first, so given back last
    Reservation m_levelPool;  // the `World Level Pool` (`+0x38`)
};

} // namespace coney::world
