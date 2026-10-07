// SPDX-License-Identifier: GPL-3.0-or-later
#include "ai/route_follower.h"

#include <algorithm>
#include <cmath>
#include <span>
#include <utility>

namespace coney::ai {

namespace {

// The most points a jump edge's queue lays (`0x00299538`).
constexpr int kMaxQueuePoints = 6;
// The updates in a second: one update's travel is the speed over this (the arrival test's lead).
constexpr float kUpdatesPerSecond = 30.0F;

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

RouteFollower::RouteFollower(const RoutePlanner& planner, Route route, anim::Vec3 start, anim::Vec3 destination)
    : m_start(start) {
    const world::PathMap& map = planner.map();
    // Each leg's kind: the record on the waypoint's node that names the node before it.
    const std::span<const std::uint32_t> nodes = route.nodes();
    for (std::size_t i = 0; i < nodes.size(); ++i) {
        m_nodes.push_back(nodes[i]);
        m_points.push_back(map.nodes()[nodes[i]].position);
        std::uint16_t kind = 0;
        bool avoided = false;
        if (i > 0) {
            for (const world::PathEdge& edge : map.edgesOf(nodes[i])) {
                if (edge.to == nodes[i - 1]) {
                    kind = edge.flags;
                    avoided = edge.avoid;
                    break;
                }
            }
        }
        m_legKinds.push_back(kind);
        m_legAvoided.push_back(avoided);
    }
    m_points.push_back(destination);
    m_legKinds.push_back(0);
    m_legAvoided.push_back(false);
    m_route.emplace(std::move(route));
    placeJumpPoint(planner, start);
}

bool RouteFollower::isJumpLeg(std::uint16_t kind) {
    return (kind & edge_flag::kJumpDown) != 0 && (kind & (edge_flag::kClimb | edge_flag::kJump)) == 0;
}

anim::Vec3 RouteFollower::waypoint(const RoutePlanner& planner, anim::Vec3 position, float speed) {
    ++m_calls;
    // A jump's take-off is reached one update's travel early, as the arrival test leads by it.
    const bool takeOff = !onLastLeg() && isJumpLeg(m_legKinds[m_index + 1]);
    const float lead = takeOff ? speed / kUpdatesPerSecond : 0.0F;
    const bool reached = !onLastLeg() && planDistance(position, m_points[m_index]) - lead <= kWaypointRadius;
    if (reached || m_calls % kSkipAheadSteps == 0) {
        moveOn(planner, position, reached);
    }
    return m_points[m_index];
}

std::vector<anim::Vec3> RouteFollower::ahead() const {
    return {m_points.begin() + static_cast<std::ptrdiff_t>(m_index), m_points.end()};
}

void RouteFollower::moveOn(const RoutePlanner& planner, anim::Vec3 position, bool reached) {
    const std::size_t before = m_index;
    if (reached && !onLastLeg()) {
        ++m_index;
    }
    while (!onLastLeg() && canSkip(planner, position)) {
        ++m_index;
    }
    if (m_index != before) {
        placeJumpPoint(planner, position);
    }
    // Only the destination left: the route's nodes are no longer used.
    if (onLastLeg()) {
        m_route.reset();
    }
}

bool RouteFollower::canSkip(const RoutePlanner& planner, anim::Vec3 position) const {
    // A jump's take-off is never skipped (its leg on is not walked), nor its landing before the jump is made.
    if (isJumpLeg(m_legKinds[m_index]) || isJumpLeg(m_legKinds[m_index + 1])) {
        return false;
    }
    const anim::Vec3 next = m_points[m_index + 1];
    if (!planner.lineClear(position, next)) {
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

void RouteFollower::placeJumpPoint(const RoutePlanner& planner, anim::Vec3 position) {
    if (onLastLeg() || m_index >= m_nodes.size()) {
        return;
    }
    const world::PathMap& map = planner.map();
    const std::uint32_t node = m_nodes[m_index];
    const anim::Vec3 at = map.nodes()[node].position;
    const std::optional<std::uint32_t> partner = map.partnerOf(node);
    if (!partner) {
        return;
    }
    const anim::Vec3 other = map.nodes()[*partner].position;
    const anim::Vec3 edge = anim::subtract(at, other);
    const float lengthSquared = anim::dot(edge, edge);
    if (lengthSquared < 1e-8F) {
        return;
    }
    if (isJumpLeg(m_legKinds[m_index])) {
        // The landing: the take-off point (the human's position) projected onto the line through the two nodes, not
        // clamped to the segment, so the jump goes square across the landing edge.
        const float along = anim::dot(anim::subtract(position, other), edge) / lengthSquared;
        m_points[m_index] = anim::add(other, anim::scale(edge, along));
        return;
    }
    if (!isJumpLeg(m_legKinds[m_index + 1])) {
        return;
    }
    // The take-off: trunc(length) + 1 points, at most six, evenly from the partner to this node; a lone human takes the
    // nearest one he can walk to in a straight line, else the node.
    const int count = std::min(kMaxQueuePoints, static_cast<int>(std::sqrt(lengthSquared)) + 1);
    if (count < 2) {
        return;
    }
    std::optional<anim::Vec3> best;
    float bestDistance = 0.0F;
    for (int k = 0; k < count; ++k) {
        const anim::Vec3 point =
            anim::add(other, anim::scale(edge, static_cast<float>(k) / static_cast<float>(count - 1)));
        const float distance = planDistance(position, point);
        if ((!best || distance < bestDistance) && planner.lineClear(position, point)) {
            best = point;
            bestDistance = distance;
        }
    }
    if (best) {
        m_points[m_index] = *best;
    }
}

} // namespace coney::ai
