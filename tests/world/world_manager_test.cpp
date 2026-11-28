// SPDX-License-Identifier: GPL-3.0-or-later
#include "world/world_manager.h"

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "support/streaming_fixtures.h"
#include "world/sector_budget.h"
#include "world/streamed_world.h"

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

// Dropping the manager runs WorldManager_Unload's port keeping nothing, then gives back the global data; a moved-from
// manager holds nothing, so dropping it gives nothing back twice (docs/research/physics-world.md#u18-coney).
TEST_CASE("a dropped world manager unloads its level and a moved-from one gives nothing back", "[world_manager]") {
    // Expected: physics-world.md#u18-unload, confirmed (code)
    SectorBudget budget(10'000'000);
    std::optional<WorldManager> kept;
    {
        WorldManager manager(budget, kGlrSize);
        manager.loadLevel(kLevSize);
        kept.emplace(std::move(manager));
    } // the moved-from manager goes here
    CHECK(budget.used() == 1'010'000 + 2'060'000);
    kept.reset();
    CHECK(budget.used() == 0);
}

TEST_CASE("the boot's sector pools are the largest free block less 128 KB, the second at least 4 KB",
          "[world_manager][physics-unit-18]") {
    // Expected: physics-world.md#u18-create-pools, confirmed (code)
    const auto retail = coney::world::sectorPoolSizes(17'217'536 + 0x20000, 100'000);
    CHECK(retail.sectorPool == 17'217'536);
    CHECK(retail.sectorPool2 == 4'096);
    CHECK(coney::world::sectorPoolSizes(17'217'536 + 0x20000, 200'000).sectorPool2 == 68'928);
    // Expected: physics-world.md#u18-tests, confirmed (code)
    CHECK(worldLevelPoolBytes(100'000) == 262'144);
    CHECK(worldLevelPoolBytes(254'500) == 262'144);
    CHECK(worldLevelPoolBytes(254'524) == 262'159);
    CHECK(worldLevelPoolBytes(300'000) == 309'000);
    CHECK(globalDataPoolBytes(99) == 99);
    CHECK(globalDataPoolBytes(1'000) == 1'010);
}

TEST_CASE("the load priority is stored as given, and a level load sets 0.001", "[world_manager][physics-unit-18]") {
    // Expected: physics-world.md#u18-small, confirmed (code)
    SectorBudget budget(10'000'000);
    WorldManager manager(budget, kGlrSize);
    manager.setLoadPriority(10.0F);
    CHECK(manager.loadPriority() == 10.0F);
    manager.setLoadPriority(-3.0F);
    CHECK(manager.loadPriority() == -3.0F);
    manager.setLoadPriority(1e9F);
    CHECK(manager.loadPriority() == 1e9F);
    // Expected: physics-world.md#u18-load, confirmed (code)
    manager.loadLevel(kLevSize);
    CHECK(manager.loadPriority() == 0.001F);
}

TEST_CASE("resetting the visibility clears both worlds' marks", "[world_manager][physics-unit-18]") {
    // Expected: physics-world.md#u18-small, confirmed (code)
    auto solid = coney::world::StreamedWorld::create("tests", coney::test::rowOfSectors({{0, 1}, {1, 1}}, 1),
                                                     coney::test::rowManifest({1}));
    auto detail = coney::world::StreamedWorld::create("testd", coney::test::rowOfSectors({{0, 1}}, 1),
                                                      coney::test::rowManifest({1}));
    REQUIRE(solid.has_value());
    REQUIRE(detail.has_value());
    solid->markVisible(0);
    solid->markVisible(1);
    detail->markVisible(0);
    SectorBudget budget(10'000'000);
    WorldManager manager(budget, std::nullopt);
    manager.resetVisibility(*solid, &*detail);
    CHECK_FALSE(solid->sectors()[0].visible);
    CHECK_FALSE(solid->sectors()[1].visible);
    CHECK_FALSE(detail->sectors()[0].visible);
    // A level of one world has no detail world.
    solid->markVisible(1);
    manager.resetVisibility(*solid, nullptr);
    CHECK_FALSE(solid->sectors()[1].visible);
}

TEST_CASE("the sector pool's largest free block is the budget's free bytes", "[world_manager][physics-unit-18]") {
    // Expected: physics-world.md#u18-coney, confirmed (code)
    SectorBudget budget(10'000'000);
    WorldManager manager(budget, kGlrSize);
    std::uint64_t largest = 0;
    CHECK(&manager.sectorPool(&largest) == &budget);
    CHECK(largest == 10'000'000 - 1'010'000);
    CHECK(&manager.sectorPool(nullptr) == &budget);
}
