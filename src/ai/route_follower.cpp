// SPDX-License-Identifier: GPL-3.0-or-later
#include "ai/route_follower.h"

#include <cmath>
#include <utility>

namespace coney::ai {

namespace {

// The distance in plan between two points.
float planDistance(anim::Vec3 a, anim::Vec3 b) { return std::hypot(b.x - a.x, b.y - a.y); }

// The angle in plan between two directions, radians in [0, π]; 0 when either has no length.
float turnBetween(anim::Vec3 a, anim::Vec3 b) {
    const float la = std::hypot(a.x, a.y);
    const float lb = std::hypot(b.x, b.y);
    if (la < 1e-6F || lb < 1e-6F) {
        return 0.0F;
    }
    const float cosine = (a.x * b.x + a.y * b.y) / (la * lb);
    return std::acos(std::fmax(-1.0F, std::fmin(1.0F, cosine)));
}

} // namespace

RouteFollower::RouteFollower(const world::PathMap& map, Route route, anim::Vec3 start, anim::Vec3 destination)
    : m_start(start) {
    for (const std::uint32_t node : route.nodes()) {
        m_points.push_back(map.nodes()[node].position);
    }
    m_points.push_back(destination);
    m_route.emplace(std::move(route));
}

anim::Vec3 RouteFollower::waypoint(const world::PathMap& map, anim::Vec3 position) {
    ++m_calls;
    const bool reached = !onLastLeg() && planDistance(position, m_points[m_index]) <= kWaypointRadius;
    if (reached || m_calls % kSkipAheadSteps == 0) {
        moveOn(map, position, reached);
    }
    return m_points[m_index];
}

std::vector<anim::Vec3> RouteFollower::ahead() const {
    return {m_points.begin() + static_cast<std::ptrdiff_t>(m_index), m_points.end()};
}

void RouteFollower::moveOn(const world::PathMap& map, anim::Vec3 position, bool reached) {
    if (reached && !onLastLeg()) {
        ++m_index;
    }
    while (!onLastLeg() && canSkip(map, position)) {
        ++m_index;
    }
    // Only the destination left: the route's nodes are no longer used.
    if (onLastLeg()) {
        m_route.reset();
    }
}

bool RouteFollower::canSkip(const world::PathMap& map, anim::Vec3 position) const {
    const anim::Vec3 next = m_points[m_index + 1];
    if (!map.walkable(position, next)) {
        return false;
    }
    if (planDistance(position, next) <= kLongLeg) {
        return true;
    }
    const anim::Vec3 previous = m_index > 0 ? m_points[m_index - 1] : m_start;
    const anim::Vec3 leg = anim::subtract(m_points[m_index], previous);
    const anim::Vec3 onward = anim::subtract(next, m_points[m_index]);
    return turnBetween(leg, onward) < kSkipTurnLimit;
}

} // namespace coney::ai
