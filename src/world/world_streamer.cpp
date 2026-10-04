// SPDX-License-Identifier: GPL-3.0-or-later
#include "world/world_streamer.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <utility>

namespace coney::world {

namespace {

constexpr float kInfinity = std::numeric_limits<float>::infinity();

// The wanted sector of one decision: which world, which sector and how far.
struct Wanted {
    std::size_t world = 0;
    std::uint32_t sector = 0;
    float distanceSq = 0.0F;
};

} // namespace

std::expected<bool, Error> requestPart(StreamedWorld& world, std::size_t worldIndex, std::uint32_t part,
                                       SectorBudget& budget, PartStore& store, std::uint64_t nowMs) {
    const std::uint32_t heap = world.part(part).sizes.heapSize;
    if (!budget.reserve(heap)) {
        return false;
    }
    if (auto loaded = store.loadPart(worldIndex, part); !loaded) {
        budget.release(heap);
        world.markPartFailed(part);
        return std::unexpected(std::move(loaded.error()));
    }
    world.markPartLoaded(part, nowMs);
    return true;
}

StreamStep updateStreaming(std::span<StreamedWorld* const> worlds, std::span<const Vec3> cameras, float drawDistance,
                           SectorBudget& budget, PartStore& store, std::uint64_t nowMs) {
    // 1. What is wanted: the nearest missing sector over every world; the earlier world wins a tie.
    std::optional<Wanted> wanted;
    for (std::size_t w = 0; w < worlds.size(); ++w) {
        const std::optional<std::uint32_t> sector = worlds[w]->findSectorToLoad(cameras, drawDistance);
        if (!sector) {
            continue;
        }
        const float d2 = cameraDistanceSq(worlds[w]->sectors()[*sector].box, cameras);
        if (!wanted || d2 < wanted->distanceSq) {
            wanted = Wanted{.world = w, .sector = *sector, .distanceSq = d2};
        }
    }
    if (!wanted) {
        return StreamStep{.result = StreamResult::Idle, .world = 0, .part = 0, .distance = kInfinity, .error = {}};
    }
    StreamedWorld& world = *worlds[wanted->world];
    const std::uint32_t part = world.sectors()[wanted->sector].part;
    const float distance = std::sqrt(wanted->distanceSq);

    // 2. Load it when its heap fits.
    auto requested = requestPart(world, wanted->world, part, budget, store, nowMs);
    if (!requested) {
        return StreamStep{.result = StreamResult::Failed,
                          .world = wanted->world,
                          .part = part,
                          .distance = distance,
                          .error = std::move(requested.error().message)};
    }
    if (*requested) {
        return StreamStep{
            .result = StreamResult::Loaded, .world = wanted->world, .part = part, .distance = distance, .error = {}};
    }

    // 3. No room: free the farthest part nobody saw, if it is clearly farther than what is wanted.
    std::optional<std::pair<std::size_t, UnloadCandidate>> farthest;
    for (std::size_t w = 0; w < worlds.size(); ++w) {
        const std::optional<UnloadCandidate> candidate = worlds[w]->findPartToUnload(cameras);
        if (candidate && (!farthest || candidate->distanceSq > farthest->second.distanceSq)) {
            farthest = std::pair{w, *candidate};
        }
    }
    if (farthest && std::sqrt(farthest->second.distanceSq) > distance + kUnloadMargin) {
        StreamedWorld& owner = *worlds[farthest->first];
        const std::uint32_t victim = farthest->second.part;
        store.unloadPart(farthest->first, victim);
        owner.markPartUnloaded(victim);
        budget.release(owner.part(victim).sizes.heapSize);
        return StreamStep{.result = StreamResult::Unloaded,
                          .world = farthest->first,
                          .part = victim,
                          .distance = distance,
                          .error = {}};
    }
    return StreamStep{
        .result = StreamResult::NoRoom, .world = wanted->world, .part = part, .distance = distance, .error = {}};
}

float nearestPendingDistance(std::span<StreamedWorld* const> worlds, std::span<const Vec3> cameras) {
    float nearest = kInfinity;
    for (const StreamedWorld* world : worlds) {
        nearest = std::min(nearest, world->pendingDistance(cameras));
    }
    return nearest;
}

PreloadResult preloadWorlds(std::span<StreamedWorld* const> worlds, std::span<const Vec3> cameras, float radius,
                            SectorBudget& budget, PartStore& store, std::uint64_t nowMs) {
    constexpr std::uint32_t kOtherPasses = 200;
    PreloadResult result;
    std::uint32_t otherPasses = 0;
    while (otherPasses < kOtherPasses) {
        const StreamStep step = updateStreaming(worlds, cameras, radius, budget, store, nowMs);
        switch (step.result) {
        case StreamResult::Loaded:
            ++result.loaded;
            break;
        case StreamResult::Unloaded:
            ++result.unloaded;
            ++otherPasses;
            break;
        case StreamResult::Failed:
            ++result.failed;
            ++otherPasses;
            break;
        case StreamResult::Idle:
        case StreamResult::NoRoom:
            return result; // no more work
        }
        // Keep going while what is still missing lies within the radius: a fresh search, since the last one found what
        // was just loaded (0x0040e100, read here as searching again; inferred).
        for (StreamedWorld* world : worlds) {
            (void)world->findSectorToLoad(cameras, radius);
        }
        const float pending = nearestPendingDistance(worlds, cameras);
        if (std::isfinite(pending) && pending > radius) {
            return result;
        }
    }
    return result;
}

float adjustDrawDistance(float current, const DrawDistanceInputs& inputs) {
    constexpr float kMaxStep = 0.1F;
    constexpr float kCeiling = 300.0F;
    constexpr float kShrinkRate = 40.5F;
    constexpr float kGrowRate = 10.5F;
    const float dt = std::min(inputs.seconds, kMaxStep);
    const float threshold = inputs.lowRateMode ? 24.5F : 29.5F;
    if (inputs.frameRate > threshold) {
        // Follow the nearest missing scenery, so that it stays beyond the far clip.
        const float target = std::min(inputs.pending, kCeiling);
        current = target < current ? std::max(target, current - kShrinkRate * dt)
                                   : std::min(target, current + kGrowRate * dt);
    } else {
        current -= (threshold - inputs.frameRate) * kGrowRate * dt;
    }
    current = std::max(current, 60.0F - 10.0F * static_cast<float>(inputs.viewports));
    return std::min(current, inputs.farClip);
}

} // namespace coney::world
