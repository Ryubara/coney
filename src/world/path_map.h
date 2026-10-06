// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <vector>

#include "animation/anim_math.h"
#include "core/error.h"

// The level's path data decoded: the polygons an AI walks on (the "paths"), and the route graph over them, whose nodes
// are the C records and whose edges are the D records. The AI's route planner (ai::RoutePlanner) searches the graph;
// the inside and walkable-line tests here are what it and the move action ask of the polygons. Game axes, z up.
// Research: docs/research/level-loading.md#path-data, docs/research/ai.md#path-planning

namespace coney::world {

/// A polygon's flag that keeps it out of the walkable-line test whatever the caller's mask (`+0x48` bit 8).
inline constexpr std::uint32_t kPathPolygonExcluded = 0x8;
/// The slabs a polygon's box is cut into across y, each with its own list of the edges that reach it.
inline constexpr std::size_t kPathSlabCount = 16;

/// One walkable polygon (a "path", 0x50 bytes).
struct PathPolygon {
    std::uint32_t firstVertex =
        0; ///< Its first vertex in PathMap::vertices(); the polygon owns the next `vertexCount`.
    std::uint32_t vertexCount = 0; ///< `+0x00`.
    std::int16_t graph = 0;        ///< `+0x02`: not 0 when the polygon is on the route graph (inferred).
    float xMin = 0.0F;             ///< `+0x08`.
    float xMax = 0.0F;             ///< `+0x0c`.
    float yMin = 0.0F;             ///< `+0x10`.
    float yMax = 0.0F;             ///< `+0x14`.
    /// `+0x28`: where each slab's list starts in PathMap::edgeLists(), −1 for none; a negative first start means the
    /// polygon has no lists and every edge is walked.
    std::array<std::int16_t, kPathSlabCount> slabStarts{};
    std::uint32_t flags = 0;     ///< `+0x48`.
    bool hasNodes = false;       ///< `+0x4c` not 0: it has an A record, so route nodes.
    std::uint32_t firstNode = 0; ///< Its first route node: the A records take the nodes in order.
    std::uint32_t nodeCount = 0; ///< The A record's `u16 +0x00`: its route nodes.
};

/// A route node (a C record, 32 bytes).
struct PathNode {
    anim::Vec3 position;         ///< `+0x00`.
    std::uint32_t firstEdge = 0; ///< Its first edge in PathMap::edges() (the `+0x10` pointer, handed out in order).
    std::uint32_t edgeCount = 0; ///< `+0x14`.
};

/// A route edge (a D record, 8 bytes): one way from the node that owns it to `to`.
struct PathEdge {
    std::uint32_t to = 0;    ///< `+0x00`: the node it leads to.
    std::uint16_t flags = 0; ///< `+0x04`'s low 16 bits.
    bool avoid = false;      ///< `+0x04`'s bit 31: costs 1600 more.
    std::uint16_t door = 0;  ///< `+0x06`'s low 13 bits: the number of the door the link passes, 0 for none.
};

/// The decoded path data of one level.
class PathMap {
  public:
    /// Decodes the path data chunk `chunk` (its 0x20-byte header, the A records, vertices, polygons, C and D records
    /// and the slab edge lists), handing out the vertices, A records and edges in order as the original's fix-up does,
    /// and the nodes to the A records in order (**Coney choice**: the A record's `+0x08` is not read; on the disc it
    /// is not always that running index, while the node counts always add up to the C records).
    /// Fails as inspectPathData() does, with ErrorCode::Invalid when the polygons' vertex or A counts, the nodes' edge
    /// counts, an A record's nodes, an edge's node or a slab start disagree with the records there are.
    /// @orig 0x0024e720 PathData_OnLoaded (unknown)
    [[nodiscard]] static std::expected<PathMap, Error> decode(std::span<const std::byte> chunk);

