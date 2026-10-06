// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "ai/route_planner.h"
#include "animation/anim_math.h"
#include "world/path_map.h"

// Following a route: the waypoints are the route's nodes and then the destination. The follower gives the move
// action the waypoint to aim at, moves on when the human reaches it, skips the ones it can already walk past in a
// straight line, and frees the route once only the destination is left.
// Research: docs/research/ai.md#route-follow

namespace coney::ai {

/// A waypoint counts as reached within this distance (the route state's `+0x08`).
inline constexpr float kWaypointRadius = 0.25F;
/// The skip-ahead runs on every this many calls, besides when a waypoint is reached.
inline constexpr std::uint32_t kSkipAheadSteps = 6;
/// A leg longer than this is skipped onto only when it turns less than kSkipTurnLimit against the previous leg.
inline constexpr float kLongLeg = 5.0F;
/// About 135°, in radians.
inline constexpr float kSkipTurnLimit = 2.356F;

/// A route being followed from `start` to `destination`.
class RouteFollower {
  public:
    /// Follows `route` (taken over, its nodes on `planner`'s map) from `start` to `destination`.
    RouteFollower(const RoutePlanner& planner, Route route, anim::Vec3 start, anim::Vec3 destination);

    /// The waypoint for a human at `position`: when it is within kWaypointRadius of the current one, or on every
    /// kSkipAheadSteps-th call, the follower moves on, skipping waypoints it reaches in a straight line on `map`
    /// (onto a leg longer than kLongLeg only when it turns less than kSkipTurnLimit). The route is freed once the
    /// destination is the waypoint.
    /// @orig 0x0029aa88 Route_Follow (unknown)
    [[nodiscard]] anim::Vec3 waypoint(const RoutePlanner& planner, anim::Vec3 position);
    /// The waypoints still ahead, the current first (the destination last): what the corner speed looks at.
    [[nodiscard]] std::vector<anim::Vec3> ahead() const;
    /// The index of the current waypoint (the destination is the last).
    [[nodiscard]] std::size_t index() const { return m_index; }
    /// The link kind of the leg to the current waypoint (the D record that steps from the waypoint before to it, on
    /// the waypoint's own records) and its avoid bit; kind 0 for the first waypoint and the destination.
    /// @orig 0x00251070 Route_LegKind (unknown)
    [[nodiscard]] std::uint16_t legKind() const { return m_legKinds[m_index]; }
    [[nodiscard]] bool legAvoided() const { return m_legAvoided[m_index]; }
    /// Moves on past the current waypoint, as when it is reached (a climb over its leg has ended beyond it).
    void passWaypoint(const RoutePlanner& planner, anim::Vec3 position) { moveOn(planner, position, true); }
    /// Whether the destination is the waypoint (the route is freed).
    [[nodiscard]] bool onLastLeg() const { return m_index + 1 >= m_points.size(); }

  private:
    // Moves on: past a reached waypoint, then past each one the human can skip.
    // @orig 0x0029b6d8 Route_MoveOn (unknown)
    void moveOn(const RoutePlanner& planner, anim::Vec3 position, bool reached);
    // Whether the human at `position` may skip the current waypoint for the next: it walks there in a straight line,
    // and a long leg turns little.
    // @orig 0x0029b4b8 Route_CanSkip (unknown)
    [[nodiscard]] bool canSkip(const RoutePlanner& planner, anim::Vec3 position) const;

    std::optional<Route> m_route;
    std::vector<anim::Vec3> m_points;      // the nodes' positions, then the destination
    std::vector<std::uint16_t> m_legKinds; // each waypoint's leg kind (0 for the first and the destination)
    std::vector<bool> m_legAvoided;        // each waypoint's leg's avoid bit
    anim::Vec3 m_start;
    std::size_t m_index = 0;
    std::uint32_t m_calls = 0;
};

} // namespace coney::ai
