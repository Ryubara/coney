// SPDX-License-Identifier: GPL-3.0-or-later
#include "world/path_map.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <format>
#include <iterator>
#include <limits>
#include <utility>

#include "fileio/reader.h"
#include "world/level_object.h"

namespace coney::world {

namespace {

// The records' sizes and the fields read from them (docs/research/level-loading.md#path-data).
constexpr std::size_t kHeaderBytes = 0x20;
constexpr std::size_t kABytes = 16;
constexpr std::size_t kVertexBytes = 16;
constexpr std::size_t kPolygonBytes = 0x50;
constexpr std::size_t kNodeBytes = 32;
constexpr std::size_t kEdgeBytes = 8;
constexpr std::size_t kNextInAreaAt = 0x20;
constexpr std::size_t kSlabStartsAt = 0x28;
constexpr std::size_t kFlagsAt = 0x48;
constexpr std::size_t kGroundAt = 0x4a;
constexpr std::size_t kARecordAt = 0x4c;
// Two areas join where the segment leaves one within this distance squared of where it enters the next (0.02 m).
constexpr float kAreaJoinSquared = 0.0004F;
// An edge flatter than this in y never crosses a row (the inside test).
constexpr float kFlatEdge = 0.0001F;
// The avoid bit of an edge's word, and where its door number sits (the low 13 bits of its `+0x06` half).
constexpr std::uint32_t kAvoidBit = 0x80000000U;
constexpr std::uint32_t kDoorShift = 16;
constexpr std::uint32_t kDoorMask = 0x1fffU;

// Little-endian reads of a buffer whose size the caller has checked.
std::int16_t loadS16(std::span<const std::byte> bytes, std::size_t at) {
    return static_cast<std::int16_t>(std::to_integer<std::uint16_t>(bytes[at]) |
                                     (std::to_integer<std::uint16_t>(bytes[at + 1]) << 8U));
}
std::uint32_t loadU32(std::span<const std::byte> bytes, std::size_t at) { return io::loadU32Le(bytes.subspan(at, 4)); }
float loadF32(std::span<const std::byte> bytes, std::size_t at) { return std::bit_cast<float>(loadU32(bytes, at)); }

// The squared distance in plan from (x, y) to the segment a-b.
float distanceToSegmentSquared(float x, float y, anim::Vec3 a, anim::Vec3 b) {
    const float dx = b.x - a.x;
    const float dy = b.y - a.y;
    const float lengthSquared = dx * dx + dy * dy;
    float t = lengthSquared > 0.0F ? ((x - a.x) * dx + (y - a.y) * dy) / lengthSquared : 0.0F;
    t = std::clamp(t, 0.0F, 1.0F);
    const float ex = a.x + t * dx - x;
    const float ey = a.y + t * dy - y;
    return ex * ex + ey * ey;
}

// Where the segment p0-p1 crosses the segment q0-q1, as a parameter along p0-p1 in [0, 1]; nothing when they do not
// cross (parallel segments never do).
std::optional<float> crossing(anim::Vec3 p0, anim::Vec3 p1, anim::Vec3 q0, anim::Vec3 q1) {
    const float rx = p1.x - p0.x;
    const float ry = p1.y - p0.y;
    const float sx = q1.x - q0.x;
    const float sy = q1.y - q0.y;
    const float denominator = rx * sy - ry * sx;
    if (std::fabs(denominator) < 1e-12F) {
        return std::nullopt;
    }
    const float qx = q0.x - p0.x;
    const float qy = q0.y - p0.y;
    const float t = (qx * sy - qy * sx) / denominator;
    const float u = (qx * ry - qy * rx) / denominator;
    if (t < 0.0F || t > 1.0F || u < 0.0F || u > 1.0F) {
        return std::nullopt;
    }
    return t;
}

} // namespace

std::expected<PathMap, Error> PathMap::decode(std::span<const std::byte> chunk) {
    auto header = inspectPathData(chunk);
    if (!header) {
        return std::unexpected(std::move(header.error()));
    }
    PathMap map;
    // The parts follow each other: A records, vertices, polygons, C records, D records, then the edge lists.
    const std::size_t aAt = kHeaderBytes;
    const std::size_t verticesAt = aAt + std::size_t{header->aCount} * kABytes;
    const std::size_t polygonsAt = verticesAt + std::size_t{header->bCount} * kVertexBytes;
    const std::size_t nodesAt = polygonsAt + std::size_t{header->paths} * kPolygonBytes;
    const std::size_t edgesAt = nodesAt + std::size_t{header->cCount} * kNodeBytes;

    map.m_vertices.reserve(header->bCount);
    for (std::uint32_t v = 0; v < header->bCount; ++v) {
        const std::size_t at = verticesAt + std::size_t{v} * kVertexBytes;
        map.m_vertices.push_back(anim::Vec3{loadF32(chunk, at), loadF32(chunk, at + 4), 0.0F});
    }
    // Each polygon takes the next vertices and, when flagged, the next A record.
    std::uint32_t nextVertex = 0;
    std::uint32_t nextA = 0;
    std::uint32_t nextNode = 0;
    map.m_polygons.reserve(header->paths);
    for (std::uint32_t p = 0; p < header->paths; ++p) {
        const std::size_t at = polygonsAt + std::size_t{p} * kPolygonBytes;
        PathPolygon polygon;
        polygon.vertexCount = static_cast<std::uint16_t>(loadS16(chunk, at));
        polygon.graph = loadS16(chunk, at + 2);
        polygon.firstVertex = nextVertex;
        nextVertex += polygon.vertexCount;
        polygon.xMin = loadF32(chunk, at + 0x08);
        polygon.xMax = loadF32(chunk, at + 0x0c);
        polygon.yMin = loadF32(chunk, at + 0x10);
        polygon.yMax = loadF32(chunk, at + 0x14);
        for (std::size_t slab = 0; slab < kPathSlabCount; ++slab) {
            polygon.slabStarts.at(slab) = loadS16(chunk, at + kSlabStartsAt + slab * 2);
        }
        polygon.nextInArea = loadU32(chunk, at + kNextInAreaAt) != 0;
        polygon.flags = static_cast<std::uint16_t>(loadS16(chunk, at + kFlagsAt));
        polygon.ground = static_cast<std::uint16_t>(loadS16(chunk, at + kGroundAt));
        polygon.hasNodes = loadU32(chunk, at + kARecordAt) != 0;
        if (polygon.hasNodes) {
            if (nextA >= header->aCount) {
                return fail(ErrorCode::Invalid,
                            std::format("path {} has an A record past the {} there are", p, header->aCount));
            }
            const std::size_t a = aAt + std::size_t{nextA} * kABytes;
            polygon.nodeCount = static_cast<std::uint16_t>(loadS16(chunk, a));
            polygon.firstNode = nextNode;
            nextNode += polygon.nodeCount;
            ++nextA;
        }
        map.m_polygons.push_back(polygon);
    }
    if (nextVertex != header->bCount || nextA != header->aCount) {
        return fail(ErrorCode::Invalid, std::format("the paths own {} vertices and {} A records, not {} and {}",
                                                    nextVertex, nextA, header->bCount, header->aCount));
    }
    // Each node takes the next of the edges.
    std::uint32_t nextEdge = 0;
    map.m_nodes.reserve(header->cCount);
    for (std::uint32_t c = 0; c < header->cCount; ++c) {
        const std::size_t at = nodesAt + std::size_t{c} * kNodeBytes;
        PathNode node;
        node.position = anim::Vec3{loadF32(chunk, at), loadF32(chunk, at + 4), loadF32(chunk, at + 8)};
        node.edgeCount = static_cast<std::uint16_t>(loadS16(chunk, at + 0x14));
        node.pair = std::to_integer<std::uint8_t>(chunk[at + 0x1e]);
        node.firstEdge = nextEdge;
        nextEdge += node.edgeCount;
        map.m_nodes.push_back(node);
    }
    if (nextEdge != header->dCount) {
        return fail(ErrorCode::Invalid,
                    std::format("the route nodes own {} edges, not the {} there are", nextEdge, header->dCount));
    }
    map.m_edges.reserve(header->dCount);
    for (std::uint32_t d = 0; d < header->dCount; ++d) {
        const std::size_t at = edgesAt + std::size_t{d} * kEdgeBytes;
        const std::uint32_t word = loadU32(chunk, at + 4);
        map.m_edges.push_back(PathEdge{.to = loadU32(chunk, at),
                                       .flags = static_cast<std::uint16_t>(word & 0xffffU),
                                       .avoid = (word & kAvoidBit) != 0,
                                       .door = static_cast<std::uint16_t>((word >> kDoorShift) & kDoorMask)});
    }
    const std::size_t listsAt = header->recordBytes;
    map.m_edgeLists.reserve(header->edgeListBytes / 2);
    for (std::size_t at = listsAt; at + 2 <= listsAt + header->edgeListBytes; at += 2) {
        map.m_edgeLists.push_back(loadS16(chunk, at));
    }
    return checked(std::move(map));
}

std::expected<PathMap, Error> PathMap::make(std::vector<anim::Vec3> vertices, std::vector<PathPolygon> polygons,
                                            std::vector<PathNode> nodes, std::vector<PathEdge> edges,
                                            std::vector<std::int16_t> edgeLists) {
    PathMap map;
    map.m_vertices = std::move(vertices);
    map.m_polygons = std::move(polygons);
    map.m_nodes = std::move(nodes);
    map.m_edges = std::move(edges);
    map.m_edgeLists = std::move(edgeLists);
    return checked(std::move(map));
}

std::expected<PathMap, Error> PathMap::checked(PathMap map) {
    const std::size_t vertices = map.m_vertices.size();
    const std::size_t nodes = map.m_nodes.size();
    for (std::size_t p = 0; p < map.m_polygons.size(); ++p) {
        const PathPolygon& polygon = map.m_polygons[p];
        if (std::size_t{polygon.firstVertex} + polygon.vertexCount > vertices) {
            return fail(ErrorCode::Invalid, std::format("path {}'s vertices run past the {} there are", p, vertices));
        }
        if (polygon.hasNodes && std::size_t{polygon.firstNode} + polygon.nodeCount > nodes) {
            return fail(ErrorCode::Invalid, std::format("path {}'s route nodes run past the {} there are", p, nodes));
        }
        for (const std::int16_t start : polygon.slabStarts) {
            if (polygon.slabStarts[0] >= 0 && start >= 0 && std::cmp_greater_equal(start, map.m_edgeLists.size())) {
                return fail(ErrorCode::Invalid, std::format("path {}'s slab list starts past the edge lists", p));
            }
        }
    }
    // Each polygon's area: an outline and the holes after it, chained by the +0x20 word.
    std::uint32_t first = 0;
    for (std::size_t p = 0; p < map.m_polygons.size(); ++p) {
        map.m_polygons[p].area = first;
        if (!map.m_polygons[p].nextInArea) {
            first = static_cast<std::uint32_t>(p + 1);
        }
    }
    for (std::size_t c = 0; c < nodes; ++c) {
        const PathNode& node = map.m_nodes[c];
        if (std::size_t{node.firstEdge} + node.edgeCount > map.m_edges.size()) {
            return fail(ErrorCode::Invalid, std::format("route node {}'s edges run past the edges there are", c));
        }
    }
    for (std::size_t d = 0; d < map.m_edges.size(); ++d) {
        if (map.m_edges[d].to >= nodes) {
            return fail(ErrorCode::Invalid,
                        std::format("route edge {} leads to node {} of {}", d, map.m_edges[d].to, nodes));
        }
    }
    return map;
}

std::span<const PathEdge> PathMap::edgesOf(std::uint32_t node) const {
    const PathNode& owner = m_nodes.at(node);
    return std::span(m_edges).subspan(owner.firstEdge, owner.edgeCount);
}

std::optional<std::uint32_t> PathMap::partnerOf(std::uint32_t node) const {
    const std::uint8_t pair = m_nodes.at(node).pair;
    if (pair == 0) {
        return std::nullopt;
    }
    for (const PathEdge& edge : edgesOf(node)) {
        if (edge.to != node && m_nodes.at(edge.to).pair == pair) {
            return edge.to;
        }
    }
    return std::nullopt;
}

bool PathMap::inside(const PathPolygon& polygon, float x, float y) const { return winding(polygon, x, y) > 0; }

int PathMap::winding(const PathPolygon& polygon, float x, float y) const {
    if (x < polygon.xMin || x > polygon.xMax || y < polygon.yMin || y > polygon.yMax || polygon.vertexCount < 3) {
        return 0;
    }
    const std::span<const anim::Vec3> outline = std::span(m_vertices).subspan(polygon.firstVertex, polygon.vertexCount);
    // One edge's share of the winding number: it crosses the point's row to its left.
    const auto windingOf = [&](std::size_t k) {
        const anim::Vec3 a = outline[k];
        const anim::Vec3 b = outline[(k + 1) % outline.size()];
        const float dy = b.y - a.y;
        if (std::fabs(dy) < kFlatEdge) {
            return 0;
        }
        const bool spans = dy > 0.0F ? (a.y <= y && y < b.y) : (b.y <= y && y < a.y);
        if (!spans || a.x + (y - a.y) * (b.x - a.x) / dy >= x) {
            return 0;
        }
        return dy < 0.0F ? 1 : -1;
    };
    int sum = 0;
    if (polygon.slabStarts[0] < 0) {
        for (std::size_t k = 0; k < outline.size(); ++k) {
            sum += windingOf(k);
        }
        return sum;
    }
    // The point's slab, and its list of edge numbers up to the negative value that ends it.
    const float height = polygon.yMax - polygon.yMin;
    const auto slab =
        height > 0.0F
            ? std::clamp(static_cast<int>(std::floor((y - polygon.yMin) * static_cast<float>(kPathSlabCount) / height)),
                         0, static_cast<int>(kPathSlabCount) - 1)
            : 0;
    const std::int16_t start = polygon.slabStarts.at(static_cast<std::size_t>(slab));
    if (start < 0) {
        return 0;
    }
    for (auto at = static_cast<std::size_t>(start); at < m_edgeLists.size() && m_edgeLists[at] >= 0; ++at) {
        const auto k = static_cast<std::size_t>(m_edgeLists[at]);
        if (k < outline.size()) {
            sum += windingOf(k);
        }
    }
    return sum;
}

bool PathMap::usable(const PathPolygon& polygon, std::uint32_t excludeFlags) {
    return (polygon.flags & excludeFlags) == 0;
}

std::optional<std::uint32_t> PathMap::polygonAt(float x, float y, std::uint32_t excludeFlags) const {
    for (std::size_t p = 0; p < m_polygons.size(); ++p) {
        if (usable(m_polygons[p], excludeFlags) && inside(m_polygons[p], x, y)) {
            return static_cast<std::uint32_t>(p);
        }
    }
    return std::nullopt;
}

std::optional<std::uint32_t> PathMap::nearestPolygon(float x, float y, float reach, std::uint32_t excludeFlags) const {
    std::optional<std::uint32_t> nearest;
    float best = reach * reach;
    for (std::size_t p = 0; p < m_polygons.size(); ++p) {
        const PathPolygon& polygon = m_polygons[p];
        if (!usable(polygon, excludeFlags) || polygon.vertexCount < 2 || x < polygon.xMin - reach ||
            x > polygon.xMax + reach || y < polygon.yMin - reach || y > polygon.yMax + reach) {
            continue;
        }
        for (std::uint32_t k = 0; k < polygon.vertexCount; ++k) {
            const anim::Vec3 a = m_vertices[polygon.firstVertex + k];
            const anim::Vec3 b = m_vertices[polygon.firstVertex + (k + 1) % polygon.vertexCount];
            if (const float d = distanceToSegmentSquared(x, y, a, b); d <= best) {
                best = d;
                nearest = static_cast<std::uint32_t>(p);
            }
        }
    }
    return nearest;
}

std::optional<std::uint32_t> PathMap::holeAt(float x, float y) const {
    // The holes whose box holds the point.
    std::vector<std::uint32_t> boxed;
    for (std::size_t p = 0; p < m_polygons.size(); ++p) {
        const PathPolygon& polygon = m_polygons[p];
        if ((polygon.flags & kPathPolygonHole) != 0 && polygon.vertexCount > 0 && x >= polygon.xMin &&
            x <= polygon.xMax && y >= polygon.yMin && y <= polygon.yMax) {
            boxed.push_back(static_cast<std::uint32_t>(p));
        }
    }
    if (boxed.size() <= 1) {
        return boxed.empty() ? std::nullopt : std::optional(boxed.front());
    }
    // Several: those that take the point in (all of them when none does), then the nearest vertex average.
    std::vector<std::uint32_t> holding;
    std::ranges::copy_if(boxed, std::back_inserter(holding),
                         [&](std::uint32_t p) { return inside(m_polygons[p], x, y); });
    const std::vector<std::uint32_t>& pool = holding.empty() ? boxed : holding;
    const auto averageDistance = [&](std::uint32_t p) {
        const PathPolygon& polygon = m_polygons[p];
        float sumX = 0.0F;
        float sumY = 0.0F;
        for (std::uint32_t k = 0; k < polygon.vertexCount; ++k) {
            sumX += m_vertices[polygon.firstVertex + k].x;
            sumY += m_vertices[polygon.firstVertex + k].y;
        }
        const float count = static_cast<float>(polygon.vertexCount);
        const float dx = sumX / count - x;
        const float dy = sumY / count - y;
        return dx * dx + dy * dy;
    };
    return *std::ranges::min_element(pool, {}, averageDistance);
}

int PathMap::areaWinding(std::uint32_t area, float x, float y) const {
    int sum = 0;
    for (std::size_t q = area; q < m_polygons.size() && m_polygons[q].area == area; ++q) {
        if (usable(m_polygons[q], kPathPolygonExcluded)) {
            sum += winding(m_polygons[q], x, y);
        }
    }
    return sum;
}

std::optional<std::uint32_t> PathMap::areaAt(float x, float y) const {
    for (std::size_t p = 0; p < m_polygons.size(); ++p) {
        // The winding summed over the area's polygons (a hole's runs the other way), flag 8's left out.
        if (m_polygons[p].area == p && areaWinding(static_cast<std::uint32_t>(p), x, y) > 0) {
            return static_cast<std::uint32_t>(p);
        }
    }
    return std::nullopt;
}

std::optional<std::uint32_t> PathMap::areaAt(anim::Vec3 point, const GroundProbe* probe) const {
    const std::optional<std::uint16_t> ground =
        probe != nullptr && *probe ? (*probe)(anim::Vec3{point.x, point.y, point.z + kGroundProbeLift}) : std::nullopt;
    if (!ground) {
        return areaAt(point.x, point.y);
    }
    for (std::size_t p = 0; p < m_polygons.size(); ++p) {
        const PathPolygon& first = m_polygons[p];
        if (first.area != p || (first.flags & kPathPolygonExcluded) != 0 || first.ground != *ground ||
            point.x < first.xMin - kAreaBoxMargin || point.x > first.xMax + kAreaBoxMargin ||
            point.y < first.yMin - kAreaBoxMargin || point.y > first.yMax + kAreaBoxMargin) {
            continue;
        }
        if (areaWinding(static_cast<std::uint32_t>(p), point.x, point.y) > 0) {
            return static_cast<std::uint32_t>(p);
        }
    }
    return std::nullopt;
}

std::optional<std::uint32_t> PathMap::candidateArea(anim::Vec3 point, const GroundProbe* probe) const {
    const std::optional<std::uint16_t> ground =
        probe != nullptr && *probe ? (*probe)(anim::Vec3{point.x, point.y, point.z + kGroundProbeLift}) : std::nullopt;
    for (std::size_t p = 0; p < m_polygons.size(); ++p) {
        const PathPolygon& first = m_polygons[p];
        if (first.area != p || (first.flags & kPathPolygonExcluded) != 0) {
            continue;
        }
        // On the ground the probe found: the area's byte and its widened box; else the outline in plan.
        if (ground ? first.ground == *ground && point.x >= first.xMin - kAreaBoxMargin &&
                         point.x <= first.xMax + kAreaBoxMargin && point.y >= first.yMin - kAreaBoxMargin &&
                         point.y <= first.yMax + kAreaBoxMargin
                   : winding(first, point.x, point.y) != 0) {
            return static_cast<std::uint32_t>(p);
        }
    }
    return std::nullopt;
}

std::optional<float> PathMap::areaCrossing(std::uint32_t area, anim::Vec3 from, anim::Vec3 to, std::uint32_t exclude,
                                           bool farthest, bool& any) const {
    std::optional<float> found;
    any = false;
    for (std::size_t q = area; q < m_polygons.size() && m_polygons[q].area == area; ++q) {
        const PathPolygon& polygon = m_polygons[q];
        if (!usable(polygon, exclude)) {
            continue;
        }
        any = true;
        for (std::uint32_t k = 0; k < polygon.vertexCount; ++k) {
            const anim::Vec3 a = m_vertices[polygon.firstVertex + k];
            const anim::Vec3 b = m_vertices[polygon.firstVertex + (k + 1) % polygon.vertexCount];
            const std::optional<float> t = crossing(from, to, a, b);
            // A crossing at the very end does not count.
            if (!t || *t >= 1.0F) {
                continue;
            }
            if (!found || (farthest ? *t > *found : *t < *found)) {
                found = t;
            }
        }
    }
    return found;
}

bool PathMap::walkable(anim::Vec3 from, anim::Vec3 to, std::uint32_t mask, const GroundProbe* probe) const {
    const std::uint32_t exclude = kPathPolygonExcluded | mask;
    const std::optional<std::uint32_t> startArea = areaAt(from, probe);
    if (!startArea) {
        return false;
    }
    // 1. The nearest crossing of the start area's edges (its outline and its holes); with none of its polygons left
    // by the mask, refused.
    bool any = false;
    const std::optional<float> leaves = areaCrossing(*startArea, from, to, exclude, false, any);
    if (!any) {
        return false;
    }
    const std::optional<std::uint32_t> endArea = areaAt(to, probe);
    // 2. No crossing: the end must be in the same area.
    if (!leaves) {
        return endArea == startArea;
    }
    // 3. A crossing: the end area's farthest crossing must be where the segment left the start's, so that it went
    // straight from one area into the next.
    if (!endArea) {
        return false;
    }
    const std::optional<float> enters = areaCrossing(*endArea, from, to, exclude, true, any);
    if (!enters) {
        return false;
    }
    const float dx = (to.x - from.x) * (*enters - *leaves);
    const float dy = (to.y - from.y) * (*enters - *leaves);
    return dx * dx + dy * dy < kAreaJoinSquared;
}

} // namespace coney::world
