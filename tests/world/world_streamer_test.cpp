// SPDX-License-Identifier: GPL-3.0-or-later
#include "world/world_streamer.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <set>
#include <utility>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "core/error.h"
#include "support/streaming_fixtures.h"
#include "world/sector_budget.h"
#include "world/streamed_world.h"
#include "world/world_streams.h"

using Catch::Approx;
using coney::test::rowManifest;
using coney::test::rowOfSectors;
using coney::world::SectorBudget;
using coney::world::StreamedWorld;
using coney::world::StreamResult;
using coney::world::Vec3;

namespace {

// A PartStore that records what it is asked and fails the parts in `failing`.
class RecordingStore final : public coney::world::PartStore {
  public:
    std::expected<void, coney::Error> loadPart(std::size_t world, std::uint32_t part) override {
        loads.emplace_back(world, part);
        if (failing.contains(part)) {
            return coney::fail(coney::ErrorCode::Io, "unreadable");
        }
        return {};
    }
    void unloadPart(std::size_t world, std::uint32_t part) override { unloads.emplace_back(world, part); }

    std::vector<std::pair<std::size_t, std::uint32_t>> loads;
    std::vector<std::pair<std::size_t, std::uint32_t>> unloads;
    std::set<std::uint32_t> failing;
};

// A world of five sectors in a row, one per part, each part's heap 100 bytes.
StreamedWorld fiveParts() {
    return StreamedWorld::create("row", rowOfSectors({{0, 1}, {1, 2}, {2, 3}, {3, 4}, {4, 5}}, 5),
                                 rowManifest({100, 100, 100, 100, 100}))
        .value();
}

} // namespace

TEST_CASE("the budget reserves what fits and remembers its peak", "[sector_budget]") {
    SectorBudget budget(1000);
    CHECK(budget.reserve(600));
    CHECK_FALSE(budget.reserve(500));
    CHECK(budget.used() == 600);
    budget.release(600);
    CHECK(budget.freeBytes() == 1000);
    CHECK(budget.peak() == 600);
    CHECK(coney::world::worldLevelPoolBytes(1000) == std::uint64_t{256} * 1024);
    CHECK(coney::world::worldLevelPoolBytes(1'000'000) == 1'030'000);
    CHECK(coney::world::globalDataPoolBytes(1'000'000) == 1'010'000);
}

TEST_CASE("each decision loads the nearest missing part while it fits", "[world_streamer]") {
    StreamedWorld world = fiveParts();
    std::array<StreamedWorld*, 1> worlds{&world};
    // Inside sector 2 (part 3). The face-plane metric puts its neighbours at the same distance (the plane x = 20 is
    // as near as it is to the camera), and the lowest index wins a tie, so sector 1 (part 2) comes first.
    const std::array<Vec3, 1> camera{Vec3{25.0F, 5.0F, 5.0F}};
    SectorBudget budget(1000);
    RecordingStore store;

    const coney::world::StreamStep first = coney::world::updateStreaming(worlds, camera, 300.0F, budget, store, 40);
    CHECK(first.result == StreamResult::Loaded);
    CHECK(first.part == 2);
    CHECK(world.part(2).state == coney::world::PartState::Loaded);
    CHECK(world.sectors()[1].fadeEndMs == 1040);
    CHECK(budget.used() == 100);
    // One decision a frame: the next nearest comes next.
    const coney::world::StreamStep second = coney::world::updateStreaming(worlds, camera, 300.0F, budget, store, 40);
    CHECK(second.result == StreamResult::Loaded);
    CHECK(second.part == 3);
    for (int i = 0; i < 3; ++i) {
        (void)coney::world::updateStreaming(worlds, camera, 300.0F, budget, store, 40);
    }
    CHECK(store.loads.size() == 5);
    CHECK(coney::world::updateStreaming(worlds, camera, 300.0F, budget, store, 40).result == StreamResult::Idle);
}

