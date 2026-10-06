// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <cstdint>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "world/path_map.h"

// Synthetic path data for the route planner's and the move action's tests: rectangles wound as the disc's walkable
// polygons are (anticlockwise seen from above), route nodes and two-way edges between them.

namespace coney::test {

/// Builds path data piece by piece.
class PathBuilder {
  public:
    /// Adds the rectangle [x0, x1] × [y0, y1] (anticlockwise, no slab lists), on the graph unless `graph` is 0, with
    /// `flags`; returns its index.
    std::uint32_t rectangle(float x0, float x1, float y0, float y1, std::int16_t graph = 1, std::uint32_t flags = 0) {
        world::PathPolygon polygon;
        polygon.firstVertex = static_cast<std::uint32_t>(m_vertices.size());
        polygon.vertexCount = 4;
        polygon.graph = graph;
        polygon.xMin = x0;
        polygon.xMax = x1;
        polygon.yMin = y0;
        polygon.yMax = y1;
        polygon.slabStarts.fill(-1);
        polygon.flags = flags;
        m_vertices.push_back(anim::Vec3{x0, y0, 0.0F});
        m_vertices.push_back(anim::Vec3{x1, y0, 0.0F});
        m_vertices.push_back(anim::Vec3{x1, y1, 0.0F});
        m_vertices.push_back(anim::Vec3{x0, y1, 0.0F});
        m_polygons.push_back(polygon);
        m_nodesOf.emplace_back();
        return static_cast<std::uint32_t>(m_polygons.size() - 1);
    }

    /// Adds the rectangle [x0, x1] × [y0, y1] as a hole (clockwise, off the graph) of the area of the polygon added
    /// last, with `flags`; returns its index.
    std::uint32_t hole(float x0, float x1, float y0, float y1, std::uint32_t flags = 7) {
        m_polygons.back().nextInArea = true;
        const std::uint32_t added = rectangle(x0, x1, y0, y1, 0, flags);
        // Clockwise: the second and fourth corners swap.
        std::swap(m_vertices[m_vertices.size() - 3], m_vertices[m_vertices.size() - 1]);
        return added;
    }

    /// Adds the polygon through `corners` (x, y) in order, which must run anticlockwise, on the graph unless `graph`
    /// is 0; returns its index.
    std::uint32_t polygon(const std::vector<std::pair<float, float>>& corners, std::int16_t graph = 1) {
        world::PathPolygon polygon;
        polygon.firstVertex = static_cast<std::uint32_t>(m_vertices.size());
        polygon.vertexCount = static_cast<std::uint32_t>(corners.size());
        polygon.graph = graph;
        polygon.xMin = polygon.xMax = corners.front().first;
        polygon.yMin = polygon.yMax = corners.front().second;
        for (const auto& [x, y] : corners) {
            polygon.xMin = std::min(polygon.xMin, x);
            polygon.xMax = std::max(polygon.xMax, x);
            polygon.yMin = std::min(polygon.yMin, y);
            polygon.yMax = std::max(polygon.yMax, y);
            m_vertices.push_back(anim::Vec3{x, y, 0.0F});
        }
        polygon.slabStarts.fill(-1);
        m_polygons.push_back(polygon);
        m_nodesOf.emplace_back();
        return static_cast<std::uint32_t>(m_polygons.size() - 1);
    }

    /// Adds a route node at (x, y, 0) owned by polygon `polygon`. Nodes are numbered when built, polygon by polygon
    /// in the order added.
    void node(std::uint32_t polygon, float x, float y) { m_nodesOf.at(polygon).push_back(anim::Vec3{x, y, 0.0F}); }

    /// Links the nodes `a` and `b` (indices as built) both ways with `flags`, the avoid bit and a door number.
    void link(std::uint32_t a, std::uint32_t b, std::uint16_t flags = 1, bool avoid = false, std::uint16_t door = 0) {
        m_links.push_back(Link{a, b, flags, avoid, door});
        m_links.push_back(Link{b, a, flags, avoid, door});
    }
    /// Links `from` to `to` one way with `flags` and the avoid bit.
    void linkOneWay(std::uint32_t from, std::uint32_t to, std::uint16_t flags = 1, bool avoid = false) {
        m_links.push_back(Link{from, to, flags, avoid, 0});
    }

    /// The path data, REQUIRing that it checks.
    [[nodiscard]] world::PathMap build() const {
        std::vector<world::PathPolygon> polygons = m_polygons;
        std::vector<anim::Vec3> positions;
        for (std::size_t p = 0; p < polygons.size(); ++p) {
            polygons[p].hasNodes = !m_nodesOf[p].empty();
            polygons[p].firstNode = static_cast<std::uint32_t>(positions.size());
            polygons[p].nodeCount = static_cast<std::uint32_t>(m_nodesOf[p].size());
            positions.insert(positions.end(), m_nodesOf[p].begin(), m_nodesOf[p].end());
        }
        std::vector<world::PathNode> nodes;
        std::vector<world::PathEdge> edges;
        for (std::uint32_t n = 0; n < positions.size(); ++n) {
            world::PathNode made{
                .position = positions[n], .firstEdge = static_cast<std::uint32_t>(edges.size()), .edgeCount = 0};
            for (const Link& link : m_links) {
                if (link.from == n) {
                    edges.push_back(
                        world::PathEdge{.to = link.to, .flags = link.flags, .avoid = link.avoid, .door = link.door});
                }
            }
            made.edgeCount = static_cast<std::uint32_t>(edges.size()) - made.firstEdge;
            nodes.push_back(made);
        }
        auto map = world::PathMap::make(m_vertices, std::move(polygons), std::move(nodes), std::move(edges));
        REQUIRE(map.has_value());
        return std::move(*map);
    }

  private:
    struct Link {
        std::uint32_t from;
        std::uint32_t to;
        std::uint16_t flags;
        bool avoid;
        std::uint16_t door;
    };
    std::vector<anim::Vec3> m_vertices;
    std::vector<world::PathPolygon> m_polygons;
    std::vector<std::vector<anim::Vec3>> m_nodesOf;
    std::vector<Link> m_links;
};

/// A U of three 2 m corridors with its corner at (x, y): the bottom [x, x+10] × [y, y+2], the right side
/// [x+8, x+10] × [y, y+10] and the top [x, x+10] × [y+8, y+10]; nodes 0 (x+2, y+1) and 1 (x+9, y+1) in the bottom,
/// 2 (x+9, y+5) in the side, 3 (x+9, y+9) and 4 (x+2, y+9) in the top, linked in a chain with flag 1. From the
/// bottom's left end the top's left end is out of a straight line.
inline world::PathMap uCorridors(float x = 0.0F, float y = 0.0F) {
    PathBuilder builder;
    const std::uint32_t u = builder.polygon({{x, y},
                                             {x + 10.0F, y},
                                             {x + 10.0F, y + 10.0F},
                                             {x, y + 10.0F},
                                             {x, y + 8.0F},
                                             {x + 8.0F, y + 8.0F},
                                             {x + 8.0F, y + 2.0F},
                                             {x, y + 2.0F}});
    builder.node(u, x + 2.0F, y + 1.0F);
    builder.node(u, x + 9.0F, y + 1.0F);
    builder.node(u, x + 9.0F, y + 5.0F);
    builder.node(u, x + 9.0F, y + 9.0F);
    builder.node(u, x + 2.0F, y + 9.0F);
    for (std::uint32_t n = 0; n < 4; ++n) {
        builder.link(n, n + 1);
    }
    return builder.build();
}

} // namespace coney::test
