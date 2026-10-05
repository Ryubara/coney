// SPDX-License-Identifier: GPL-3.0-or-later

// A check against the player's own disc: every level's streamed worlds are loaded as the world viewer loads them and
// streamed headless, one decision a frame, while a scripted camera visits the centre of every streamed sector of the
// level in turn. Each frame checks the streaming's invariants: the budget is never exceeded and always equals what is
// loaded, every loaded part has all its atomics and nothing else is resident, a freed part was not seen last frame,
// was not its world's last part and was more than the margin farther than the sector wanted. It runs twice: with the
// default budget on every level, and with a tight one (the worlds plus 2 MB) on the largest levels, which forces
// evictions. It runs only when the environment variable CONEY_DISC names the disc and skips otherwise; it prints
// counts only, never data (LEGAL.md). docs/research/world.md has the results.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <format>
#include <limits>
#include <string>
#include <utility>
#include <vector>

#include <SDL3/SDL_stdinc.h>
#include <catch2/catch_test_macros.hpp>

#include "camera/camera_lens.h"
#include "core/game_timer.h"
#include "fileio/disc.h"
#include "fileio/wad.h"
#include "platform/render_engine.h"
#include "platform/world_set.h"
#include "world/sector_budget.h"
#include "world/streamed_world.h"
#include "world/view_frustum.h"
#include "world/world_streamer.h"

