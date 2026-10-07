// SPDX-License-Identifier: GPL-3.0-or-later
#include "ai/move_action.h"

#include <algorithm>
#include <cmath>
#include <expected>
#include <utility>

#include "ai/brain.h"
#include "ai/steering.h"
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

// The updates in a second: one update's travel is the speed over this (the arrival test's lead).
constexpr float kUpdatesPerSecond = 30.0F;

// Whether a point lies on a walkable polygon.
bool onPolygons(const world::PathMap& map, anim::Vec3 point) { return map.polygonAt(point.x, point.y).has_value(); }

} // namespace

float gaitSpeed(const human::Speeds& speeds, int gait) {
    switch (gait) {
    case 1:
        return speeds.sneak;
    case 2:
        return speeds.walk;
    case 3:
        return speeds.jog;
    case 4:
        return speeds.run;
    case 5:
        return speeds.sprint;
    default:
        return 0.0F; // gait 0 stands, and the original gives 0 for any other value too
    }
}

ActionStatus MoveAction::start(Brain& brain) {
    const anim::Vec3 position = brain.human().position();
    m_landings = brain.human().landings();
    if (planDistance(position, m_request.point) <= kMoveNothingToDo) {
        return finish(brain);
    }
    brain.setMoveFailure(MoveFailure::None);
    resetAvoidance(brain);
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
        m_follower.emplace(*planner, std::move(*route), position, m_request.point);
    }
    return ActionStatus::Running;
}

ActionStatus MoveAction::update(Brain& brain) {
    human::Human& human = brain.human();
    // 1. Wait while the human is busy (Human_IsBusy) or climbing: the move stays as it was. A climb over a route's
    // leg that has just ended leaves the human beyond the leg's waypoint: the follower moves on.
    const bool climbing = human.traversal() == human::Traversal::Climbing;
    if (m_wasClimbing && !climbing && m_follower && brain.planner() != nullptr) {
        m_follower->passWaypoint(*brain.planner(), human.position());
        m_stuckFrom = human.position();
        m_movingSteps = 0;
    }
    m_wasClimbing = climbing;
    // A landing since the last update (docs/research/ai.md#route-jump) keeps the route or ends the move. **Coney
    // stand-in**: the brain does not run in the air, so the arrival test the original runs there is judged at the
    // landing: the landing waypoint counts as reached within its radius plus one update's travel at the launch speed.
    if (human.landings() != m_landings) {
        m_landings = human.landings();
        if (m_follower && m_follower->onJumpLeg() && m_jump != JumpState::None) {
            m_airArrived =
                planDistance(human.position(), m_follower->ahead().front()) - (m_launchAcross / kUpdatesPerSecond) <=
                kWaypointRadius;
        }
        if (const std::optional<ActionStatus> status = landed(brain, human.position())) {
            return *status;
        }
    }
    if (climbing || human::stickBusy(human.gateInput())) {
        return ActionStatus::Running;
    }
    ++m_updates;
    const anim::Vec3 position = human.position();
    // The move's speed this update: its gait's, or the steering's override while it runs (set on an earlier update).
    const float moveSpeed = brain.steering().speedOverride().value_or(gaitSpeed(human.speeds(), m_request.gait));
    // 2. Now and then, a route whose point has come into a straight line is dropped.
    RoutePlanner* planner = brain.planner();
    // Not near a jump leg, which the straight line would cut across.
    if (m_follower && planner != nullptr && (m_updates + brain.slot()) % kStraightCheckSteps == 0 &&
        !m_follower->atJump() && planner->lineClear(position, m_request.point)) {
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
        aim = m_follower->waypoint(*planner, position, human.speed());
        aimRadius = m_follower->onLastLeg() ? m_request.radius : kWaypointRadius;
    }
    brain.setRouteNode(m_follower ? m_follower->currentNode() : std::nullopt);
    brain.setMoveAim(aim, aimRadius);
    if (const std::optional<ActionStatus> leg = followLeg(brain, position, aim)) {
        return *leg;
    }
    // 6. The steering round other humans may bend the aim (arriving within kSteerAimRadius) or set a speed for the
    // updates to come. Never on a charge leg: the original's follower returns before the steering there (Coney walks
    // the charge, which is not built).
    const bool chargeLeg = m_follower && !m_follower->onLastLeg() && (m_follower->legKind() & edge_flag::kCharge) != 0;
    if (const std::optional<anim::Vec3> steered =
            chargeLeg ? std::nullopt
                      : steerAroundHumans(brain, SteerRequest{.moveSpeed = moveSpeed,
                                                              .aim = aim,
                                                              .destination = m_request.point,
                                                              .arrivalRadius = m_request.radius})) {
        aim = *steered;
        brain.setMoveAim(aim, kSteerAimRadius);
    }
    const anim::Vec3 way = anim::subtract(aim, position);
    const float heading = std::hypot(way.x, way.y) > 1e-4F ? human::headingOf(way) : human.heading();
    // 8. Standing far off the way, turn on the spot first; else go at the corner speed.
    if (human.speed() < kStandingSpeed && std::fabs(human::wrapAngle(heading - human.heading())) > kTurnOnSpotAngle) {
        brain.setMoveHeading(heading, 0.0F);
        m_movingSteps = 0;
        m_stuckFrom = position;
        return ActionStatus::Running;
    }
    brain.setMoveHeading(heading, cornerSpeed(brain, position, moveSpeed));
    // 9. Stuck.
    if (stuck(position)) {
        brain.setMoveFailure(MoveFailure::Stuck);
        return finish(brain);
    }
    return ActionStatus::Running;
}