TEST_CASE("without room the farthest unseen part goes, if it is clearly farther", "[world_streamer]") {
    StreamedWorld world = fiveParts();
    std::array<StreamedWorld*, 1> worlds{&world};
    SectorBudget budget(300); // three parts
    RecordingStore store;
    // Camera at the start of the row: parts 1, 2, 3 load.
    const std::array<Vec3, 1> start{Vec3{5.0F, 5.0F, 5.0F}};
    for (int i = 0; i < 3; ++i) {
        CHECK(coney::world::updateStreaming(worlds, start, 300.0F, budget, store, 0).result == StreamResult::Loaded);
    }
    // Part 4 does not fit; part 3 is the farthest loaded one but only 10 farther than part 4's sector... and part 1,
    // 2, 3 are all nearer than the wanted one, so nothing is more than 5.0 farther: no room.
    CHECK(coney::world::updateStreaming(worlds, start, 300.0F, budget, store, 0).result == StreamResult::NoRoom);
    CHECK(store.unloads.empty());

    // Fly to the far end: part 1 is now farthest and unseen, and much farther than the wanted part 4.
    const std::array<Vec3, 1> end{Vec3{45.0F, 5.0F, 5.0F}};
    const coney::world::StreamStep step = coney::world::updateStreaming(worlds, end, 300.0F, budget, store, 0);
    CHECK(step.result == StreamResult::Unloaded);
    CHECK(step.part == 1);
    CHECK(world.part(1).state == coney::world::PartState::Unloaded);
    CHECK(budget.used() == 200);
    // The room is used on the next decision.
    CHECK(coney::world::updateStreaming(worlds, end, 300.0F, budget, store, 0).result == StreamResult::Loaded);
}

TEST_CASE("a visible part is kept even when it is the farthest", "[world_streamer]") {
    StreamedWorld world = fiveParts();
    std::array<StreamedWorld*, 1> worlds{&world};
    SectorBudget budget(300);
    RecordingStore store;
    const std::array<Vec3, 1> start{Vec3{5.0F, 5.0F, 5.0F}};
    for (int i = 0; i < 3; ++i) {
        (void)coney::world::updateStreaming(worlds, start, 300.0F, budget, store, 0);
    }
    world.markVisible(0); // part 1 was seen last frame
    const std::array<Vec3, 1> end{Vec3{45.0F, 5.0F, 5.0F}};
    const coney::world::StreamStep step = coney::world::updateStreaming(worlds, end, 300.0F, budget, store, 0);
    CHECK(step.result == StreamResult::Unloaded);
    CHECK(step.part == 2);
}

TEST_CASE("the nearest missing sector of both worlds wins, and a failed read is not retried", "[world_streamer]") {
    StreamedWorld solid = StreamedWorld::create("s", rowOfSectors({{0, 1}, {1, 2}}, 2), rowManifest({10, 10})).value();
    StreamedWorld detail =
        StreamedWorld::create("d", rowOfSectors({{-1, 0}, {-1, 0}, {0, 1}}, 1), rowManifest({10})).value();
    std::array<StreamedWorld*, 2> worlds{&solid, &detail};
    const std::array<Vec3, 1> camera{Vec3{26.0F, 5.0F, 5.0F}}; // inside the d world's only sector
    SectorBudget budget(1000);
    RecordingStore store;
    store.failing = {1};
    const coney::world::StreamStep step = coney::world::updateStreaming(worlds, camera, 300.0F, budget, store, 0);
    CHECK(step.result == StreamResult::Failed);
    CHECK(step.world == 1);
    CHECK(budget.used() == 0);
    CHECK(detail.part(1).state == coney::world::PartState::Failed);
    // The s world's part 2 (x 10..20, 6 away) is next; part 1 fails too.
    CHECK(coney::world::updateStreaming(worlds, camera, 300.0F, budget, store, 0).part == 2);
    CHECK(coney::world::updateStreaming(worlds, camera, 300.0F, budget, store, 0).result == StreamResult::Failed);
    CHECK(coney::world::updateStreaming(worlds, camera, 300.0F, budget, store, 0).result == StreamResult::Idle);
}

TEST_CASE("the preload streams until what is missing lies beyond its radius", "[world_streamer]") {
    StreamedWorld world = fiveParts();
    std::array<StreamedWorld*, 1> worlds{&world};
    SectorBudget budget(1000);
    RecordingStore store;
    const std::array<Vec3, 1> camera{Vec3{5.0F, 5.0F, 5.0F}};
    // Radius 25: parts 1 to 3 come in (their sectors at most 17 away). The check after each pass reads the last
    // search, which found the sector just loaded, so part 4 (26 away) is loaded too before the preload sees that what
    // it went for lies beyond the radius, and stops: one part past the radius, as the original's loop does.
    const coney::world::PreloadResult result = coney::world::preloadWorlds(worlds, camera, 25.0F, budget, store, 0);
    CHECK(result.loaded == 4);
    CHECK(world.part(4).state == coney::world::PartState::Loaded);
    CHECK(world.part(5).state == coney::world::PartState::Unloaded);
    // No new search: the pending distance is still that of part 4's sector, although it is loaded now.
    CHECK(coney::world::nearestPendingDistance(worlds, camera) == Approx(std::sqrt(25.0F * 25.0F + 50.0F)));
}

