// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <utility>
#include <vector>

#include "animation/anim_math.h"
#include "world/path_map.h"

namespace coney::raycast {
class CollisionMesh;
}

// The route planner: how an AI finds its way over the level's path data. A request first tries the straight line;
// when that leaves the walkable polygons it searches the route graph (A*, from the destination's node back to the
// start's) and builds a route of node indices from a pool of 32, shortened where the graph and the straight lines
// allow. A route raises a use count on each of its nodes while it lives, which makes the next searches spread over
// parallel routes.
// Research: docs/research/ai.md#path-planning

namespace coney::ai {

/// Why a move failed (brain `+0x284`, docs/research/ai.md#move-action).
enum class MoveFailure : std::uint8_t {
    None = 0,    ///< It has not failed.
    NoRoute = 1, ///< No polygon under an end, or no route between them.
    Edge = 2,    ///< An edge it cannot take (flags 8 or `0x40`; not built in Coney).
    Stuck = 3,   ///< It covered less than 0.2 m in 60 updates.
    EdgeTen = 4, ///< A flag `0x10` edge it cannot take (not built in Coney).
};

/// The edge flags the planner and the move action name (the D records' low 16 bits; one flag an edge on the disc).
namespace edge_flag {
inline constexpr std::uint16_t kChoke = 0x4;     ///< A choke point: +80 when its extra costs apply.
inline constexpr std::uint16_t kEight = 0x8;     ///< +200 when its extra costs apply.
inline constexpr std::uint16_t kJumpDown = 0x4;  ///< The leg's kind 4: a jump (or a run).
inline constexpr std::uint16_t kClimb = 0x8;     ///< The leg's kind 8: a climb over a fence or wall.
inline constexpr std::uint16_t kDoor = 0x10;     ///< The leg's kind `0x10`: a door, refused while its avoid bit is set.
inline constexpr std::uint16_t kCharge = 0x40;   ///< A breakable door or pane, charged while its avoid bit is set.
inline constexpr std::uint16_t kJump = 0x80;     ///< +320 when its extra costs apply; the leg's kind: a running climb.
inline constexpr std::uint16_t kNoExtra = 0x100; ///< In the mask, turns the three extra costs off.
/// What a failed search adds to the mask before it tries again (`0x002511c8`).
inline constexpr std::uint16_t kRetry = kChoke | kEight | kJump;
/// The mask every human searches with (`0x0029a388`, route state `+0x14`): every link kind on the disc; only `0x100`
/// is outside it, and with it no failed search is retried (docs/research/ai.md#path-planning).
inline constexpr std::uint16_t kDefaultMask = 0xff;
} // namespace edge_flag

/// The planner's numbers.
struct PlannerSettings {
    /// `0x0051059c` = 1: the extra costs of flags 8, `0x80` and 4 apply (and the `0x80` detour is tried). **Coney
    /// choice**: on; the global's value in play is not traced.
    bool extraCosts = true;
    /// `0x005105a0` = 0: a route's leading nodes that the start reaches directly are skipped. **Coney choice**: 0.
    bool keepLeadingNodes = false;
};

/// The route pool's size (`0x006ca250`).
inline constexpr std::size_t kRoutePoolSize = 32;
/// A search gives up once its open list would hold more nodes than this. **Coney choice**: "fails at 128 nodes" is
/// read as the open list's size; read as the nodes closed, the search fails on a third of the reachable pairs of
/// `level99`'s nodes (docs/research/ai.md#coney).
inline constexpr std::size_t kMaxSearchNodes = 128;
/// An edge whose cost reaches this is never taken (`0x005105a4`).
inline constexpr std::uint32_t kEdgeCostCap = 65000;
/// How many of its polygon's nodes an end tries, nearest first, for one it reaches in a straight line.
inline constexpr std::size_t kEndNodeTries = 30;
/// How far a point just off every polygon may be from one and still count as on it (**Coney choice**, metres).
inline constexpr float kPolygonReach = 1.0F;
/// How far a human standing in no area looks for the nearest point on an edge of its candidate area's polygons, and
/// how near an edge ends the look at once (metres; `Nav_NearestPolygonPoint` `0x002505b0`, RoutePlanner::navPoint()).
inline constexpr float kNavEdgeReach = 20.0F;
inline constexpr float kNavEdgeNear = 0.18F;
/// The eight probes `Nav_FindPolygonNear` (`0x0024e218`) tries last, in its order, metres in plan from the point.
inline constexpr std::array<std::array<float, 2>, 8> kNavProbes{{{0.0F, 1.0F},
                                                                 {0.0F, -1.0F},
                                                                 {-1.0F, 0.0F},
                                                                 {1.0F, 0.0F},
                                                                 {1.0F, 1.0F},
                                                                 {-1.0F, -1.0F},
                                                                 {-1.0F, 1.0F},
                                                                 {1.0F, -1.0F}}};

class RoutePlanner;

/// A planned route: graph node indices in walking order, from the start's end to the destination's. It holds one of
/// the pool's 32 places and a use on each of its nodes until it is destroyed (`0x00251680`).
class Route {
  public:
    Route(const Route&) = delete;
    Route& operator=(const Route&) = delete;
    /// Moves the place and the uses to the new route.
    Route(Route&& other) noexcept;
    Route& operator=(Route&& other) noexcept;
    /// Gives the place back and lowers its nodes' use counts.
    /// @orig 0x00251680 Route_Free (unknown)
    ~Route();

