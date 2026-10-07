// SPDX-License-Identifier: GPL-3.0-or-later
#include "ai/chase_goals.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <memory>
#include <numbers>
#include <utility>

#include "ai/brain.h"
#include "ai/gangs.h"
#include "ai/move_action.h"
#include "ai/move_to_flag_goal.h"
#include "ai/script_services.h"
#include "ai/turn_action.h"
#include "human/locomotion.h"

namespace coney::ai {

namespace {

// Gaits by the original's ids.
constexpr int kWalkGait = 2;
constexpr int kRunGait = 4;
constexpr int kSprintGait = 5;
// A path point counts as reached within this many metres (**Coney choice**: the path goals' arrival is the move's).
constexpr float kPointReached = 1.0F;
// The lead-chase runner's sprint: on above this share of its stamina, off at or below the lower one.
constexpr float kSprintOnShare = 0.6F;
constexpr float kSprintOffShare = 0.2F;
// A chaser more than this off the runner's facing (radians, 15°) is turned to.
constexpr float kFaceAngle = 0.2618F;
// The ledge thrower's walk back.
constexpr float kReturnRadius = 0.5F;

// Degrees to radians.
float radians(float degrees) { return degrees * std::numbers::pi_v<float> / 180.0F; }

// The distance across the ground between two points.
float flatDistance(anim::Vec3 a, anim::Vec3 b) { return std::hypot(a.x - b.x, a.y - b.y); }

// The heading (radians, 0 facing +y) from one point to another.
float headingTo(anim::Vec3 from, anim::Vec3 to) { return std::atan2(-(to.x - from.x), to.y - from.y); }

// A placement's position as a vector.
anim::Vec3 positionOf(const world_objects::Placement& placement) {
    return anim::Vec3{placement.position[0], placement.position[1], placement.position[2]};
}

// Every brain `gangs` know (the members of every gang in use) and the brain's own enemies.
std::vector<Brain*> knownBrains(Brain& brain, const Gangs& gangs) {
    std::vector<Brain*> all;
    for (int id = 0; id < static_cast<int>(kGangSlots); ++id) {
        if (const Gang* gang = gangs.find(id); gang != nullptr) {
            all.insert(all.end(), gang->members().begin(), gang->members().end());
        }
    }
    all.insert(all.end(), brain.enemies().begin(), brain.enemies().end());
    return all;
}

// The nearest living enemy of `brain` within `range` metres, or null.
Brain* nearestLivingEnemy(Brain& brain, const Gangs& gangs, float range) {
    Brain* best = nullptr;
    float bestDistance = range;
    for (Brain* other : knownBrains(brain, gangs)) {
        if (other == &brain || !Brain::fightable(*other)) {
            continue;
        }
        const bool listed = std::ranges::find(brain.enemies(), other) != brain.enemies().end();
        if (!listed && (Gangs::friends(brain.gang(), other->gang()) || !Gangs::enemies(brain.gang(), other->gang()))) {
            continue;
        }
        if (const float distance = brain.distanceTo(*other); distance <= bestDistance) {
            best = other;
            bestDistance = distance;
        }
    }
    return best;
}

// Where `p` projects on the line through `a` and `b`, as a share of the segment (0 at `a`, 1 at `b`).
float shareAlong(anim::Vec3 a, anim::Vec3 b, anim::Vec3 p) {
    const anim::Vec3 ab = anim::subtract(b, a);
    const float squared = anim::dot(ab, ab);
    return squared > 0.0F ? anim::dot(anim::subtract(p, a), ab) / squared : 0.0F;
}

// The distance from `p` to the segment from `a` to `b`.
float segmentDistance(anim::Vec3 a, anim::Vec3 b, anim::Vec3 p) {
    const float t = std::clamp(shareAlong(a, b, p), 0.0F, 1.0F);
    return anim::distance(anim::add(a, anim::scale(anim::subtract(b, a), t)), p);
}

// The segment a position is on (0x002e19b0): walking the segments from the first, the one before the first whose
// distance to the position grows, else the last.
std::size_t segmentOf(const std::vector<anim::Vec3>& path, anim::Vec3 p) {
    const std::size_t last = path.size() - 2;
    float previous = segmentDistance(path.at(0), path.at(1), p);
    for (std::size_t i = 1; i <= last; ++i) {
        const float now = segmentDistance(path.at(i), path.at(i + 1), p);
        if (now > previous) {
            return i - 1;
        }
        previous = now;
    }
    return last;
}

} // namespace

// ---- GuardFlagGoal ----

GuardFlagGoal::GuardFlagGoal(double flag, float radius, int headingDegrees, std::string callback, int timeMs,
                             FlagServices& services, ScriptServices* scripts)
    : Goal(GoalType::GuardFlag), m_flag(flag), m_radius(radius), m_heading(headingDegrees),
      m_callback(std::move(callback)), m_timeMs(timeMs), m_services(&services), m_scripts(scripts) {}

GoalStatus GuardFlagGoal::process(Brain& brain) {
    if (brain.actionCount() > 0) {
        return GoalStatus::Stop;
    }
    const std::optional<world_objects::Placement> flag = m_services->flag(m_flag);
    if (!flag) {
        m_completed = true;
        return GoalStatus::Done;
    }
    // Strayed: back to the flag at a walk.
    if (flatDistance(positionOf(*flag), brain.human().position()) > m_radius) {
        static_cast<void>(
            goalMoveToFlag(brain, MoveToFlagOrder{.flag = m_flag, .gait = kWalkGait, .radius = m_radius}, *m_services));
        return GoalStatus::Stop;
    }
    // On station: the heading, now and then.
    if (m_heading >= 0 && brain.updates() % kGuardFaceUpdates == 0) {
        const float heading = radians(static_cast<float>(m_heading));
        if (std::fabs(human::wrapAngle(heading - brain.human().heading())) > kFaceAngle) {
            brain.queueAction(TurnAction::toHeading(heading));
        }
    }
    return GoalStatus::Stop;
}

void GuardFlagGoal::end(Brain& brain) {
    if (!m_callback.empty() && m_scripts != nullptr) {
        const std::array<double, 2> args{brain.handle(), m_completed ? 1.0 : 0.0};
        m_scripts->schedule(m_callback, args, kGoalCallbackDelayMs);
    }
}

// ---- LeadChaseGoal ----

LeadChaseGoal::LeadChaseGoal(std::vector<double> points, float waitDistance, FlagServices& services, const Gangs& gangs)
    : Goal(GoalType::LeadChase), m_points(std::move(points)), m_waitDistance(waitDistance), m_services(&services),
      m_gangs(&gangs) {}

void LeadChaseGoal::start(Brain& brain) { brain.setThreatResponse(0); }

GoalStatus LeadChaseGoal::process(Brain& brain) {
    // The chaser, chosen again every few updates.
    if (m_chaser == nullptr || brain.updates() % kLeadChaseChooseUpdates == 0) {
        m_chaser = nearestLivingEnemy(brain, *m_gangs, kLeadChaseRange);
    }
    // The next point it can find; past the last one the chase is over.
    const anim::Vec3 position = brain.human().position();
    std::optional<anim::Vec3> point;
    while (m_current < m_points.size()) {
        if (const auto flag = m_services->flag(m_points.at(m_current)); flag) {
            point = positionOf(*flag);
            if (flatDistance(*point, position) > kPointReached) {
                break;
            }
        }
        point.reset();
        ++m_current;
    }
    if (!point) {
        brain.stopMove();
        return GoalStatus::Done;
    }
    // The chaser near enough: run on, sprinting while the stamina lasts.
    if (m_chaser != nullptr && brain.distanceTo(*m_chaser) <= m_waitDistance) {
        const human::Stamina& stamina = brain.human().stamina();
        const float share =
            stamina.maximum() > 0 ? static_cast<float>(stamina.value()) / static_cast<float>(stamina.maximum()) : 0.0F;
        if (m_sprinting && share <= kSprintOffShare) {
            m_sprinting = false;
        } else if (!m_sprinting && share > kSprintOnShare) {
            m_sprinting = true;
        }
        brain.setMove(anim::subtract(*point, position),
                      gaitSpeed(brain.human().speeds(), m_sprinting ? kSprintGait : kRunGait));
        return GoalStatus::Stop;
    }
    // Too far behind: wait, facing the chaser.
    brain.stopMove();
    if (m_chaser != nullptr) {
        const float heading = headingTo(position, m_chaser->human().position());
        if (std::fabs(human::wrapAngle(heading - brain.human().heading())) > kFaceAngle) {
            brain.setMoveHeading(heading, 0.0F);
        }
    }
    return GoalStatus::Stop;
}

// ---- DevilRunGoal ----

DevilRunGoal::DevilRunGoal(std::vector<double> points, const DevilRunOrder& order, FlagServices& services,
                           const Gangs& gangs)
    : Goal(GoalType::DevilRun), m_points(std::move(points)), m_order(order), m_services(&services), m_gangs(&gangs) {}

std::vector<anim::Vec3> DevilRunGoal::points() const {
    std::vector<anim::Vec3> found;
    for (const double point : m_points) {
        if (const auto flag = m_services->flag(point); flag) {
            found.push_back(positionOf(*flag));
        }
    }
    return found;
}

void DevilRunGoal::start(Brain& brain) {
    m_savedThreat = brain.threatResponse();
    m_savedFieldOfView = brain.fieldOfView();
    m_speed = gaitSpeed(brain.human().speeds(), m_order.gait);
    brain.setSight(brain.sightRange(), 2.0F * std::numbers::pi_v<float>);
    resume(brain);
}

void DevilRunGoal::resume(Brain& brain) {
    brain.setThreatResponse(0);
    m_count = kDevilResumeCount;
}

void DevilRunGoal::end(Brain& brain) {
    brain.setThreatResponse(m_savedThreat);
    brain.setSight(brain.sightRange(), m_savedFieldOfView);
}

bool DevilRunGoal::pace(Brain& brain, const std::vector<anim::Vec3>& path) {
    const anim::Vec3 runner = brain.human().position();
    const anim::Vec3 chaser = m_chaser->human().position();
    // The distance: along the hindmost segment when the runner is on it (0 once the chaser is past it), else straight.
    float distance = anim::distance(runner, chaser);
    if (m_hindmost && path.size() >= 2 && segmentOf(path, runner) == *m_hindmost) {
        const anim::Vec3 a = path.at(*m_hindmost);
        const anim::Vec3 b = path.at(*m_hindmost + 1);
        const float runnerShare = shareAlong(a, b, runner);
        const float chaserShare = shareAlong(a, b, chaser);
        const bool past = m_order.hostile ? chaserShare < runnerShare : chaserShare > runnerShare;
        distance = past ? 0.0F : anim::distance(runner, anim::add(a, anim::scale(anim::subtract(b, a), chaserShare)));
    }
    // A hostile runner attacks a running chaser within the attack distance, or a slower one within its reach (a
    // squared length against metres, as the original compares them).
    if (m_order.hostile) {
        const bool running = static_cast<int>(m_chaser->human().gait()) > kWalkGait;
        const float reach = 1.1F * brain.meleeFar();
        if ((running && distance <= m_order.attackDistance) || (!running && distance <= reach * reach)) {
            brain.setThreatResponse(2);
            static_cast<void>(brain.fight(*m_chaser));
            return true;
        }
    }
    // The blend and the speed, given to a running move at once.
    const float share = m_order.paceDistance > 0.0F ? distance / m_order.paceDistance : 1.0F;
    const float blend = m_order.hostile ? std::min(share, 1.0F) : std::max(1.0F - share, 0.0F);
    const float slowest = gaitSpeed(brain.human().speeds(), m_order.gait);
    m_speed = slowest + ((m_order.maxSpeed - slowest) * blend);
    if (auto* move = dynamic_cast<MoveAction*>(brain.frontAction()); move != nullptr) {
        move->setSpeed(m_speed);
    }
    return false;
}

GoalStatus DevilRunGoal::process(Brain& brain) {
    ++m_count;
    const std::vector<anim::Vec3> path = points();
    const Gang* gang = m_gangs->find(m_order.gang);
    if (path.size() < 2 || gang == nullptr) {
        return GoalStatus::Done;
    }
    // The hindmost segment of the gang's members; none left: the run is over.
    if (!m_hindmost || m_count % kDevilHindmostUpdates == 0) {
        if (gang->members().empty()) {
            return GoalStatus::Done;
        }
        std::size_t hindmost = path.size() - 2;
        for (const Brain* member : gang->members()) {
            hindmost = std::min(hindmost, segmentOf(path, member->human().position()));
        }
        m_hindmost = hindmost;
    }
    // The chaser: the live member farthest back along the hindmost segment; none: the run is over.
    const bool chaserGone = m_chaser != nullptr && m_order.hostile && !Brain::fightable(*m_chaser);
    if (m_chaser == nullptr || chaserGone || m_count % kDevilChooseUpdates == 0) {
        m_chaser = nullptr;
        float back = std::numeric_limits<float>::max();
        const anim::Vec3 a = path.at(*m_hindmost);
        const anim::Vec3 b = path.at(*m_hindmost + 1);
        for (Brain* member : gang->members()) {
            if (member == &brain || !Brain::fightable(*member)) {
                continue;
            }
            if (const float share = shareAlong(a, b, member->human().position()); share < back) {
                back = share;
                m_chaser = member;
            }
        }
        if (m_chaser == nullptr) {
            return GoalStatus::Done;
        }
    }
    if (m_count % kDevilPaceUpdates == 0 && pace(brain, path)) {
        return GoalStatus::Stop;
    }
    // With nothing queued: on for the path's last point at the current speed, at least a walk's.
    if (brain.actionCount() == 0) {
        auto move = std::make_unique<MoveAction>(MoveRequest{.point = path.back(),
                                                             .radius = kDevilArrival,
                                                             .gait = kRunGait,
                                                             .option = false,
                                                             .delayMs = 0,
                                                             .flagKind = false});
        move->setSpeed(std::max(m_speed, gaitSpeed(brain.human().speeds(), kWalkGait)));
        brain.queueAction(std::move(move));
    }
    return GoalStatus::Stop;
}

// ---- BigLedgeThrowerGoal ----

BigLedgeThrowerGoal::BigLedgeThrowerGoal(const LedgeThrowerOrder& order, FlagServices& services, const Gangs& gangs)
    : Goal(GoalType::BigLedgeThrower), m_order(order), m_services(&services), m_gangs(&gangs) {}

void BigLedgeThrowerGoal::start(Brain& brain) {
    m_spot = brain.human().position();
    m_state = m_order.taunt ? State::Taunt : State::PickUp;
}

GoalStatus BigLedgeThrowerGoal::process(Brain& brain) {
    if (brain.actionCount() > 0) {
        return GoalStatus::Stop;
    }
    // Off its spot: back to it first.
    if (flatDistance(brain.human().position(), m_spot) > kLedgeReturnDistance) {
        m_state = State::Return;
        brain.queueAction(std::make_unique<MoveAction>(MoveRequest{.point = m_spot,
                                                                   .radius = kReturnRadius,
                                                                   .gait = kWalkGait,
                                                                   .option = false,
                                                                   .delayMs = 0,
                                                                   .flagKind = false}));
        return GoalStatus::Stop;
    }
    switch (m_state) {
    case State::Taunt:
    case State::Return:
    case State::PickUp:
        m_state = State::Throw;
        break;
    case State::Throw: {
        // At the nearest enemy, or failing one a target flag (a random one of those that exist).
        std::optional<anim::Vec3> at;
        if (const Brain* enemy = nearestLivingEnemy(brain, *m_gangs, brain.sightRange()); enemy != nullptr) {
            at = enemy->human().position();
        } else {
            std::vector<anim::Vec3> flags;
            for (const double target : m_order.targets) {
                if (const auto flag = target != 0.0 ? m_services->flag(target) : std::nullopt; flag) {
                    flags.push_back(positionOf(*flag));
                }
            }
            if (!flags.empty()) {
                at = flags.at(static_cast<std::size_t>(brain.rand100()) % flags.size());
            }
        }
        if (at) {
            brain.queueAction(TurnAction::toPoint(*at));
        }
        ++m_throws;
        m_waitUntilMs = brain.nowMs() + m_order.delayMs;
        m_state = State::Pause;
        break;
    }
    case State::Pause:
        if (brain.nowMs() < m_waitUntilMs) {
            break;
        }
        // A round over: it turns on the nearest enemy before the next.
        if (m_throws >= m_order.cycles) {
            m_throws = 0;
            if (const Brain* enemy = nearestLivingEnemy(brain, *m_gangs, brain.sightRange()); enemy != nullptr) {
                brain.queueAction(TurnAction::toPoint(enemy->human().position()));
            }
        }
        m_state = State::PickUp;
        break;
    }
    return GoalStatus::Stop;
}

} // namespace coney::ai