std::optional<ActionStatus> MoveAction::followLeg(Brain& brain, anim::Vec3 position, anim::Vec3 aim) {
    if (!m_follower || m_follower->onLastLeg()) {
        return std::nullopt;
    }
    const std::uint16_t kind = m_follower->legKind();
    // A jump leg: dropped off or jumped, whatever its avoid bit.
    if (m_follower->onJumpLeg()) {
        return jumpLeg(brain, position, aim);
    }
    // A leg whose link is avoided is refused (a closed door), but for a charge, which is not built: walked.
    if (m_follower->legAvoided() && (kind & edge_flag::kCharge) == 0) {
        brain.setMoveFailure((kind & edge_flag::kDoor) != 0 ? MoveFailure::EdgeTen : MoveFailure::Edge);
        return finish(brain);
    }
    if ((kind & (edge_flag::kClimb | edge_flag::kJump)) == 0) {
        return std::nullopt;
    }
    // A climb: run at the waypoint (gait 4) and try the climb toward it each update; give up after kClimbTries.
    if (m_follower->index() != m_climbIndex) {
        m_climbIndex = m_follower->index();
        m_climbFails = 0;
    }
    if (++m_climbFails > kClimbTries) {
        brain.setMoveFailure(MoveFailure::Stuck);
        return finish(brain);
    }
    const anim::Vec3 way = anim::subtract(aim, position);
    const float heading = std::hypot(way.x, way.y) > 1e-4F ? human::headingOf(way) : brain.human().heading();
    brain.setMoveHeading(heading, gaitSpeed(brain.human().speeds(), 4));
    brain.requestClimb(anim::Vec3{way.x, way.y, 0.0F});
    m_stuckFrom = position;
    m_movingSteps = 0;
    return ActionStatus::Running;
}

bool MoveAction::abort(Brain& brain) {
    brain.stopMove();
    brain.setRouteNode(std::nullopt);
    return true;
}

float MoveAction::cornerSpeed(Brain& brain, anim::Vec3 position, float top) {
    const human::Speeds& speeds = brain.human().speeds();
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
        // The human comes into the corner at `speed` and turns at its gait's AI rate (with the brain's turn boost)
        // until it faces the way out: its path is an arc of radius speed / rate, sampled by the radius's band.
        const float rate =
            human::aiMaxTurn(human::gaitOfSpeed(speed, speeds), brain.turnBoost(), brain.human().script().wounded) /
            human::kStepSeconds;
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
    brain.setRouteNode(std::nullopt);
    return ActionStatus::Done;
}