    /// The nodes, in walking order.
    [[nodiscard]] std::span<const std::uint32_t> nodes() const { return m_nodes; }
    /// What the search found the route to cost, in 1/16 m.
    [[nodiscard]] std::uint32_t cost() const { return m_cost; }

  private:
    friend class RoutePlanner;
    Route(RoutePlanner& planner, std::vector<std::uint32_t> nodes, std::uint32_t cost);
    // Gives the place and the uses back now.
    void release();

    RoutePlanner* m_planner = nullptr;
    std::vector<std::uint32_t> m_nodes;
    std::uint32_t m_cost = 0;
};

/// What one search finds: the nodes in walking order and what they cost, in 1/16 m.
struct RouteSearch {
    std::vector<std::uint32_t> nodes;
    std::uint32_t cost = 0;
};

/// What a request gives: a route, or nothing when the straight line is walkable and no route is needed.
struct RoutePlan {
    std::optional<Route> route;
};

/// How far down the ground probe's ray reaches, metres. **Coney choice**: the length of the ray `0x00250760` casts is
/// not on the page; 10 m finds the floor under any point a human stands or falls at.
inline constexpr float kAreaProbeLength = 10.0F;

/// The ground probe of the level's collision `mesh` (it must outlive the probe): the area byte of the first ground a
/// ray straight down from the point finds within kAreaProbeLength (world::GroundProbe). **Coney choice**: the page
/// names "the ground's collision byte"; the triangle's area byte (`+0x09`) is the one whose values the paths' `+0x4a`
/// match on the disc (in `level80` the material byte finds no area under the upper walkway), so it is read here.
[[nodiscard]] world::GroundProbe groundProbe(const raycast::CollisionMesh& mesh);

/// The route planner of one level's path data.
class RoutePlanner {
  public:
    /// A planner over `map` (it must outlive the planner and every route made from it).
    explicit RoutePlanner(const world::PathMap& map, PlannerSettings settings = {});
    RoutePlanner(const RoutePlanner&) = delete;
    RoutePlanner& operator=(const RoutePlanner&) = delete;
    RoutePlanner(RoutePlanner&&) = delete;
    RoutePlanner& operator=(RoutePlanner&&) = delete;
    ~RoutePlanner() = default;

    /// The path data.
    [[nodiscard]] const world::PathMap& map() const { return *m_map; }

    /// The walkable-line test with the mask 0 (world::PathMap::walkable(), docs/research/ai.md#path-planning): a
    /// hole in the path polygons on the way, a fence's among them, refuses the line. A start standing in a hole is
    /// tested from its navPoint(), as the original tests from the point its human's polygon came from (`+0x2b0`).
    [[nodiscard]] bool lineClear(anim::Vec3 from, anim::Vec3 to) const;
    /// The level's ground probe, which the area lookups ask so that a point on an upper floor finds that floor's area
    /// (world::PathMap::areaAt()); without one they look in plan only.
    void setGroundProbe(world::GroundProbe probe) { m_ground = std::move(probe); }
    /// The area under `point` (world::PathMap::areaAt() with the ground probe): its first polygon.
    [[nodiscard]] std::optional<std::uint32_t> areaAt(anim::Vec3 point) const {
        return m_map->areaAt(point, &m_ground);
    }

    /// A route from `from` to `to` over edges whose flags meet `mask`, a start standing in a hole planned from its
    /// navPoint(): none needed when the straight line is walkable; MoveFailure::NoRoute when the start has no
    /// navPoint(), when the destination stands in a hole (it has no fallback), when either end lies on no polygon (or,
    /// off every polygon, none within kPolygonReach), when the ends' polygons are neither the same nor both on the
    /// graph (`+0x02`), when an end reaches none of its polygon's nodes, when the search fails, or when the pool is
    /// full.
    /// @orig 0x0029a8c0 Route_Request (unknown)
    [[nodiscard]] std::expected<RoutePlan, MoveFailure> request(anim::Vec3 from, anim::Vec3 to,
                                                                std::uint16_t mask = edge_flag::kDefaultMask);

