// SPDX-License-Identifier: GPL-3.0-or-later
#include "ai/riot_goals.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <numbers>
#include <optional>
#include <vector>

#include "ai/attack_kinds.h"
#include "ai/brain.h"
#include "ai/gangs.h"
#include "ai/move_action.h"
#include "ai/play_anim_action.h"
#include "ai/route_planner.h"
#include "ai/script_services.h"
#include "ai/story_goals.h"
#include "ai/turn_action.h"
#include "human/locomotion.h"

namespace coney::ai {

namespace {

// The distance between two points in plan.
float flatDistance(anim::Vec3 a, anim::Vec3 b) { return std::hypot(a.x - b.x, a.y - b.y); }

// The heading that faces from `from` to `to` (the game's: 0 along +y, turning toward -x).
float headingTo(anim::Vec3 from, anim::Vec3 to) { return std::atan2(-(to.x - from.x), to.y - from.y); }

// The riot's draws: `Random_Int(100)` tops out at 100; under 51 a free act at the start; under 50 half the time.
constexpr int kPercentMax = 100;
constexpr int kFreeActPercent = 51;
constexpr int kHalfPercent = 50;
// The wander vector's start (and reset).
constexpr anim::Vec3 kWanderStart{0.0F, 2.5F, 0.2F};
// The roam's move deadline, ms: a second, or 10 s for a destination toward the player or in the turf.
constexpr std::uint64_t kMoveDeadlineMs = 1000;
constexpr std::uint64_t kFarDeadlineMs = 10000;
// The shout's period, ms.
constexpr int kShoutMinMs = 4000;
constexpr int kShoutMaxMs = 4500;
// Tries for a random point near a centre, and for a point in a turf box.
constexpr int kPointTries = 5;
constexpr int kTurfTries = 3;
// A turf point lies (box radius - 5 m) × a random 0.5-1 from the box's centre.
constexpr float kTurfMargin = 5.0F;
constexpr float kTurfReachMin = 0.5F;
// The threat response a rioter fights with.
constexpr int kFightThreatResponse = 2;
// The thrower returns to its spot at a walk.
constexpr int kThrowerGait = 2;
// A thrower turns until it faces its target within this many radians (**Coney choice**, as the throw goal's).
constexpr float kFaceAngle = 0.35F;
// The thrower looks this share of its sight range for an enemy.
constexpr float kThrowerSightShare = 0.75F;
// Milliseconds in a second, for the wait.
constexpr int kMsPerSecond = 1000;

} // namespace

// ---- RiotGoal ----

namespace {

// `Random_Int(100)`: a whole number from 0 to 100 inclusive.
int randomInt100(Brain& brain) { return rollRange(brain.random(), 0, kPercentMax); }

// A random angle about the vertical, radians.
float randomAngle(Brain& brain) { return brain.random().unit() * 2.0F * std::numbers::pi_v<float>; }

// `vector` turned by the human's heading (the game's: 0 along +y, turning toward -x).
anim::Vec3 turnedBy(anim::Vec3 vector, float heading) {
    const float c = std::cos(heading);
    const float s = std::sin(heading);
    return anim::Vec3{(vector.x * c) - (vector.y * s), (vector.x * s) + (vector.y * c), vector.z};
}

// Whether the brain's human walks from `from` to `to` in a straight line (the planner's test; none takes any line).
bool straightLine(const Brain& brain, anim::Vec3 from, anim::Vec3 to) {
    const RoutePlanner* planner = brain.planner();
    return planner == nullptr || planner->lineClear(from, to);
}

// A random point `range` from `centre` that a straight line from the centre reaches (`0x0029f3c8`, up to 5 tries).
// Coney stand-in: the straight line stands for "on an area, connected to the start", and the point is not dropped to
// the ground.
std::optional<anim::Vec3> randomPointNear(Brain& brain, anim::Vec3 centre, float range) {
    for (int attempt = 0; attempt < kPointTries; ++attempt) {
        const float angle = randomAngle(brain);
        const anim::Vec3 point{centre.x + (range * std::cos(angle)), centre.y + (range * std::sin(angle)), centre.z};
        if (straightLine(brain, centre, point)) {
            return point;
        }
    }
    return std::nullopt;
}

} // namespace

RiotGoal::RiotGoal(const RiotOrder& order, const RiotServices& services)
    : Goal(GoalType::Riot), m_order(order), m_services(&services), m_wander(kWanderStart) {}

void RiotGoal::start(Brain& brain) {
    m_actsLeft = m_order.acts;
    // Init: a draw under 51 starts with a free act, a second under 50 making it a loot.
    if (randomInt100(brain) < kFreeActPercent) {
        m_state = randomInt100(brain) < kHalfPercent ? RiotState::Loot : RiotState::Smash;
    }
}

bool RiotGoal::inTurf(const Brain& brain, anim::Vec3 point) const {
    return !m_services->inTurf || m_services->inTurf(brain, point);
}

GoalStatus RiotGoal::process(Brain& brain) {
    // The nearest player and his distance; nothing without one.
    const std::vector<anim::Vec3> players = m_services->players ? m_services->players() : std::vector<anim::Vec3>{};
    if (players.empty()) {
        return GoalStatus::Stop;
    }
    const anim::Vec3 position = brain.human().position();
    const auto nearest = std::ranges::min_element(
        players, {}, [&position](anim::Vec3 player) { return anim::distance(player, position); });
    const anim::Vec3 player = *nearest;
    const float distance = anim::distance(player, position);
    switch (m_state) {
    case RiotState::Roam: {
        // Decide when the player is outside the gang's turf, or near at a decision update about half the time.
        const bool gate = inTurf(brain, player);
        if (!gate || (distance < m_order.radius && brain.updates() % kRiotDecisionUpdates == 0 &&
                      randomInt100(brain) < kHalfPercent)) {
            decide(brain, gate);
        } else {
            roam(brain, player, distance);
        }
        return GoalStatus::Stop;
    }
    case RiotState::Smash:
    case RiotState::Loot: {
        // Look for a target (found or not), then count the act: a decided act counts down, the free one returns to
        // the roam. With acts left the state stays, so the next update looks again.
        const std::function<bool(Brain&)>& find = m_state == RiotState::Smash ? m_services->smash : m_services->loot;
        if (find) {
            static_cast<void>(find(brain));
        }
        if (!m_decided) {
            m_state = RiotState::Roam;
        } else if (--m_actsLeft <= 0) {
            m_state = RiotState::Leave;
        }
        return GoalStatus::Stop;
    }
    case RiotState::Leave:
        // The exit goal goes over this one; with no exit it tries again next update, and should it end, again.
        if (m_services->leave) {
            static_cast<void>(m_services->leave(brain));
        }
        return GoalStatus::Stop;
    }
    return GoalStatus::Stop;
}

void RiotGoal::decide(Brain& brain, bool inTurf) {
    m_decided = true;
    // One draw: under the fight chance a fight (started, it leaves); under the act chance an act; else it leaves.
    const int draw = randomInt100(brain);
    if (draw < m_order.fightChance && inTurf && tryPickFight(brain)) {
        m_state = RiotState::Leave;
        return;
    }
    if (draw < m_order.actChance && inTurf) {
        m_state = randomInt100(brain) < kHalfPercent ? RiotState::Loot : RiotState::Smash;
        return;
    }
    m_state = RiotState::Leave;
}

void RiotGoal::roam(Brain& brain, anim::Vec3 player, float playerDistance) {
    // The deadlines: the move's rolls over each second (Coney stand-in: human `+0x333` is 0), the shout's every
    // 4-4.5 s (the shout itself is not said).
    const std::uint64_t now = brain.nowMs();
    bool rolled = false;
    if (now >= m_moveDeadlineMs) {
        m_moveDeadlineMs = now + kMoveDeadlineMs;
        rolled = true;
    }
    if (now >= m_shoutAtMs) {
        m_shoutAtMs = now + static_cast<std::uint64_t>(rollRange(brain.random(), kShoutMinMs, kShoutMaxMs));
    }
    if (brain.actionCount() > 0 && !rolled) {
        return;
    }
    // After a failed move: at 30 it leaves; every 5th clears the failure so the next update tries again.
    if (brain.moveFailure() != MoveFailure::None) {
        ++m_failedMoves;
        if (m_failedMoves >= kRiotFailedMovesMax) {
            m_state = RiotState::Leave;
        } else if (m_failedMoves % kRiotRetryEvery == 0) {
            brain.setMoveFailure(MoveFailure::None);
        }
        return;
    }
    const std::optional<anim::Vec3> point = roamPoint(brain, player, playerDistance);
    if (!point) {
        return;
    }
    // Coney stand-in: a running move is replaced (Coney's move action cannot be retargeted).
    if (brain.actionCount() > 0) {
        brain.clearActions();
    }
    brain.queueAction(std::make_unique<MoveAction>(MoveRequest{
        .point = *point, .radius = kRiotArrival, .gait = kRiotGait, .option = false, .delayMs = 0, .flagKind = false}));
}

std::optional<anim::Vec3> RiotGoal::roamPoint(Brain& brain, anim::Vec3 player, float playerDistance) {
    const anim::Vec3 position = brain.human().position();
    ++m_roamCounter;
    // Toward the player, when he is 15 m or more away: a point 15 m from him the rioter reaches in a straight line.
    if (m_roamCounter % kRiotTowardPlayerEvery == 0 && playerDistance >= kRiotTowardPlayerRange) {
        const std::optional<anim::Vec3> point = randomPointNear(brain, player, kRiotTowardPlayerRange);
        if (point && straightLine(brain, position, *point)) {
            m_moveDeadlineMs = brain.nowMs() + kFarDeadlineMs;
            return point;
        }
    }
    // Somewhere in the turf.
    if (m_roamCounter % kRiotTurfEvery == 0) {
        if (const std::optional<anim::Vec3> point = turfPoint(brain)) {
            m_moveDeadlineMs = brain.nowMs() + kFarDeadlineMs;
            return point;
        }
    }
    // The wander, else (the wander vector reset) a random point 5 m away.
    if (const std::optional<anim::Vec3> point = wanderPoint(brain)) {
        return point;
    }
    m_wander = kWanderStart;
    return randomPointNear(brain, position, kRiotFallbackRange);
}

std::optional<anim::Vec3> RiotGoal::turfPoint(Brain& brain) {
    const std::vector<TurfCircle> circles = m_services->turf ? m_services->turf(brain) : std::vector<TurfCircle>{};
    if (circles.empty()) {
        return std::nullopt;
    }
    const TurfCircle& circle =
        circles[static_cast<std::size_t>(rollRange(brain.random(), 0, static_cast<int>(circles.size()) - 1))];
    // Up to 3 tries for a point on an area, at a random angle and (radius - 5) × a random 0.5-1 from the centre.
    const RoutePlanner* planner = brain.planner();
    for (int attempt = 0; attempt < kTurfTries; ++attempt) {
        const float angle = randomAngle(brain);
        const float reach =
            (circle.radius - kTurfMargin) * (kTurfReachMin + ((1.0F - kTurfReachMin) * brain.random().unit()));
        const anim::Vec3 point{circle.centre.x + (reach * std::cos(angle)), circle.centre.y + (reach * std::sin(angle)),
                               circle.centre.z};
        if (planner != nullptr && !planner->startNode(point).has_value()) {
            continue;
        }
        if (inTurf(brain, point) && straightLine(brain, brain.human().position(), point)) {
            return point;
        }
        return std::nullopt;
    }
    return std::nullopt;
}

std::optional<anim::Vec3> RiotGoal::wanderPoint(Brain& brain) {
    // The wander vector nudged by up to 1 m in plan, then scaled back to 2.5 m.
    const anim::Vec3 nudge{(2.0F * brain.random().unit()) - 1.0F, (2.0F * brain.random().unit()) - 1.0F, 0.0F};
    m_wander = anim::add(m_wander, nudge);
    if (const float length = anim::length(m_wander); length > 0.0F) {
        m_wander = anim::scale(m_wander, kRiotWanderRadius / length);
    }
    // 10 m ahead plus the wander circle, turned by the human's heading (Coney stand-in: not dropped to the ground).
    const anim::Vec3 position = brain.human().position();
    const anim::Vec3 ahead =
        turnedBy(anim::add(m_wander, anim::Vec3{0.0F, kRiotWanderAhead, 0.0F}), brain.human().heading());
    const anim::Vec3 point = anim::add(position, ahead);
    if (!inTurf(brain, point) || !straightLine(brain, position, point)) {
        return std::nullopt;
    }
    return point;
}

bool RiotGoal::tryPickFight(Brain& brain) {
    // Only a gang soldier picks fights; one draw says whether a human a player controls may be picked.
    if (brain.type() != BrainType::Gang || !m_services->candidates) {
        return false;
    }
    const bool playersToo = randomInt100(brain) < m_order.playerFightChance;
    const anim::Vec3 position = brain.human().position();
    for (Brain* other : m_services->candidates()) {
        // The first within 15 m that is no ally Warrior, no player's unless the draw allows it, not friendly, not
        // fighting, with nobody in its attack slots, and reached in a straight line. Coney choice: a human out of
        // health is skipped too (the search's own filter is not on the page).
        if (other == &brain || brain.distanceTo(*other) > kRiotFightRange || !Brain::fightable(*other)) {
            continue;
        }
        if (other->type() == BrainType::Warrior || (other->type() == BrainType::Player && !playersToo)) {
            continue;
        }
        if (Gangs::friends(brain.gang(), other->gang()) || other->findGoal(GoalType::Fight) != nullptr ||
            !other->attackSlots().empty() || !straightLine(brain, position, other->human().position())) {
            continue;
        }
        // Threat response 2, then the fight (Coney stand-in: its 8 s deadline is not built, and the taunt is not
        // said).
        brain.setThreatResponse(kFightThreatResponse);
        return brain.fight(*other);
    }
    return false;
}

// ---- StationaryThrowerGoal ----

StationaryThrowerGoal::StationaryThrowerGoal(int delaySeconds, const ThrowerObjects& objects, ScriptServices& services)
    : Goal(GoalType::StationaryThrower), m_delaySeconds(delaySeconds & 0xff), m_objects(objects),
      m_services(&services) {}

void StationaryThrowerGoal::start(Brain& brain) { m_spot = brain.human().position(); }

GoalStatus StationaryThrowerGoal::process(Brain& brain) {
    if (brain.actionCount() > 0) {
        return GoalStatus::Stop;
    }
    switch (m_state) {
    case ThrowerState::Return:
        if (flatDistance(m_spot, brain.human().position()) > kThrowerSpotRadius) {
            brain.queueAction(std::make_unique<MoveAction>(MoveRequest{.point = m_spot,
                                                                       .radius = kThrowerSpotRadius,
                                                                       .gait = kThrowerGait,
                                                                       .option = false,
                                                                       .delayMs = 0,
                                                                       .flagKind = false}));
            return GoalStatus::Stop;
        }
        m_state = ThrowerState::Throw;
        [[fallthrough]];
    case ThrowerState::Throw: {
        brain.stopMove();
        const std::vector<Brain*> candidates = knownBrains(brain);
        Brain* enemy = nearestHostileWithin(brain, candidates, brain.sightRange() * kThrowerSightShare);
        if (enemy == nullptr) {
            return GoalStatus::Stop;
        }
        const anim::Vec3 position = brain.human().position();
        const anim::Vec3 target = enemy->human().position();
        if (std::fabs(human::wrapAngle(headingTo(position, target) - brain.human().heading())) > kFaceAngle) {
            brain.queueAction(TurnAction::toPoint(target));
            return GoalStatus::Stop;
        }
        brain.queueAction(std::make_unique<PlayAnimAction>(*m_services, kThrowAnim, false));
        ++m_throws;
        m_waitUntilMs = brain.nowMs() + static_cast<std::uint64_t>(kMsPerSecond * m_delaySeconds) +
                        static_cast<std::uint64_t>(rollRange(brain.random(), 0, kMsPerSecond));
        m_state = ThrowerState::Wait;
        return GoalStatus::Stop;
    }
    case ThrowerState::Wait:
        if (brain.nowMs() >= m_waitUntilMs) {
            m_state = ThrowerState::Return;
        }
        return GoalStatus::Stop;
    }
    return GoalStatus::Stop;
}

} // namespace coney::ai
