// SPDX-License-Identifier: GPL-3.0-or-later
#include "ai/move_action.h"

#include <algorithm>
#include <cmath>
#include <expected>
#include <utility>

#include "ai/brain.h"
#include "human/locomotion.h"

namespace coney::ai {

namespace {

// The distance in plan between two points, and its square.
float planDistance(anim::Vec3 a, anim::Vec3 b) { return std::hypot(b.x - a.x, b.y - a.y); }
float planDistanceSquared(anim::Vec3 a, anim::Vec3 b) {
    const float dx = b.x - a.x;
    const float dy = b.y - a.y;
    return dx * dx + dy * dy;
}

// How many points of a corner's predicted arc are tested, by its turn radius: bands of 3, 5 and 7 m.
int cornerSamples(float turnRadius) {
    if (turnRadius < 3.0F) {
        return 4;
    }
    if (turnRadius < 5.0F) {
        return 5;
    }
    return turnRadius < 7.0F ? 6 : 7;
}

// Whether a point lies on a walkable polygon.
bool onPolygons(const world::PathMap& map, anim::Vec3 point) { return map.polygonAt(point.x, point.y).has_value(); }

} // namespace

float gaitSpeed(const human::Speeds& speeds, int gait) {
    switch (gait) {
    case 1:
        return speeds.sneak;
    case 3:
        return speeds.jog;
    case 4:
        return speeds.run;
    case 5:
        return speeds.sprint;
    default:
        return speeds.walk;
    }
}

ActionStatus MoveAction::start(Brain& brain) {
    const anim::Vec3 position = brain.human().position();
    if (planDistance(position, m_request.point) <= kMoveNothingToDo) {
        return finish(brain);
    }
    brain.setMoveFailure(MoveFailure::None);
    brain.setMoveAim(m_request.point, m_request.radius);
    m_stuckFrom = position;
    RoutePlanner* planner = brain.planner();
    if (planner == nullptr) {
        return ActionStatus::Running;
    }
    std::expected<RoutePlan, MoveFailure> plan = planner->request(position, m_request.point);
    if (!plan) {
        brain.setMoveFailure(plan.error());
        return finish(brain);
    }
    if (std::optional<Route>& route = plan->route; route.has_value()) {
        m_follower.emplace(planner->map(), std::move(*route), position, m_request.point);
    }
    return ActionStatus::Running;
}

ActionStatus MoveAction::update(Brain& brain) {
    human::Human& human = brain.human();
    // 1. Wait while the human is busy (Human_IsBusy): the move stays as it was.
    if (human::stickBusy(human.gateInput())) {
        return ActionStatus::Running;
    }
    ++m_updates;
    const anim::Vec3 position = human.position();
    // 2. Now and then, a route whose point has come into a straight line is dropped.
    RoutePlanner* planner = brain.planner();
    if (m_follower && planner != nullptr && (m_updates + brain.slot()) % kStraightCheckSteps == 0 &&
        planner->map().walkable(position, m_request.point)) {
        m_follower.reset();
    }
    // 3. Arrived; or close in plan but on another level.
    const float distance = planDistance(position, m_request.point);
    if (distance <= m_request.radius ||
        (distance <= kMoveHeightReach && std::fabs(position.z - m_request.point.z) > kMoveHeightGap)) {
        return finish(brain);
    }
    // 7. The aim: the route's waypoint, else the point.
    anim::Vec3 aim = m_request.point;
    float aimRadius = m_request.radius;
    if (m_follower && planner != nullptr) {
        aim = m_follower->waypoint(planner->map(), position);
        aimRadius = m_follower->onLastLeg() ? m_request.radius : kWaypointRadius;
    }
    brain.setMoveAim(aim, aimRadius);
    const anim::Vec3 way = anim::subtract(aim, position);
    const float heading = std::hypot(way.x, way.y) > 1e-4F ? human::headingOf(way) : human.heading();
    // 8. Standing far off the way, turn on the spot first; else go at the corner speed.
    if (human.speed() < kStandingSpeed && std::fabs(human::wrapAngle(heading - human.heading())) > kTurnOnSpotAngle) {
        brain.setMoveHeading(heading, 0.0F);
        m_movingSteps = 0;
        m_stuckFrom = position;
        return ActionStatus::Running;
    }
    brain.setMoveHeading(heading, cornerSpeed(brain, position));
    // 9. Stuck.
    if (stuck(position)) {
        brain.setMoveFailure(MoveFailure::Stuck);
        return finish(brain);
    }
    return ActionStatus::Running;
}

bool MoveAction::abort(Brain& brain) {
    brain.stopMove();
    return true;
}

float MoveAction::cornerSpeed(Brain& brain, anim::Vec3 position) {
    const human::Speeds& speeds = brain.human().speeds();
    const float top = gaitSpeed(speeds, m_request.gait);
    if (!m_follower || brain.planner() == nullptr) {
        return top;
    }
    const std::vector<anim::Vec3> ahead = m_follower->ahead();
    if (m_cornersAt != m_follower->index()) {
        // The corners at the next two waypoints, each from the leg before it to the leg after.
        const float floor = std::min(speeds.walk, top);
        m_firstCornerSpeed = top;
        m_secondCornerSpeed = top;
        if (ahead.size() >= 2 && planDistance(position, ahead[0]) > kCornerMinDistance) {
            m_firstCornerSpeed = cornerTrial(brain, ahead[0], anim::subtract(ahead[0], position),
                                             anim::subtract(ahead[1], ahead[0]), top, floor);
        }
        if (ahead.size() >= 3) {
            m_secondCornerSpeed = cornerTrial(brain, ahead[1], anim::subtract(ahead[1], ahead[0]),
                                              anim::subtract(ahead[2], ahead[1]), top, floor);
        }
        const float braking = m_firstCornerSpeed * kBrakingSeconds;
        m_brakingSquared = braking * braking;
        m_cornersAt = m_follower->index();
    }
    const float speed = planDistanceSquared(position, ahead.front()) > m_brakingSquared
                            ? m_firstCornerSpeed
                            : std::min(m_firstCornerSpeed, m_secondCornerSpeed);
    return std::min(speed, top);
}

float MoveAction::cornerTrial(const Brain& brain, anim::Vec3 corner, anim::Vec3 in, anim::Vec3 out, float top,
                              float floor) {
    const RoutePlanner* planner = brain.planner();
    const float inLength = std::hypot(in.x, in.y);
    const float outLength = std::hypot(out.x, out.y);
    if (planner == nullptr || inLength < 1e-4F || outLength < 1e-4F) {
        return top;
    }
    const float headingIn = human::headingOf(in);
    const float turn = human::wrapAngle(human::headingOf(out) - headingIn);
    if (std::fabs(turn) < 1e-3F) {
        return top;
    }
    const float side = turn > 0.0F ? 1.0F : -1.0F;
    const human::Speeds& speeds = brain.human().speeds();
    for (int trial = 0;; ++trial) {
        const float speed = top - static_cast<float>(trial) * kCornerSpeedStep;
        if (speed <= floor) {
            return floor;
        }
        // The human comes into the corner at `speed` and turns at its gait's rate until it faces the way out: its
        // path is an arc of radius speed / rate, sampled by the radius's band.
        const float rate = human::maxTurn(human::gaitOfSpeed(speed, speeds)) / human::kStepSeconds;
        const float radius = speed / rate;
        const int samples = cornerSamples(radius);
        const float duration = std::fabs(turn) / rate;
        bool stays = true;
        for (int k = 1; k <= samples && stays; ++k) {
            const float t = duration * static_cast<float>(k) / static_cast<float>(samples);
            const float heading = headingIn + side * rate * t;
            // The integral of facing() along the turn: facing(h) = (-sin h, cos h).
            const float scale = speed / (side * rate);
            const anim::Vec3 point{corner.x + scale * (std::cos(heading) - std::cos(headingIn)),
                                   corner.y + scale * (std::sin(heading) - std::sin(headingIn)), corner.z};
            stays = onPolygons(planner->map(), point);
        }
        if (stays) {
            return speed;
        }
    }
}

bool MoveAction::stuck(anim::Vec3 position) {
    if (++m_movingSteps < kStuckSteps) {
        return false;
    }
    const bool covered = planDistance(m_stuckFrom, position) >= kStuckDistance;
    m_movingSteps = 0;
    m_stuckFrom = position;
    return !covered;
}

ActionStatus MoveAction::finish(Brain& brain) {
    brain.stopMove();
    return ActionStatus::Done;
}

} // namespace coney::ai
