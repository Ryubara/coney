// SPDX-License-Identifier: GPL-3.0-or-later
#include "ai/route_planner.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <queue>
#include <utility>

namespace coney::ai {

namespace {

// Costs are u16 in 1/16 m; a sum saturates at the largest.
constexpr std::uint32_t kCostMax = 0xffff;
constexpr float kCostPerMetre = 16.0F;
// The extra costs of the three flags, the avoid bit, and the use term's factor and offset.
constexpr std::uint32_t kEightCost = 200;
constexpr std::uint32_t kJumpCost = 320;
constexpr std::uint32_t kChokeCost = 80;
constexpr std::uint32_t kAvoidCost = 1600;
constexpr int kUseCost = 40;
constexpr int kUseOffset = 8;
// A route looks this many nodes ahead for one that links back.
constexpr std::size_t kShortcutReach = 4;
// A g cost no node has yet.
constexpr std::uint32_t kUnreached = 0x10000;

// Adds two costs, saturating at kCostMax.
std::uint32_t addCost(std::uint32_t a, std::uint32_t b) { return std::min(kCostMax, a + b); }

} // namespace

Route::Route(RoutePlanner& planner, std::vector<std::uint32_t> nodes, std::uint32_t cost)
    : m_planner(&planner), m_nodes(std::move(nodes)), m_cost(cost) {
    ++m_planner->m_routesInUse;
    for (const std::uint32_t node : m_nodes) {
        std::uint8_t& uses = m_planner->m_uses.at(node);
        uses = static_cast<std::uint8_t>(std::min(0xff, uses + 1));
    }
}

Route::Route(Route&& other) noexcept
    : m_planner(std::exchange(other.m_planner, nullptr)), m_nodes(std::move(other.m_nodes)), m_cost(other.m_cost) {}

Route& Route::operator=(Route&& other) noexcept {
    if (this != &other) {
        release();
        m_planner = std::exchange(other.m_planner, nullptr);
        m_nodes = std::move(other.m_nodes);
        m_cost = other.m_cost;
    }
    return *this;
}

Route::~Route() { release(); }

void Route::release() {
    if (m_planner == nullptr) {
        return;
    }
    for (const std::uint32_t node : m_nodes) {
        std::uint8_t& uses = m_planner->m_uses.at(node);
        uses = static_cast<std::uint8_t>(uses > 0 ? uses - 1 : 0);
    }
    --m_planner->m_routesInUse;
    m_planner = nullptr;
}

RoutePlanner::RoutePlanner(const world::PathMap& map, PlannerSettings settings)
    : m_map(&map), m_settings(settings), m_uses(map.nodes().size(), 0) {}

std::expected<RoutePlan, MoveFailure> RoutePlanner::request(anim::Vec3 from, anim::Vec3 to, std::uint16_t mask) {
    // 1. The start's polygon.
    const std::optional<std::uint32_t> startPolygon = polygonUnder(from);
    if (!startPolygon) {
        return std::unexpected(MoveFailure::NoRoute);
    }
    // 2. The straight line first.
    if (lineClear(from, to)) {
        return RoutePlan{};
    }
    // 3. Both ends in one polygon or both on the graph, and each end's node.
    const std::optional<std::uint32_t> endPolygon = polygonUnder(to);
    if (!endPolygon) {
        return std::unexpected(MoveFailure::NoRoute);
    }
    const std::span<const world::PathPolygon> polygons = m_map->polygons();
    if (*startPolygon != *endPolygon && (polygons[*startPolygon].graph == 0 || polygons[*endPolygon].graph == 0)) {
        return std::unexpected(MoveFailure::NoRoute);
    }
    const std::optional<std::uint32_t> startNode = endNode(*startPolygon, from, true);
    const std::optional<std::uint32_t> goalNode = endNode(*endPolygon, to, false);
    if (!startNode || !goalNode) {
        return std::unexpected(MoveFailure::NoRoute);
    }
    // 4-5. The search, with its retries.
    std::optional<RouteSearch> found = *startNode == *goalNode
                                           ? std::optional(RouteSearch{.nodes = {*startNode}, .cost = 0})
                                           : searchWithRetries(*startNode, *goalNode, mask);
    if (!found) {
        return std::unexpected(MoveFailure::NoRoute);
    }
    // 6. The route.
    std::optional<Route> route = build(std::move(found->nodes), found->cost, from, to);
    if (!route) {
        return std::unexpected(MoveFailure::NoRoute);
    }
    return RoutePlan{.route = std::move(route)};
}

std::uint32_t RoutePlanner::edgeCost(std::uint32_t from, const world::PathEdge& edge, std::uint16_t mask) const {
    const std::span<const world::PathNode> nodes = m_map->nodes();
    float cost = anim::distance(nodes[from].position, nodes[edge.to].position) * kCostPerMetre;
    if (m_settings.extraCosts && (mask & edge_flag::kNoExtra) == 0) {
        if ((edge.flags & edge_flag::kEight) != 0) {
            cost += static_cast<float>(kEightCost);
        }
        if ((edge.flags & edge_flag::kJump) != 0) {
            cost += static_cast<float>(kJumpCost);
        }
        if ((edge.flags & edge_flag::kChoke) != 0) {
            cost += static_cast<float>(kChokeCost);
        }
    }
    if (edge.avoid) {
        cost += static_cast<float>(kAvoidCost);
    }
    cost += static_cast<float>(kUseCost * m_uses.at(edge.to) - kUseOffset);
    return static_cast<std::uint32_t>(std::clamp(cost, 0.0F, static_cast<float>(kCostMax)));
}

std::uint32_t RoutePlanner::heuristic(std::uint32_t a, std::uint32_t b) const {
    const std::span<const world::PathNode> nodes = m_map->nodes();
    return std::min(kCostMax,
                    static_cast<std::uint32_t>(anim::distance(nodes[a].position, nodes[b].position) * kCostPerMetre));
}

std::optional<RouteSearch> RoutePlanner::search(std::uint32_t start, std::uint32_t goal, std::uint16_t mask,
                                                std::uint32_t cap) const {
    const std::size_t count = m_map->nodes().size();
    if (start >= count || goal >= count) {
        return std::nullopt;
    }
    std::vector<std::uint32_t> g(count, kUnreached);
    std::vector<std::uint32_t> parent(count, 0);
    std::vector<bool> closed(count, false);
    // The open list: a binary min-heap on f, a node pushed again when its g improves (the stale entry is skipped).
    using Entry = std::pair<std::uint32_t, std::uint32_t>; // f, node
    std::priority_queue<Entry, std::vector<Entry>, std::greater<>> open;
    g[goal] = 0;
    parent[goal] = goal;
    open.emplace(heuristic(goal, start), goal);
    std::size_t openCount = 1; // nodes on the open list, each once however often it was pushed
    while (!open.empty()) {
        const auto [f, node] = open.top();
        open.pop();
        if (closed[node]) {
            continue;
        }
        if (f > cap) {
            return std::nullopt;
        }
        if (node == start) {
            // The parent links run from the start to the goal: walking order.
            RouteSearch found{.nodes = {start}, .cost = g[start]};
            for (std::uint32_t at = start; at != goal; at = parent[at]) {
                found.nodes.push_back(parent[at]);
            }
            return found;
        }
        closed[node] = true;
        --openCount;
        for (const world::PathEdge& edge : m_map->edgesOf(node)) {
            if ((edge.flags & mask) == 0 || closed[edge.to]) {
                continue;
            }
            const std::uint32_t cost = edgeCost(node, edge, mask);
            if (cost >= kEdgeCostCap) {
                continue;
            }
            const std::uint32_t reached = addCost(g[node], cost);
            if (reached < g[edge.to]) {
                // A node new to the open list must fit in it.
                if (g[edge.to] == kUnreached && ++openCount > kMaxSearchNodes) {
                    return std::nullopt;
                }
                g[edge.to] = reached;
                parent[edge.to] = node;
                open.emplace(addCost(reached, heuristic(edge.to, start)), edge.to);
            }
        }
    }
    return std::nullopt;
}

std::optional<RouteSearch> RoutePlanner::searchWithRetries(std::uint32_t start, std::uint32_t goal,
                                                           std::uint16_t mask) const {
    std::uint16_t used = mask;
    std::optional<RouteSearch> found = search(start, goal, used);
    // Retried only when the mask (without 0x100) is not every kind already.
    if (!found && (mask & 0xffU) != 0xffU) {
        used = static_cast<std::uint16_t>(mask | edge_flag::kRetry);
        found = search(start, goal, used);
    }
    if (!found || !m_settings.extraCosts || (used & edge_flag::kNoExtra) != 0) {
        return found;
    }
    // A route over a jump edge: look for one without, no dearer than it.
    bool jumps = false;
    for (std::size_t i = 0; i + 1 < found->nodes.size() && !jumps; ++i) {
        for (const world::PathEdge& edge : m_map->edgesOf(found->nodes[i + 1])) {
            jumps = jumps || (edge.to == found->nodes[i] && (edge.flags & used & edge_flag::kJump) != 0);
        }
    }
    if (jumps) {
        std::optional<RouteSearch> detour =
            search(start, goal, static_cast<std::uint16_t>(used & ~edge_flag::kJump), found->cost);
        if (detour && detour->cost < found->cost) {
            return detour;
        }
    }
    return found;
}

std::optional<std::uint32_t> RoutePlanner::polygonUnder(anim::Vec3 point) const {
    if (const std::optional<std::uint32_t> polygon = m_map->polygonAt(point.x, point.y)) {
        return polygon;
    }
    return m_map->nearestPolygon(point.x, point.y, kPolygonReach);
}

std::optional<std::uint32_t> RoutePlanner::endNode(std::uint32_t polygon, anim::Vec3 point, bool toNode) const {
    const world::PathPolygon& owner = m_map->polygons()[polygon];
    if (!owner.hasNodes || owner.nodeCount == 0) {
        return std::nullopt;
    }
    std::vector<std::pair<float, std::uint32_t>> byDistance;
    byDistance.reserve(owner.nodeCount);
    for (std::uint32_t n = owner.firstNode; n < owner.firstNode + owner.nodeCount; ++n) {
        byDistance.emplace_back(anim::distance(point, m_map->nodes()[n].position), n);
    }
    std::ranges::sort(byDistance);
    // A point just off every polygon walks no straight line: it takes the nearest node.
    if (!m_map->polygonAt(point.x, point.y)) {
        return byDistance.front().second;
    }
    const std::size_t tries = std::min(byDistance.size(), kEndNodeTries);
    for (std::size_t i = 0; i < tries; ++i) {
        const anim::Vec3 node = m_map->nodes()[byDistance[i].second].position;
        if (toNode ? lineClear(point, node) : lineClear(node, point)) {
            return byDistance[i].second;
        }
    }
    return std::nullopt;
}

bool RoutePlanner::linked(std::uint32_t from, std::uint32_t to) const {
    return std::ranges::any_of(m_map->edgesOf(from), [to](const world::PathEdge& edge) { return edge.to == to; });
}

std::optional<Route> RoutePlanner::build(std::vector<std::uint32_t> chain, std::uint32_t cost, anim::Vec3 from,
                                         anim::Vec3 to) {
    const auto position = [this](std::uint32_t node) { return m_map->nodes()[node].position; };
    // Leading nodes the start reaches directly.
    if (!m_settings.keepLeadingNodes) {
        while (chain.size() > 1 && lineClear(from, position(chain[1]))) {
            chain.erase(chain.begin());
        }
    }
    // Shortcuts: from each node, the farthest of the next kShortcutReach that links back to it.
    for (std::size_t i = 0; i + 2 < chain.size(); ++i) {
        const std::size_t last = std::min(chain.size() - 1, i + 1 + kShortcutReach);
        for (std::size_t j = last; j >= i + 2; --j) {
            if (linked(chain[j], chain[i])) {
                chain.erase(chain.begin() + static_cast<std::ptrdiff_t>(i + 1),
                            chain.begin() + static_cast<std::ptrdiff_t>(j));
                break;
            }
        }
    }
    // Trailing nodes while the destination is in a straight line from the node before.
    while (chain.size() > 1 && lineClear(position(chain[chain.size() - 2]), to)) {
        chain.pop_back();
    }
    if (m_routesInUse >= kRoutePoolSize) {
        return std::nullopt;
    }
    return Route(*this, std::move(chain), cost);
}

} // namespace coney::ai
