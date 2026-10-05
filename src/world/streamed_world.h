// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "core/error.h"
#include "world/view_frustum.h"
#include "world/world_manifest.h"
#include "world/world_streams.h"

// The bookkeeping of one streamed world, without RenderWare: which sectors have their atomic, which parts are loaded,
// which sectors were seen, and the searches the streaming decision is made from. The platform layer owns the atomics
// and texture dictionaries themselves (src/platform/loaded_world.h). Research: docs/research/world.md.

namespace coney::world {

/// What became of a part.
enum class PartState : std::uint8_t {
    Unloaded, ///< On the disc only.
    Loaded,   ///< Its atomics are in their sectors.
    Failed,   ///< Reading it failed; it is not asked for again (a Coney choice: the original has no failure path).
};

/// A sector with a streamed atomic, by its streamed-sector index. Research: docs/research/world.md#sector-plugin
struct StreamedSector {
    Box box;                     ///< The sector's bounding box.
    Vec3 origin;                 ///< Where its atomic's frame goes (sector plugin +0x14).
    std::uint32_t part = 0;      ///< The part file holding its atomic.
    bool loaded = false;         ///< Its atomic is in place (sector plugin state 2).
    bool visible = false;        ///< Marked by the last visibility pass (the world's visible bits, +0x08f8).
    std::uint64_t fadeEndMs = 0; ///< Game time in milliseconds when its fade-in ends (sector plugin +0x10).
};

/// One part file's record (the world's per-part tables at +0x0940 and +0x1200).
struct WorldPart {
    PartSizes sizes;                    ///< From the manifest.
    std::vector<std::uint32_t> sectors; ///< Streamed-sector indices whose atomics it holds.
    PartState state = PartState::Unloaded;
};

/// A part that may be unloaded and how far its nearest sector is (squared, RenderWare units).
struct UnloadCandidate {
    std::uint32_t part = 0;
    float distanceSq = 0.0F;
};

/// The squared distance from the nearest of `cameras` (positions in RenderWare axes) to `box`, measured to the box's
/// nearest face *planes*: per axis the smaller of the squared distances to the two planes, summed. A camera inside the
/// box still gets a positive value; this is the original's metric, kept as it is. +infinity without cameras.
/// @orig 0x004101f0 World_CameraDistanceSq (WorldPS2.cpp)
[[nodiscard]] float cameraDistanceSq(const Box& box, std::span<const Vec3> cameras);

/// A game-axes position (the player camera's) in RenderWare's axes: (x, z, -y), as World_CameraDistanceSq converts it.
[[nodiscard]] Vec3 gameToRenderWareAxes(Vec3 game);

/// The alpha a newly loaded atomic is drawn with: 255 × (1 - (fadeEnd - now) / 1000) during the second before
/// `fadeEndMs`, 255 from then on. Coney counts game time; the original real time (a Coney choice for the test mode).
/// Research: docs/research/world.md#a-frame (World_RenderSectorAtomic)
[[nodiscard]] std::uint8_t fadeInAlpha(std::uint64_t fadeEndMs, std::uint64_t nowMs);

/// How long a new atomic fades in, in milliseconds.
inline constexpr std::uint64_t kFadeInMs = 1000;

/// The pending distance when no missing sector was found: FLT_MAX, as World_PendingDistance returns, not infinity
/// (docs/research/world.md#camera-distance).
inline constexpr float kNoPendingDistance = std::numeric_limits<float>::max();

/// One streamed world's state. Its tables are sized from the data, not the original's fixed 560 sectors and 139 parts
/// (`level70s` has 593 streamed sectors).
///
/// Research: docs/research/world.md
class StreamedWorld {
  public:
    /// Indexes the sectors of `layout` by their streamed-sector index and the parts by number, with the sizes of
    /// `manifest`. Fails with ErrorCode::Invalid when the two disagree on the part count, a sector names a part outside
    /// 1 to the part count, or the streamed indices are not 0 to k - 1 each once.
    /// @orig 0x00411010 World_RegisterSector (WorldPS2.cpp)
    [[nodiscard]] static std::expected<StreamedWorld, Error> create(std::string name, const WorldStream& layout,
                                                                    const WorldManifest& manifest);

    /// The world's name (`level2s`).
    [[nodiscard]] const std::string& name() const { return m_name; }
    /// The sectors with an atomic, by streamed-sector index.
    [[nodiscard]] const std::vector<StreamedSector>& sectors() const { return m_sectors; }
    /// The number of parts, n.
    [[nodiscard]] std::uint32_t partCount() const { return static_cast<std::uint32_t>(m_parts.size()); }
    /// Part `number`, 1 to partCount() (CONEY_ASSERT otherwise).
    [[nodiscard]] const WorldPart& part(std::uint32_t number) const;