TEST_CASE("the unload margin is added to the squared distance", "[world_streamer]") {
    // Three sectors around a camera at the origin (y and z from -1 to 1, so each adds 1 + 1 to a squared distance):
    // A (part 1) at x 21..22, B (part 2) at x 20..30, C (part 3, never unloaded) far away. A is loaded and the
    // budget holds one part, so B does not fit.
    coney::world::WorldStream layout;
    layout.partCount = 3;
    const auto add = [&layout](float x0, float x1, std::int32_t index, std::uint32_t part) {
        coney::world::WorldSector sector;
        sector.box = coney::world::Box{{x0, -1.0F, -1.0F}, {x1, 1.0F, 1.0F}};
        sector.plugin = coney::world::SectorPluginData{.streamedIndex = index, .part = part, .origin = {}};
        layout.sectors.push_back(sector);
    };
    add(21.0F, 22.0F, 0, 1);
    add(20.0F, 30.0F, 1, 2);
    add(500.0F, 510.0F, 2, 3);
    StreamedWorld world = StreamedWorld::create("margin", layout, rowManifest({100, 100, 100})).value();
    std::array<StreamedWorld*, 1> worlds{&world};
    SectorBudget budget(100);
    REQUIRE(budget.reserve(100));
    world.markPartLoaded(1, 0);
    RecordingStore store;
    // B is wanted at 20² + 2 = 402 squared; A is at 21² + 2 = 443, more than 402 + 5, so A goes. In plain units A is
    // only 21.05 against B's 20.05, within 5.0, and would stay.
    const std::array<Vec3, 1> camera{Vec3{0.0F, 0.0F, 0.0F}};
    const coney::world::StreamStep step = coney::world::updateStreaming(worlds, camera, 300.0F, budget, store, 0);
    CHECK(step.result == StreamResult::Unloaded);
    CHECK(step.part == 1);
}

TEST_CASE("nothing missing reads as FLT_MAX, not infinity", "[world_streamer]") {
    StreamedWorld world = fiveParts();
    std::array<StreamedWorld*, 1> worlds{&world};
    const std::array<Vec3, 1> camera{Vec3{5.0F, 5.0F, 5.0F}};
    // No search yet.
    CHECK(coney::world::nearestPendingDistance(worlds, camera) == std::numeric_limits<float>::max());
    SectorBudget budget(1000);
    RecordingStore store;
    while (coney::world::updateStreaming(worlds, camera, 300.0F, budget, store, 0).result == StreamResult::Loaded) {
    }
    CHECK(coney::world::nearestPendingDistance(worlds, camera) == std::numeric_limits<float>::max());
    // With nothing missing the draw distance grows to its ceiling.
    const float grown = coney::world::adjustDrawDistance(100.0F, {.pending = coney::world::kNoPendingDistance,
                                                                  .farClip = 115.0F,
                                                                  .seconds = 0.1F,
                                                                  .frameRate = 30.0F,
                                                                  .viewports = 1,
                                                                  .lowRateMode = false});
    CHECK(grown == Approx(101.05F));
}

TEST_CASE("the draw distance follows the missing scenery within its limits", "[world_streamer]") {
    using coney::world::adjustDrawDistance;
    using coney::world::DrawDistanceInputs;
    const DrawDistanceInputs base{.pending = 1000.0F,
                                  .farClip = 300.0F,
                                  .seconds = 1.0F / 30.0F,
                                  .frameRate = 30.0F,
                                  .viewports = 1,
                                  .lowRateMode = false};
    // Growing toward 300 by 10.5 a second.
    CHECK(adjustDrawDistance(100.0F, base) == Approx(100.0F + 10.5F / 30.0F));
    // Shrinking toward a near missing sector by 40.5 a second, but not below 60 - 10.
    DrawDistanceInputs near = base;
    near.pending = 20.0F;
    CHECK(adjustDrawDistance(100.0F, near) == Approx(100.0F - 40.5F / 30.0F));
    CHECK(adjustDrawDistance(51.0F, near) == Approx(50.0F));
    // Steps are capped at 0.1 s; the far clip is the ceiling.
    DrawDistanceInputs slowStep = near;
    slowStep.seconds = 2.0F;
    CHECK(adjustDrawDistance(100.0F, slowStep) == Approx(100.0F - 4.05F));
    DrawDistanceInputs lowClip = base;
    lowClip.farClip = 80.0F;
    CHECK(adjustDrawDistance(100.0F, lowClip) == Approx(80.0F));
    // A poor frame rate shrinks it by (29.5 - rate) * 10.5 a second.
    DrawDistanceInputs slow = base;
    slow.frameRate = 19.5F;
    CHECK(adjustDrawDistance(200.0F, slow) == Approx(200.0F - 10.0F * 10.5F / 30.0F));
}
