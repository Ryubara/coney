// SPDX-License-Identifier: GPL-3.0-or-later
#include "world/streamed_world.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <limits>
#include <utility>

#include "core/assert.h"

namespace coney::world {

namespace {

constexpr float kInfinity = std::numeric_limits<float>::infinity();

// The smaller of the squared distances from `q` to the planes `lo` and `hi`: one axis of the camera metric.
float axisTerm(float lo, float hi, float q) { return std::min((lo - q) * (lo - q), (hi - q) * (hi - q)); }

} // namespace

float cameraDistanceSq(const Box& box, std::span<const Vec3> cameras) {
    float best = kInfinity;
    for (const Vec3& q : cameras) {
        const float d2 = axisTerm(box.min.x, box.max.x, q.x) + axisTerm(box.min.y, box.max.y, q.y) +
                         axisTerm(box.min.z, box.max.z, q.z);
        best = std::min(best, d2);
    }
    return best;
}

Vec3 gameToRenderWareAxes(Vec3 game) { return Vec3{game.x, game.z, -game.y}; }

std::uint8_t fadeInAlpha(std::uint64_t fadeEndMs, std::uint64_t nowMs) {
    if (nowMs >= fadeEndMs) {
        return 255;
    }
    const std::uint64_t left = std::min(fadeEndMs - nowMs, kFadeInMs);
    return static_cast<std::uint8_t>(255 * (kFadeInMs - left) / kFadeInMs);
}

std::expected<StreamedWorld, Error> StreamedWorld::create(std::string name, const WorldStream& layout,
                                                          const WorldManifest& manifest) {
    if (manifest.parts.size() != layout.partCount) {
        return fail(ErrorCode::Invalid, std::format("{}: the manifest lists {} parts, the world stream {}", name,
                                                    manifest.parts.size(), layout.partCount));
    }
    StreamedWorld world;
    world.m_name = std::move(name);
    world.m_parts.resize(manifest.parts.size());
    for (std::size_t i = 0; i < manifest.parts.size(); ++i) {
        world.m_parts[i].sizes = manifest.parts[i];
    }

    // Index every sector that has an atomic by its streamed index; the table grows to the highest index + 1
    // (world +0x2168), then every slot must have been filled exactly once.
    std::vector<bool> filled;
    for (const WorldSector& sector : layout.sectors) {
        // A sector without plugin data reads as one without an atomic (index -1).
        const SectorPluginData plugin = sector.plugin.value_or(SectorPluginData{});
        if (plugin.streamedIndex < 0) {
            continue;
        }
        const auto index = static_cast<std::size_t>(plugin.streamedIndex);
        if (plugin.part == 0 || plugin.part > layout.partCount) {
            return fail(ErrorCode::Invalid, std::format("{}: sector {} names part {} of {}", world.m_name, index,
                                                        plugin.part, layout.partCount));
        }
        // Indices beyond the sector count cannot all be filled; refuse them before growing the table.
        if (index >= layout.sectors.size()) {
            return fail(ErrorCode::Invalid, std::format("{}: streamed index {} is beyond the {} sectors", world.m_name,
                                                        index, layout.sectors.size()));
        }
        if (index >= world.m_sectors.size()) {
            world.m_sectors.resize(index + 1);
            filled.resize(index + 1, false);
        }
        if (filled[index]) {
            return fail(ErrorCode::Invalid, std::format("{}: streamed index {} is used twice", world.m_name, index));
        }
        filled[index] = true;
        world.m_sectors[index] = StreamedSector{.box = sector.box,
                                                .origin = plugin.origin,
                                                .part = plugin.part,
                                                .loaded = false,
                                                .visible = false,
                                                .fadeEndMs = 0};
        world.m_parts[plugin.part - 1].sectors.push_back(static_cast<std::uint32_t>(index));
    }
    if (std::ranges::find(filled, false) != filled.end()) {
        return fail(ErrorCode::Invalid, std::format("{}: the streamed indices have a gap", world.m_name));
    }
    return world;
}

const WorldPart& StreamedWorld::part(std::uint32_t number) const {
    CONEY_ASSERT(number >= 1 && number <= m_parts.size());
    return m_parts[number - 1];
}

template <typename Predicate>
std::optional<std::uint32_t> StreamedWorld::nearestMissing(std::span<const Vec3> cameras, Predicate include,
                                                           float& distanceSq) const {
    std::optional<std::uint32_t> best;
    distanceSq = kInfinity;
    for (std::uint32_t k = 0; k < m_sectors.size(); ++k) {
        const StreamedSector& sector = m_sectors[k];
        if (sector.loaded || m_parts[sector.part - 1].state == PartState::Failed || !include(sector)) {
            continue;
        }
        // Strictly nearer, so the lowest index wins a tie and the search is deterministic.
        const float d2 = cameraDistanceSq(sector.box, cameras);
        if (!best || d2 < distanceSq) {
            best = k;
            distanceSq = d2;
        }
    }
    return best;
}

std::optional<std::uint32_t> StreamedWorld::findSectorToLoad(std::span<const Vec3> cameras, float drawDistance) {
    float d2 = kInfinity;
    m_fellBack = false;
    m_lastFound = nearestMissing(cameras, [](const StreamedSector& s) { return s.visible; }, d2);
    // The original's comparison: a squared distance against a plain one (see the header).
    if (!m_lastFound || d2 > drawDistance) {
        m_fellBack = true;
        m_lastFound = nearestMissing(cameras, [](const StreamedSector&) { return true; }, d2);
    }
    return m_lastFound;
}

float StreamedWorld::pendingDistance(std::span<const Vec3> cameras) const {
    const std::optional<std::uint32_t> found = m_lastFound;
    if (!found) {
        return kInfinity;
    }
    return std::sqrt(cameraDistanceSq(m_sectors[*found].box, cameras));
}

std::optional<UnloadCandidate> StreamedWorld::findPartToUnload(std::span<const Vec3> cameras) const {
    std::optional<UnloadCandidate> best;
    // Parts 1 to n - 1: the original's loop stops one short, so part n stays (see the header).
    for (std::uint32_t number = 1; number < partCount(); ++number) {
        const WorldPart& candidate = m_parts[number - 1];
        if (candidate.state != PartState::Loaded) {
            continue;
        }
        float nearest = kInfinity;
        bool seen = false;
        for (const std::uint32_t k : candidate.sectors) {
            seen = seen || m_sectors[k].visible;
            nearest = std::min(nearest, cameraDistanceSq(m_sectors[k].box, cameras));
        }
        if (seen) {
            continue;
        }
        if (!best || nearest > best->distanceSq) {
            best = UnloadCandidate{.part = number, .distanceSq = nearest};
        }
    }
    return best;
}

void StreamedWorld::resetVisibility() {
    for (StreamedSector& sector : m_sectors) {
        sector.visible = false;
    }
}

void StreamedWorld::markVisible(std::uint32_t sector) {
    CONEY_ASSERT(sector < m_sectors.size());
    m_sectors[sector].visible = true;
}

void StreamedWorld::findVisibleSectors(const ViewFrustum& frustum, bool firstViewport) {
    if (firstViewport) {
        resetVisibility();
    }
    for (StreamedSector& sector : m_sectors) {
        if (frustum.mayContain(sector.box)) {
            sector.visible = true;
        }
    }
}

std::vector<std::uint32_t> StreamedWorld::collectSectors(std::span<const Vec3> cameras) const {
    std::vector<std::pair<float, std::uint32_t>> ordered;
    for (std::uint32_t k = 0; k < m_sectors.size(); ++k) {
        if (m_sectors[k].visible && m_sectors[k].loaded) {
            ordered.emplace_back(cameraDistanceSq(m_sectors[k].box, cameras), k);
        }
    }
    std::ranges::sort(ordered);
    std::vector<std::uint32_t> result;
    result.reserve(ordered.size());
    for (const auto& entry : ordered) {
        result.push_back(entry.second);
    }
    return result;
}

void StreamedWorld::markPartLoaded(std::uint32_t number, std::uint64_t nowMs) {
    CONEY_ASSERT(number >= 1 && number <= m_parts.size());
    WorldPart& record = m_parts[number - 1];
    record.state = PartState::Loaded;
    for (const std::uint32_t k : record.sectors) {
        m_sectors[k].loaded = true;
        m_sectors[k].fadeEndMs = nowMs + kFadeInMs;
    }
}

void StreamedWorld::markPartUnloaded(std::uint32_t number) {
    CONEY_ASSERT(number >= 1 && number <= m_parts.size());
    WorldPart& record = m_parts[number - 1];
    record.state = PartState::Unloaded;
    for (const std::uint32_t k : record.sectors) {
        m_sectors[k].loaded = false;
    }
}

void StreamedWorld::markPartFailed(std::uint32_t number) {
    markPartUnloaded(number);
    m_parts[number - 1].state = PartState::Failed;
}

} // namespace coney::world