    /// Finds the nearest sector whose atomic is missing: first among the sectors marked visible by the last pass, and
    /// when none of those is within reach, among all of them (searchFellBack() is then true). Sectors of failed parts
    /// are skipped. The result is remembered for pendingDistance().
    ///
    /// Original quirk, kept: "within reach" compares the *squared* distance with `drawDistance`, which is not squared,
    /// so a visible sector counts as within reach only up to the square root of the draw distance.
    /// @orig 0x00411eb0 World_FindPartToLoad (WorldPS2.cpp)
    [[nodiscard]] std::optional<std::uint32_t> findSectorToLoad(std::span<const Vec3> cameras, float drawDistance);

    /// Whether the last findSectorToLoad() had to look beyond the visible sectors (world +0x10).
    [[nodiscard]] bool searchFellBack() const { return m_fellBack; }

    /// The distance (not squared) from the cameras to the sector the last findSectorToLoad() found, even if it has been
    /// loaded since, as the original reads it; kNoPendingDistance (FLT_MAX) when it found none.
    /// @orig 0x00411880 World_PendingDistance (WorldPS2.cpp)
    [[nodiscard]] float pendingDistance(std::span<const Vec3> cameras) const;

    /// The loaded part whose nearest sector is farthest from the cameras, among parts none of whose sectors was
    /// visible in the last pass. Nothing when there is none.
    ///
    /// Original quirk, kept: only parts 1 to n - 1 are looked at, so the last part of a world is never unloaded.
    /// @orig 0x004120a8 World_FindPartToUnload (WorldPS2.cpp)
    [[nodiscard]] std::optional<UnloadCandidate> findPartToUnload(std::span<const Vec3> cameras) const;

    /// Clears every sector's visible mark, before the first viewport's visibility pass.
    /// @orig 0x004123e8 World_ResetVisibility (WorldPS2.cpp)
    void resetVisibility();

    /// The visibility pass for one viewport: marks visible every sector whose box `frustum` may contain. With
    /// `firstViewport` the marks of the last frame are cleared first. PVS and occluder culling are not done yet (they
    /// only save work; docs/research/world.md#visibility).
    /// @orig 0x00411d10 World_FindVisibleSectors (WorldPS2.cpp)
    void findVisibleSectors(const ViewFrustum& frustum, bool firstViewport);

    /// Marks sector `sector` visible, as the visibility pass's sector callback does for a sector it keeps.
    void markVisible(std::uint32_t sector);

    /// The sectors to draw for this viewport, in drawing order: those marked visible whose atomic is loaded, collected
    /// in a back-to-front walk of the world's BSP from `viewpoint` (the viewport camera's position, RenderWare axes)
    /// and returned last collected first, as the original draws its list from the end: so front to back by the BSP. At
    /// each plane the side `viewpoint` is not on is walked first. A layout without planes and with several sectors
    /// (only synthetic ones) is collected in stream order (a Coney choice).
    /// @orig 0x00411b20 World_CollectSector (WorldPS2.cpp)
    [[nodiscard]] std::vector<std::uint32_t> collectSectors(Vec3 viewpoint) const;

    /// As collectSectors(), but choosing the loaded sectors whose box `frustum` may contain instead of those the last
    /// visibility pass marked. For a render that blends between two steps
    /// (docs/guides/conventions.md#update-and-render): its camera is not the one the visibility pass used, and the
    /// marks belong to the simulation (streaming reads them), so the render must not redo the pass. With the pass's own
    /// frustum it gives exactly collectSectors().
    [[nodiscard]] std::vector<std::uint32_t> collectSectorsIn(Vec3 viewpoint, const ViewFrustum& frustum) const;

    /// Records that part `number`'s atomics are in place, their fade-in ending kFadeInMs after `nowMs`.
    void markPartLoaded(std::uint32_t number, std::uint64_t nowMs);
    /// Records that part `number` was unloaded.
    void markPartUnloaded(std::uint32_t number);
    /// Records that part `number` could not be read, so it is not asked for again.
    void markPartFailed(std::uint32_t number);

  private:
    StreamedWorld() = default;

    /// The BSP walk both collectSectors() share: the loaded sectors `chosen` keeps, in drawing order.
    [[nodiscard]] std::vector<std::uint32_t>
    collectSectorsWhere(Vec3 viewpoint, const std::function<bool(const StreamedSector&)>& chosen) const;

    // The nearest sector with a missing atomic among those `include` accepts, and its squared distance.
    template <typename Predicate>
    [[nodiscard]] std::optional<std::uint32_t> nearestMissing(std::span<const Vec3> cameras, Predicate include,
                                                              float& distanceSq) const;

    std::string m_name;
    std::vector<StreamedSector> m_sectors;
    std::vector<BspPlane> m_planes;          // the BSP's inner nodes, for the drawing order
    BspChild m_root;                         // the BSP's root
    std::vector<std::int32_t> m_leafSectors; // by the layout's sector index: its streamed index, or -1
    std::vector<WorldPart> m_parts;          // part i at [i - 1]
    std::optional<std::uint32_t> m_lastFound;
    bool m_fellBack = false;
};

} // namespace coney::world