    /// The cost of the edge `edge` into node `to`, in 1/16 m: its length × 16, the extra costs of its flag (when on
    /// and `mask` lacks `0x100`), 1600 for the avoid bit, and 40 × the routes through `to` − 8; at least 0, at most
    /// `0xffff`. **Coney choice**: the last term is read as (40 × uses) − 8, of the node the edge enters.
    /// @orig 0x00251890 Route_EdgeCost (unknown)
    [[nodiscard]] std::uint32_t edgeCost(std::uint32_t from, const world::PathEdge& edge, std::uint16_t mask) const;
    /// The routes through node `node` now (C `+0x1f`).
    [[nodiscard]] std::uint8_t uses(std::uint32_t node) const { return m_uses.at(node); }
    /// Routes held from the pool.
    [[nodiscard]] std::size_t routesInUse() const { return m_routesInUse; }

    /// One A* search over the graph from node `goal` back to node `start`, so the parent links read in walking
    /// order: the nodes from `start` to `goal` and the cost, or nothing when the heap empties, its open list outgrows
    /// kMaxSearchNodes, or every way costs more than `cap`.
    /// @orig 0x00251a70 Route_AStar (unknown)
    [[nodiscard]] std::optional<RouteSearch> search(std::uint32_t start, std::uint32_t goal, std::uint16_t mask,
                                                    std::uint32_t cap = 0xffff) const;

    /// The node a route starting at `point` would leave from: of the polygon under it (or the nearest within
    /// kPolygonReach), the nearest of up to kEndNodeTries of its nodes that `point` reaches in a straight line;
    /// nothing when there is none.
    [[nodiscard]] std::optional<std::uint32_t> startNode(anim::Vec3 point) const;

    /// The point a human standing at `point` plans from (docs/research/ai.md#path-holes), the first that an area
    /// takes in (areaAt()) of: `point` itself; the point on an edge of its candidate area's polygons
    /// (world::PathMap::candidateArea(), the outline and its holes) nearest it within kNavEdgeReach, the first edge
    /// nearer than kNavEdgeNear ending the look; the eight kNavProbes. Nothing when none is. The body is not moved.
    /// **Coney choices**: a probe keeps the point's height and its area is found by the ground probe, where the
    /// original drops it to the collision from 1 m up and needs a clear ray to it; with no edge in reach there is no
    /// edge point (the original reuses the previous call's).
    /// @orig 0x002505b0 Nav_NearestPolygonPoint (unknown)
    /// @orig 0x0024e218 Nav_FindPolygonNear (unknown)
    [[nodiscard]] std::optional<anim::Vec3> navPoint(anim::Vec3 point) const;
    /// Whether `point` stands in a hole of the path polygons: inside a polygon in plan, yet no area takes it in.
    [[nodiscard]] bool inHole(anim::Vec3 point) const;

  private:
    friend class Route;

    // The search with its retries: with `mask | 0x8c` after a failure; and, when the route takes a `0x80` edge under
    // the extra costs, once more without `0x80` capped at that cost, keeping the cheaper.
    // @orig 0x002511c8 Route_Search (unknown)
    [[nodiscard]] std::optional<RouteSearch> searchWithRetries(std::uint32_t start, std::uint32_t goal,
                                                               std::uint16_t mask) const;
    // The heuristic: the straight distance between two nodes × 16.
    // @orig 0x002517b0 Route_Heuristic (unknown)
    [[nodiscard]] std::uint32_t heuristic(std::uint32_t a, std::uint32_t b) const;
    // The polygon under `point`, or the nearest within kPolygonReach.
    [[nodiscard]] std::optional<std::uint32_t> polygonUnder(anim::Vec3 point) const;
    // The nearest of `polygon`'s nodes, of the first kEndNodeTries by distance, that `point` reaches in a straight
    // line (towards the node when `toNode`, else from it). **Coney choice**: a point just off every polygon, which
    // walks no straight line, takes the nearest node.
    // @orig 0x00251150 Route_NearestNode (unknown)
    [[nodiscard]] std::optional<std::uint32_t> endNode(std::uint32_t polygon, anim::Vec3 point, bool toNode) const;
    // Turns a searched chain into a route: leading nodes the start reaches skipped, shortcuts up to 4 nodes ahead
    // along edges that link back, trailing nodes dropped while the destination is in a straight line, then a place
    // from the pool and a use on each node.
    // @orig 0x002513a8 Route_Build (unknown)
    [[nodiscard]] std::optional<Route> build(std::vector<std::uint32_t> chain, std::uint32_t cost, anim::Vec3 from,
                                             anim::Vec3 to);
    // Whether node `from` has an edge to node `to`.
    [[nodiscard]] bool linked(std::uint32_t from, std::uint32_t to) const;

    const world::PathMap* m_map;
    PlannerSettings m_settings;
    world::GroundProbe m_ground; // the level's ground under a point, for the area lookups
    std::vector<std::uint8_t> m_uses;
    std::size_t m_routesInUse = 0;
};

} // namespace coney::ai