namespace {

using coney::world::StreamResult;

// Totals over every level streamed.
struct StreamingTotals {
    std::uint64_t levels = 0;
    std::uint64_t loadFailures = 0; // levels whose worlds did not load
    std::uint64_t frames = 0;
    std::uint64_t loads = 0;
    std::uint64_t unloads = 0;
    std::uint64_t noRoom = 0;
    std::uint64_t failedParts = 0;
    std::uint64_t violations = 0; // broken invariants, printed as they happen (the first few)
    std::uint64_t partsEverLoaded = 0;
    std::uint64_t parts = 0;
    std::uint64_t maxResident = 0;
    std::uint64_t peakBudget = 0;
};

// Records a broken invariant and prints the first few.
void violation(StreamingTotals& totals, const std::string& what) {
    if (++totals.violations <= 10) {
        std::printf("  violation: %s\n", what.c_str());
    }
}

// Checks what must hold after every frame: the budget and the resident atomics match the loaded parts.
void checkState(const coney::platform::WorldSet& set, const coney::world::SectorBudget& budget,
                std::uint64_t worldHeaps, const std::string& level, StreamingTotals& totals) {
    std::uint64_t expected = worldHeaps;
    std::size_t loadedSectors = 0;
    const auto worlds = set.worlds();
    for (std::size_t w = 0; w < worlds.size(); ++w) {
        for (std::uint32_t part = 1; part <= worlds[w]->partCount(); ++part) {
            const coney::world::WorldPart& record = worlds[w]->part(part);
            if (record.state != coney::world::PartState::Loaded) {
                continue;
            }
            expected += record.sizes.heapSize;
            loadedSectors += record.sectors.size();
            for (const std::uint32_t sector : record.sectors) {
                if (set.atomic(w, sector) == nullptr) {
                    violation(totals, std::format("{}: a loaded part's sector has no atomic", level));
                }
            }
        }
    }
    if (budget.used() != expected || budget.used() > budget.capacity()) {
        violation(totals, std::format("{}: budget {} in use, {} expected, capacity {}", level, budget.used(), expected,
                                      budget.capacity()));
    }
    if (set.residentAtomics() != loadedSectors) {
        violation(totals, std::format("{}: {} atomics resident for {} loaded sectors", level, set.residentAtomics(),
                                      loadedSectors));
    }
    totals.maxResident = std::max<std::uint64_t>(totals.maxResident, set.residentAtomics());
}

// Streams one level's worlds under the camera path and checks every frame. `extraBudget` is added to the worlds' own
// heaps to size the budget; 0 means the default budget.
void streamLevel(const coney::io::Wad& wad, const std::string& level, std::uint64_t extraBudget,
                 StreamingTotals& totals) {
    auto names = coney::platform::worldNamesFor(wad, level);
    if (!names) {
        return;
    }
    ++totals.levels;
    // Size the budget: the default, or the worlds' heaps (measured with a generous first load) plus the extra.
    std::uint64_t capacity = coney::world::kSectorPoolSize;
    if (extraBudget > 0) {
        coney::world::SectorBudget probe(coney::world::kSectorPoolSize);
        auto measured = coney::platform::WorldSet::load(wad, *names, probe, false);
        if (!measured) {
            ++totals.loadFailures;
            return;
        }
        capacity = probe.used() + extraBudget;
    }
    coney::world::SectorBudget budget(capacity);
    auto set = coney::platform::WorldSet::load(wad, *names, budget, false);
    if (!set) {
        ++totals.loadFailures;
        std::printf("  %s: %s\n", level.c_str(), set.error().message.c_str());
        return;
    }
    const std::uint64_t worldHeaps = budget.used();
    const auto worlds = (*set)->worlds();
    std::vector<std::vector<bool>> everLoaded;
    for (const coney::world::StreamedWorld* world : worlds) {
        everLoaded.emplace_back(world->partCount() + 1, false);
        totals.parts += world->partCount();
    }

    // The path: the centre of every streamed sector of every world, three frames at each, looking along +z.
    std::vector<coney::world::Vec3> path;
    for (const coney::world::StreamedWorld* world : worlds) {
        for (const coney::world::StreamedSector& sector : world->sectors()) {
            const coney::world::Box& box = sector.box;
            path.push_back(
                {(box.min.x + box.max.x) * 0.5F, (box.min.y + box.max.y) * 0.5F, (box.min.z + box.max.z) * 0.5F});
        }
    }
    // The player camera: its far clip caps the draw distance, its view window shapes the visibility pass.
    const coney::camera::CameraLens& lens = coney::camera::kPlayerCameraLens;
    const coney::camera::ViewWindow window = coney::camera::viewWindow(lens);
    float drawDistance = lens.farClip;
    std::uint64_t frame = 0;
    for (const coney::world::Vec3& stop : path) {
        for (int repeat = 0; repeat < 3; ++repeat, ++frame) {
            const std::array<coney::world::Vec3, 1> cameras{stop};
            const std::uint64_t nowMs = frame * 1000 / 30;
            // Remember last frame's visibility, to check that a freed part was unseen.
            std::vector<std::vector<bool>> seen;
            for (const coney::world::StreamedWorld* world : worlds) {
                std::vector<bool>& marks = seen.emplace_back();
                for (const coney::world::StreamedSector& sector : world->sectors()) {
                    marks.push_back(sector.visible);
                }
            }
            const coney::world::StreamStep step =
                coney::world::updateStreaming(worlds, cameras, drawDistance, budget, **set, nowMs);
            switch (step.result) {
            case StreamResult::Loaded:
                ++totals.loads;
                everLoaded[step.world][step.part] = true;
                break;
            case StreamResult::Unloaded: {
                ++totals.unloads;
                const coney::world::StreamedWorld& world = *worlds[step.world];
                if (step.part == world.partCount()) {
                    violation(totals, std::format("{}: the last part was freed", level));
                }
                float nearest = std::numeric_limits<float>::infinity();
                for (const std::uint32_t sector : world.part(step.part).sectors) {
                    if (seen[step.world][sector]) {
                        violation(totals, std::format("{}: a part seen last frame was freed", level));
                    }
                    nearest = std::min(nearest, coney::world::cameraDistanceSq(world.sectors()[sector].box, cameras));
                }
                // The margin is added to the squared distance of the wanted sector, as the original does.
                if (!(nearest > step.distance * step.distance + coney::world::kUnloadMargin)) {
                    violation(totals, std::format("{}: a part within the margin was freed", level));
                }
                break;
            }
            case StreamResult::NoRoom:
                ++totals.noRoom;
                break;
            case StreamResult::Failed:
                ++totals.failedParts;
                std::printf("  %s: %s\n", level.c_str(), step.error.c_str());
                break;
            case StreamResult::Idle:
                break;
            }
            // The rest of the viewer's frame: the draw distance and the visibility pass.
            drawDistance = coney::world::adjustDrawDistance(
                drawDistance,
                coney::world::DrawDistanceInputs{.pending = coney::world::nearestPendingDistance(worlds, cameras),
                                                 .farClip = lens.farClip,
                                                 .seconds = 1.0F / 30.0F,
                                                 .frameRate = 30.0F,
                                                 .viewports = 1,
                                                 .lowRateMode = false});
            coney::world::CameraPose pose;
            pose.position = stop;
            const coney::world::ViewFrustum frustum(pose, window.halfWidth, window.halfHeight, lens.nearClip,
                                                    drawDistance);
            for (coney::world::StreamedWorld* world : worlds) {
                world->findVisibleSectors(frustum, true);
            }
            checkState(**set, budget, worldHeaps, level, totals);
        }
    }
    totals.frames += frame;
    totals.peakBudget = std::max(totals.peakBudget, budget.peak());
    for (const std::vector<bool>& loaded : everLoaded) {
        totals.partsEverLoaded += static_cast<std::uint64_t>(std::ranges::count(loaded, true));
    }
}

// Prints one run's totals.
void printTotals(const char* run, const StreamingTotals& totals) {
    std::printf("%s: %llu levels (%llu failed to load), %llu frames; %llu parts read, %llu freed, %llu frames short of "
                "room, %llu failed; %llu of %llu parts loaded at least once; at most %llu atomics resident; budget "
                "peak %llu; %llu violations\n",
                run, static_cast<unsigned long long>(totals.levels),
                static_cast<unsigned long long>(totals.loadFailures), static_cast<unsigned long long>(totals.frames),
                static_cast<unsigned long long>(totals.loads), static_cast<unsigned long long>(totals.unloads),
                static_cast<unsigned long long>(totals.noRoom), static_cast<unsigned long long>(totals.failedParts),
                static_cast<unsigned long long>(totals.partsEverLoaded), static_cast<unsigned long long>(totals.parts),
                static_cast<unsigned long long>(totals.maxResident), static_cast<unsigned long long>(totals.peakBudget),
                static_cast<unsigned long long>(totals.violations));
}

} // namespace

