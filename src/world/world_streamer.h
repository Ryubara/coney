// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string>

#include "core/error.h"
#include "world/sector_budget.h"
#include "world/streamed_world.h"
#include "world/world_streams.h"

// The world manager's streaming: the one decision a frame about which part to load or unload, the preload before a
// level's first frame, and the draw distance that follows what is missing. Platform-neutral; the platform layer does
// the reading and freeing through a PartStore. Research: docs/research/world.md#streaming,
// docs/research/level-loading.md#preload.

namespace coney::world {

/// Reads and frees parts for the streamer: the platform layer's side of World_PartLoaded and World_UnloadPart.
class PartStore {
  public:
    virtual ~PartStore() = default;
    PartStore() = default;
    PartStore(const PartStore&) = delete;
    PartStore& operator=(const PartStore&) = delete;
    PartStore(PartStore&&) = delete;
    PartStore& operator=(PartStore&&) = delete;

    /// Reads part `part` of world `world` (its index in the span given to the streamer) and puts its atomics in place.
    /// Coney reads synchronously: when this returns, the part is loaded or has failed.
    [[nodiscard]] virtual std::expected<void, Error> loadPart(std::size_t world, std::uint32_t part) = 0;

    /// Destroys part `part` of world `world`: its atomics, then its texture dictionary.
    virtual void unloadPart(std::size_t world, std::uint32_t part) = 0;
};

/// What one streaming decision did.
enum class StreamResult : std::uint8_t {
    Idle,     ///< Nothing was missing (the original returns 1 or 3).
    Loaded,   ///< A part was read (the original's 2).
    Unloaded, ///< A part was freed to make room (2).
    NoRoom,   ///< Something is missing but there was no room and nothing could go (1 or 3).
    Failed,   ///< The part could not be read; it is marked failed and not asked for again (a Coney choice).
};

/// One decision and what it concerned.
struct StreamStep {
    StreamResult result = StreamResult::Idle;
    std::size_t world = 0;  ///< Index into the worlds given, for every result but Idle.
    std::uint32_t part = 0; ///< The part loaded, freed, refused or failed.
    float distance = 0.0F;  ///< The nearest missing sector's distance (not squared); +infinity when none.
    std::string error;      ///< Failed: why the part could not be read.
};

/// How much farther than the nearest missing sector a part must be before it is freed for it: the original's 5.0
/// units of hysteresis.
inline constexpr float kUnloadMargin = 5.0F;

/// Asks for room for part `part` of `world` (its manifest heap size) and, when it fits, reads it through `store` and
/// marks it loaded at `nowMs`. Returns false when there is no room. A failed read gives the room back, marks the part
/// failed and returns the error.
///
/// The original's request reserves a clump and queues an asynchronous read; Coney reads at once. Afterwards the
/// original replaces the part's recorded heap size with what the part really used; Coney cannot measure that and keeps
/// the manifest's size (a Coney choice).
/// @orig 0x00412310 World_RequestPart (WorldPS2.cpp)
[[nodiscard]] std::expected<bool, Error> requestPart(StreamedWorld& world, std::size_t worldIndex, std::uint32_t part,
                                                     SectorBudget& budget, PartStore& store, std::uint64_t nowMs);

/// One streaming decision for `worlds` (the `s` world first), seen from `cameras` (RenderWare axes):
///
/// 1. each world finds its nearest missing sector (World_FindPartToLoad); the nearest of all is wanted;
/// 2. its part is requested; if it fits, it is read (Loaded);
/// 3. otherwise each world offers its farthest loaded part that nobody saw last frame (World_FindPartToUnload); the
///    farthest of those is freed, but only if it is more than kUnloadMargin farther than the wanted sector (Unloaded);
/// 4. otherwise NoRoom; with nothing missing at all, Idle.
///
/// The original also weighs the resource manager's and the water effect's wants, and does nothing while a read is in
/// flight; Coney has neither yet and reads synchronously. Freeing only when a wanted part does not fit is how
/// Coney reads "when nothing could be started" (inferred). Whether the 5.0 margin compares plain or squared distances
/// is not on the page; Coney compares plain ones (a Coney choice, TODO for the analysts on world.md).
/// @orig 0x0040f8a0 WorldManager_Update (WorldManagerPS2.cpp)
[[nodiscard]] StreamStep updateStreaming(std::span<StreamedWorld* const> worlds, std::span<const Vec3> cameras,
                                         float drawDistance, SectorBudget& budget, PartStore& store,
                                         std::uint64_t nowMs);

/// The distance to the nearest missing sector of any of `worlds`, from their last searches: what the draw distance
/// follows and the preload compares with its radius. +infinity when nothing is missing.
[[nodiscard]] float nearestPendingDistance(std::span<StreamedWorld* const> worlds, std::span<const Vec3> cameras);

/// What a preload did.
struct PreloadResult {
    std::uint32_t loaded = 0;   ///< Parts read.
    std::uint32_t unloaded = 0; ///< Parts freed.
    std::uint32_t failed = 0;   ///< Parts that could not be read.
};

/// The streaming loop before a level's first frame: runs updateStreaming() while it does work and the nearest missing
/// sector is within `radius` (or nothing is missing), counting the passes that did not load, up to 200. The original
/// also loads the section's pack and stops at a real-time budget (15 or 30 s); Coney has no packs yet and reads
/// synchronously, so the passes alone bound it.
/// @orig 0x0040e2d8 WorldManager_Preload (WorldManagerPS2.cpp)
PreloadResult preloadWorlds(std::span<StreamedWorld* const> worlds, std::span<const Vec3> cameras, float radius,
                            SectorBudget& budget, PartStore& store, std::uint64_t nowMs);

/// The inputs of the per-frame draw-distance adjustment (docs/research/world.md#a-frame, step 3).
struct DrawDistanceInputs {
    float pending = 0.0F;     ///< nearestPendingDistance(); +infinity when nothing is missing.
    float farClip = 0.0F;     ///< The camera's own far clip, the ceiling.
    float seconds = 0.0F;     ///< The frame's step; capped at 0.1.
    float frameRate = 30.0F;  ///< Frames a second (Coney's fixed step: always 30).
    int viewports = 1;        ///< Split-screen viewports.
    bool lowRateMode = false; ///< Device flag 0x02 (PAL, speculative): threshold 24.5 instead of 29.5.
};

/// Moves the draw distance one frame: with a good frame rate toward the pending distance (at most 300), shrinking by
/// up to 40.5 a second and growing by up to 10.5; with a poor one shrinking by (threshold − rate) × 10.5 a second;
/// never below 60 − 10 × viewports nor above the far clip. Part of WorldManager_Render (0x0040e8d8). Coney's step
/// is game time, the original's real time capped at 100 ms.
[[nodiscard]] float adjustDrawDistance(float current, const DrawDistanceInputs& inputs);

} // namespace coney::world
