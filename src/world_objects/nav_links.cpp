// SPDX-License-Identifier: GPL-3.0-or-later
#include "world_objects/nav_links.h"

#include <limits>
#include <span>

#include "world/path_map.h"

namespace coney::world_objects {

NavLinks::NavLinks(world::PathMap* map) : m_map(map) {
    if (m_map == nullptr) {
        return;
    }
    // Each edge's starting node: the nodes own their edges in order.
    m_fromNode.resize(m_map->edges().size());
    const std::span<const world::PathNode> nodes = m_map->nodes();
    for (std::uint32_t n = 0; n < nodes.size(); ++n) {
        for (std::uint32_t e = 0; e < nodes[n].edgeCount; ++e) {
            m_fromNode.at(nodes[n].firstEdge + e) = n;
        }
    }
    // Each node's polygon: the polygons own their nodes in order.
    m_polygonOfNode.resize(nodes.size());
    const std::span<const world::PathPolygon> polygons = m_map->polygons();
    for (std::uint32_t p = 0; p < polygons.size(); ++p) {
        for (std::uint32_t n = 0; n < polygons[p].nodeCount; ++n) {
            m_polygonOfNode.at(polygons[p].firstNode + n) = p;
        }
    }
}

void NavLinks::setKindByNumber(std::uint16_t number, std::uint16_t kind) {
    if (m_map == nullptr || number == 0) {
        return;
    }
    for (world::PathEdge& edge : m_map->mutableEdges()) {
        if (edge.door == number) {
            edge.flags = kind;
        }
    }
}

void NavLinks::openByNumber(std::uint16_t number) { setNumberOpen(number, true); }

void NavLinks::closeByNumber(std::uint16_t number) { setNumberOpen(number, false); }

void NavLinks::setNumberOpen(std::uint16_t number, bool open) {
    if (m_map == nullptr || number == 0) {
        return;
    }
    const std::span<world::PathEdge> edges = m_map->mutableEdges();
    for (std::uint32_t e = 0; e < edges.size(); ++e) {
        if (edges[e].door != number) {
            continue;
        }
        edges[e].avoid = !open;
        if (const std::optional<std::uint32_t> polygon = polygonOf(e)) {
            setPolygonExcluded(*polygon, open);
        }
    }
}

std::optional<FoundLink> NavLinks::findNearest(anim::Vec3 at, std::uint16_t kinds, float reach) const {
    if (m_map == nullptr) {
        return std::nullopt;
    }
    const std::span<const world::PathEdge> edges = m_map->edges();
    const std::span<const world::PathNode> nodes = m_map->nodes();
    std::optional<FoundLink> best;
    float bestDistance = std::numeric_limits<float>::max();
    for (std::uint32_t e = 0; e < edges.size(); ++e) {
        if ((edges[e].flags & kinds) == 0) {
            continue;
        }
        const anim::Vec3 middle =
            anim::scale(anim::add(nodes[m_fromNode[e]].position, nodes[edges[e].to].position), 0.5F);
        const float distance = anim::distance(middle, at);
        if (distance <= reach && distance < bestDistance) {
            bestDistance = distance;
            best = FoundLink{.link = e, .back = std::nullopt};
        }
    }
    if (best) {
        // The reverse: the edge from this one's end back to its start.
        const std::uint32_t from = m_fromNode[best->link];
        const world::PathNode& end = nodes[edges[best->link].to];
        for (std::uint32_t e = end.firstEdge; e < end.firstEdge + end.edgeCount; ++e) {
            if (edges[e].to == from) {
                best->back = e;
                break;
            }
        }
    }
    return best;
}

void NavLinks::setAvoid(std::uint32_t link, bool avoid) {
    if (m_map != nullptr && link < m_map->edges().size()) {
        m_map->mutableEdges()[link].avoid = avoid;
    }
}

void NavLinks::setKind(std::uint32_t link, std::uint16_t kind) {
    if (m_map != nullptr && link < m_map->edges().size()) {
        m_map->mutableEdges()[link].flags = kind;
    }
}

void NavLinks::setPolygonExcluded(std::uint32_t polygon, bool excluded) {
    if (m_map == nullptr || polygon >= m_map->polygons().size()) {
        return;
    }
    std::uint32_t& flags = m_map->mutablePolygons()[polygon].flags;
    flags = excluded ? (flags | world::kPathPolygonExcluded) : (flags & ~world::kPathPolygonExcluded);
}

std::optional<std::uint32_t> NavLinks::polygonOf(std::uint32_t link) const {
    if (link >= m_fromNode.size()) {
        return std::nullopt;
    }
    return m_polygonOfNode.at(m_fromNode[link]);
}

std::optional<std::uint32_t> NavLinks::polygonAt(anim::Vec3 at) const {
    if (m_map == nullptr) {
        return std::nullopt;
    }
    return m_map->polygonAt(at.x, at.y);
}

template <typename Change> void NavLinks::changeNearest(anim::Vec3 at, std::uint16_t kinds, Change change) {
    const std::optional<FoundLink> found = findNearest(at, kinds);
    if (!found) {
        return;
    }
    change(found->link);
    if (found->back) {
        change(*found->back);
    }
}

void NavLinks::disableNear(anim::Vec3 at) {
    changeNearest(at, link_kind::kDoor, [this](std::uint32_t link) { setAvoid(link, true); });
}

void NavLinks::enableNear(anim::Vec3 at) {
    changeNearest(at, link_kind::kDoor, [this](std::uint32_t link) { setAvoid(link, false); });
}

void NavLinks::convertJumpToDoor(anim::Vec3 at) {
    changeNearest(at, link_kind::kChoke, [this](std::uint32_t link) { setKind(link, link_kind::kDoor); });
}

} // namespace coney::world_objects
