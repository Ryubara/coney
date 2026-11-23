// SPDX-License-Identifier: GPL-3.0-or-later
#include "world/world_manager.h"

#include <cstdint>
#include <optional>
#include <utility>

#include <catch2/catch_test_macros.hpp>

#include "world/sector_budget.h"

using coney::world::globalDataPoolBytes;
using coney::world::SectorBudget;
using coney::world::worldLevelPoolBytes;
using coney::world::WorldManager;

namespace {

// Sizes of a `warriors.glr` and a level file, synthetic. The pools are 101 % and 103 % of them, the level's at least
// 256 KB (WorldManager_Construct 0x0040d900, WorldManager_LoadLevel 0x0040dbb8:
// docs/research/physics-world.md#unit-18).
constexpr std::uint64_t kGlrSize = 1'000'000;
constexpr std::uint64_t kLevSize = 2'000'000;

} // namespace

// WorldManager_Construct makes the Global Data Pool once, before any level
// (docs/research/physics-world.md#u18-construct).
TEST_CASE("a world manager reserves the global data pool when it is made", "[world_manager]") {
    // Expected: physics-world.md#u18-construct, confirmed (code)
    SectorBudget budget(10'000'000);
    const WorldManager manager(budget, kGlrSize);
    CHECK(manager.globalDataBytes() == 1'010'000);
    CHECK(budget.used() == 1'010'000);
    CHECK(manager.levelPoolBytes() == 0);
}

// WorldManager_Unload with keep 0 gives back the World Level Pool, so the budget returns to what it was before
// WorldManager_LoadLevel; the Global Data Pool stays (docs/research/physics-world.md#u18-unload).
TEST_CASE("a level load then an unload keeping nothing returns the budget to where it was", "[world_manager]") {
    // Expected: physics-world.md#u18-load, confirmed (code)
    // Expected: physics-world.md#u18-unload, confirmed (code)
    SectorBudget budget(10'000'000);
    WorldManager manager(budget, kGlrSize);
    const std::uint64_t before = budget.used();
    for (int level = 0; level < 3; ++level) {
        manager.loadLevel(kLevSize);
        CHECK(manager.levelPoolBytes() == 2'060'000);
        CHECK(budget.used() == before + 2'060'000);
        manager.unload(false);
        CHECK(manager.levelPoolBytes() == 0);
        CHECK(budget.used() == before);
    }
    CHECK(manager.globalDataBytes() == globalDataPoolBytes(kGlrSize));
}

// WorldManager_Unload with keep 1 leaves the World Level Pool and the level file in it; a later unload with keep 0
// gives it back (docs/research/physics-world.md#u18-unload).
TEST_CASE("an unload that keeps the level file keeps its pool", "[world_manager]") {
    // Expected: physics-world.md#u18-unload, confirmed (code)
    SectorBudget budget(10'000'000);
    WorldManager manager(budget, kGlrSize);
    const std::uint64_t before = budget.used();
    manager.loadLevel(1000); // a small level file: the pool's 256 KB minimum
    manager.unload(true);
    CHECK(manager.levelPoolBytes() == worldLevelPoolBytes(1000));
    CHECK(budget.used() == before + std::uint64_t{256} * 1024);
    manager.unload(false);
    CHECK(budget.used() == before);
}

// Coney's manager goes with the level's scenery, and takes the Global Data Pool and any level still loaded with it
// (docs/research/physics-world.md#u18-coney).
TEST_CASE("a world manager gives back everything it reserved when it goes", "[world_manager]") {
    // Expected: physics-world.md#u18-coney, confirmed (code)
    SectorBudget budget(10'000'000);
    {
        WorldManager manager(budget, kGlrSize);
        manager.loadLevel(kLevSize);
        manager.unload(true);
        WorldManager moved(std::move(manager));
        CHECK(moved.levelPoolBytes() == 2'060'000);
    }
    CHECK(budget.used() == 0);
    {
        WorldManager first(budget, kGlrSize);
        WorldManager second(budget, std::nullopt);
        second.loadLevel(kLevSize);
        first = std::move(second); // first's global data goes; second's level pool moves over
        CHECK(budget.used() == 2'060'000);
    }
    CHECK(budget.used() == 0);
}