TEST_CASE("every level's streamed worlds stream under a camera path within the budget", "[disc][world_streaming]") {
    const char* discPath = SDL_getenv("CONEY_DISC");
    if (discPath == nullptr || *discPath == '\0') {
        SKIP("CONEY_DISC is not set: no disc to check");
    }
    auto disc = coney::io::Disc::open(discPath);
    REQUIRE(disc.has_value());
    auto wad = coney::io::Wad::open(std::move(*disc));
    REQUIRE(wad.has_value());
    auto engine = coney::platform::RenderEngine::start(coney::platform::RenderBackend::Null, {});
    REQUIRE(engine.has_value());

    // Every level name that may exist, and objarena.
    std::vector<std::string> levels{"objarena"};
    for (int level = 0; level < 200; ++level) {
        levels.push_back(std::format("level{}", level));
    }

    StreamingTotals full;
    for (const std::string& level : levels) {
        streamLevel(*wad, level, 0, full);
    }
    printTotals("default budget", full);

    StreamingTotals tight;
    for (const std::string& level : {std::string("level51"), std::string("level83"), std::string("level54")}) {
        streamLevel(*wad, level, std::uint64_t{2} * 1024 * 1024, tight);
    }
    printTotals("worlds + 2 MB", tight);

    CHECK(full.levels == 80);
    CHECK(full.loadFailures == 0);
    CHECK(full.failedParts == 0);
    CHECK(full.violations == 0);
    CHECK(full.partsEverLoaded == full.parts);
    CHECK(tight.loadFailures == 0);
    CHECK(tight.violations == 0);
    CHECK(tight.unloads > 0);
    CHECK(tight.partsEverLoaded == tight.parts);
}

TEST_CASE("the playable levels are the level names with a streamed world", "[disc][world_streaming]") {
    const char* discPath = SDL_getenv("CONEY_DISC");
    if (discPath == nullptr || *discPath == '\0') {
        SKIP("CONEY_DISC is not set: no disc to check");
    }
    auto disc = coney::io::Disc::open(discPath);
    REQUIRE(disc.has_value());
    auto wad = coney::io::Wad::open(std::move(*disc));
    REQUIRE(wad.has_value());
    const std::vector<std::string> names = coney::platform::playableLevelNames(*wad);
    std::printf("playable levels: %zu\n", names.size());
    CHECK(std::ranges::find(names, "level2") != names.end());
    CHECK(std::ranges::find(names, "level99") != names.end());
    CHECK(std::ranges::find(names, "level7") == names.end());
    for (const std::string& name : names) {
        CHECK(coney::platform::worldNamesFor(*wad, name).has_value());
    }
}
