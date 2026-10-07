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
/// A polygon's flag marking a hole cut out of an area (`+0x48` bit 4): a doorway's hole is what opening its door
/// flags (docs/research/objects.md#nav-links).
inline constexpr std::uint32_t kPathPolygonHole = 0x4;
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
    bool nextInArea = false;     ///< `+0x20` not 0: the next polygon is one of this area's holes.
    std::uint32_t area = 0;      ///< Its area's first polygon (the outline), worked out from nextInArea.
    bool hasNodes = false;       ///< `+0x4c` not 0: it has an A record, so route nodes.
    std::uint32_t firstNode = 0; ///< Its first route node: the A records take the nodes in order.
    std::uint32_t nodeCount = 0; ///< The A record's `u16 +0x00`: its route nodes.
};

/// A route node (a C record, 32 bytes).
struct PathNode {
    anim::Vec3 position;         ///< `+0x00`.
    std::uint32_t firstEdge = 0; ///< Its first edge in PathMap::edges() (the `+0x10` pointer, handed out in order).
    std::uint32_t edgeCount = 0; ///< `+0x14`.
    std::uint8_t pair = 0;       ///< `+0x1e`: not 0 for a node of a jump edge, shared with its partner.
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
    /// The partner of a jump edge's node `node`: the node it links to with the same non-zero pair byte (`+0x1e`);
    /// nothing for a node with no pair byte or no such link (docs/research/ai.md#route-jump).
    /// @orig 0x002510f8 PathNode_FindPartner (unknown)
    [[nodiscard]] std::optional<std::uint32_t> partnerOf(std::uint32_t node) const;

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

    /// The hole at (x, y): of the polygons with flag kPathPolygonHole whose box holds the point, the one; when several,
    /// those that take the point in, of which the one whose vertex average is nearest; nothing when none. **Coney
    /// choice**: when several boxes hold it and none takes it in, the nearest vertex average of them all.
    /// @orig 0x00250100 PathPolygon_FindAtPoint (unknown)
    [[nodiscard]] std::optional<std::uint32_t> holeAt(float x, float y) const;

    /// The area at (x, y): the first area whose polygons' winding numbers (the outline's and its holes', a polygon
    /// with flag 8 left out) add up to a positive sum; nothing when none. Returns the area's first polygon.
    /// @orig 0x00250708 PathArea_FindAtPoint (unknown)
    [[nodiscard]] std::optional<std::uint32_t> areaAt(float x, float y) const;
    /// The walkable-line test (docs/research/ai.md#path-planning): the segment from `from` to `to` (in plan) is
    /// crossed with the edges of the start's area's polygons that have neither flag 8 (kPathPolygonExcluded) nor any
    /// of `mask`, keeping the nearest crossing (one at the very end ignored). With none, it passes when the end lies in
    /// the same area; with one, the end's area's farthest crossing must lie within 0.02 m of it, so the segment goes
    /// straight from one area into the next: any hole on the way refuses it. **Coney choices**: the start's area is
    /// found from the point; every edge is crossed rather than the slab lists'; the hazard spheres (`0x00221f80`)
    /// are not built (only fire adds them).
    /// @orig 0x0024fbf8 PathMap_LineWalkable (unknown)
    [[nodiscard]] bool walkable(anim::Vec3 from, anim::Vec3 to, std::uint32_t mask = 0) const;

  private:
    // Checks the records against each other (counts, indices) and builds the map.
    [[nodiscard]] static std::expected<PathMap, Error> checked(PathMap map);
    // Whether `polygon` takes part in the walkable-line test under `mask`.
    [[nodiscard]] static bool usable(const PathPolygon& polygon, std::uint32_t excludeFlags);
    // The winding number of (x, y) in `polygon` (inside()'s sum).
    [[nodiscard]] int winding(const PathPolygon& polygon, float x, float y) const;
    // The nearest (or `farthest`) parameter in [0, 1) at which the segment crosses an edge of area `area`'s polygons
    // without any of `exclude`; `any` says whether the area had such a polygon at all.
    [[nodiscard]] std::optional<float> areaCrossing(std::uint32_t area, anim::Vec3 from, anim::Vec3 to,
                                                    std::uint32_t exclude, bool farthest, bool& any) const;

    std::vector<anim::Vec3> m_vertices;
    std::vector<PathPolygon> m_polygons;
    std::vector<PathNode> m_nodes;
    std::vector<PathEdge> m_edges;
    std::vector<std::int16_t> m_edgeLists;
};

} // namespace coney::world