    /// A map from records already in memory (the tests' synthetic levels). The same checks as decode().
    [[nodiscard]] static std::expected<PathMap, Error> make(std::vector<anim::Vec3> vertices,
                                                            std::vector<PathPolygon> polygons,
                                                            std::vector<PathNode> nodes, std::vector<PathEdge> edges,
                                                            std::vector<std::int16_t> edgeLists = {});

    [[nodiscard]] std::span<const PathPolygon> polygons() const { return m_polygons; }
    [[nodiscard]] std::span<const anim::Vec3> vertices() const { return m_vertices; }
    [[nodiscard]] std::span<const PathNode> nodes() const { return m_nodes; }
    [[nodiscard]] std::span<const PathEdge> edges() const { return m_edges; }
    [[nodiscard]] std::span<const std::int16_t> edgeLists() const { return m_edgeLists; }
    /// The edges for the doors' and panes' changes to the links (world_objects::NavLinks): an edge's kind and avoid
    /// bit. The count never changes.
    [[nodiscard]] std::span<PathEdge> mutableEdges() { return m_edges; }
    /// The polygons, for the same changes to a polygon's flags. The count never changes.
    [[nodiscard]] std::span<PathPolygon> mutablePolygons() { return m_polygons; }
    /// The edges leaving node `node`.
    [[nodiscard]] std::span<const PathEdge> edgesOf(std::uint32_t node) const;

    /// Whether (x, y) lies inside `polygon`: outside its box no; else the winding number of the edges listed for the
    /// point's slab (every edge when it has no lists), each edge that crosses the point's row to its left counting
    /// +1 going down in y and −1 going up (edges flatter than 0.0001 in y skipped); inside when the sum is positive.
    /// **Coney choice**: which direction counts +1 is not traced; the disc's polygons are wound so that this one
    /// takes in the route nodes they own (docs/research/ai.md#coney).
    /// @orig 0x0024eef0 PathPolygon_Contains (unknown)
    [[nodiscard]] bool inside(const PathPolygon& polygon, float x, float y) const;
    /// The first polygon that takes in (x, y) and has none of `excludeFlags`; nothing when none does.
    [[nodiscard]] std::optional<std::uint32_t> polygonAt(float x, float y, std::uint32_t excludeFlags = 0) const;
    /// The polygon whose outline passes nearest (x, y) within `reach` metres, for a point just off every polygon;
    /// nothing when none is that near. **Coney choice**: stands for the polygon search's fallbacks (`0x002505b0`,
    /// `0x0024e218`), which are not traced.
    [[nodiscard]] std::optional<std::uint32_t> nearestPolygon(float x, float y, float reach,
                                                              std::uint32_t excludeFlags = 0) const;

    /// The walkable-line test: whether the segment from `from` to `to` (in plan) never leaves the polygons that have
    /// neither flag 8 (kPathPolygonExcluded) nor any of `mask`. **Coney choice**: the segment is cut at every edge of
    /// those polygons it crosses and each piece's middle (and both ends) must lie inside one of them, which is the
    /// same answer as the original's walk from polygon to polygon by slab (`0x0024e938`); the collision test that
    /// refuses a blocked segment first (`0x00221f80`) is not made.
    /// @orig 0x0024fbf8 PathMap_LineWalkable (unknown)
    [[nodiscard]] bool walkable(anim::Vec3 from, anim::Vec3 to, std::uint32_t mask = 0) const;

  private:
    // Checks the records against each other (counts, indices) and builds the map.
    [[nodiscard]] static std::expected<PathMap, Error> checked(PathMap map);
    // Whether `polygon` takes part in the walkable-line test under `mask`.
    [[nodiscard]] static bool usable(const PathPolygon& polygon, std::uint32_t excludeFlags);

    std::vector<anim::Vec3> m_vertices;
    std::vector<PathPolygon> m_polygons;
    std::vector<PathNode> m_nodes;
    std::vector<PathEdge> m_edges;
    std::vector<std::int16_t> m_edgeLists;
};

} // namespace coney::world