ActionStatus MoveAction::jumpLeg(Brain& brain, anim::Vec3 position, anim::Vec3 aim) {
    // A new leg starts with nothing done.
    if (m_follower->index() != m_jumpIndex) {
        m_jumpIndex = m_follower->index();
        m_jump = JumpState::None;
        m_airArrived = false;
    }
    // A jump made waits for its landing.
    if (m_jump == JumpState::Jumped) {
        return ActionStatus::Running;
    }
    // 1. Aim at the landing point and turn the body to it at once.
    human::Human& human = brain.human();
    human.face(aim);
    const anim::Vec3 way = anim::subtract(aim, position);
    const float across = std::hypot(way.x, way.y);
    m_stuckFrom = position;
    m_movingSteps = 0;
    // 2. A drop: the point lower, the leg not avoided and the speed that lands on it under the limit; set again each
    // update until the human leaves the ground.
    if (!m_follower->legAvoided()) {
        const float fall = largerRoot(kHalfGravity, 0.0F, way.z);
        if (fall > 0.0F && across / fall < kJumpMaxAcross) {
            brain.setMoveHeading(human.heading(), across / fall);
            m_launchAcross = across / fall;
            m_jump = JumpState::Drop;
            return ActionStatus::Running;
        }
    }
    // Otherwise a jump whose arc ends on the point; refused, the move ends and is planned again.
    const std::optional<anim::Vec3> velocity = routeJumpVelocity(way);
    if (!velocity || !human.launchJump(*velocity)) {
        return finish(brain);
    }
    m_launchAcross = std::hypot(velocity->x, velocity->y);
    brain.stopMove();
    m_jump = JumpState::Jumped;
    return ActionStatus::Running;
}

std::optional<ActionStatus> MoveAction::landed(Brain& brain, anim::Vec3 position) {
    // Near the jump leg's waypoint after a drop or a jump the route goes on: past the waypoint when the arc reached
    // it, else the leg's handler runs again from here.
    if (m_follower && brain.planner() != nullptr && m_jump != JumpState::None && m_follower->onJumpLeg() &&
        anim::distance(position, m_follower->ahead().front()) <= kLandingKeepsRoute) {
        if (m_airArrived) {
            m_follower->passWaypoint(*brain.planner(), position);
        }
        m_jump = JumpState::None;
        m_airArrived = false;
        m_stuckFrom = position;
        m_movingSteps = 0;
        return std::nullopt;
    }
    // Any other landing, a fall off an edge too: the move ends, and its goal plans again from here.
    return finish(brain);
}

float largerRoot(float a, float b, float c) {
    if (std::fabs(a) < 1e-12F) {
        return std::fabs(b) < 1e-12F ? 0.0F : -c / b;
    }
    const float discriminant = (b * b) - (4.0F * a * c);
    if (discriminant < 0.0F) {
        return 0.0F;
    }
    const float root = std::sqrt(discriminant);
    return std::max((-b + root) / (2.0F * a), (-b - root) / (2.0F * a));
}

std::optional<anim::Vec3> routeJumpVelocity(anim::Vec3 way) {
    const float across = std::hypot(way.x, way.y);
    const anim::Vec3 unit = across > 1e-6F ? anim::Vec3{way.x / across, way.y / across, 0.0F} : anim::Vec3{};
    // The lowest vertical speed whose arc comes down to the point's height slowly enough across; the last tried is
    // the first past kJumpLastUp.
    for (float up = kJumpFirstUp;; up += kJumpUpStep) {
        const float time = largerRoot(-kHalfGravity, up, -way.z);
        if (time > 0.0F && across / time <= kJumpMaxAcross) {
            const float speed = across / time;
            return anim::Vec3{unit.x * speed, unit.y * speed, up};
        }
        if (up > kJumpLastUp) {
            return std::nullopt;
        }
    }
}

} // namespace coney::ai
