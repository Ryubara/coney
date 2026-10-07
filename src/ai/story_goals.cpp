// SPDX-License-Identifier: GPL-3.0-or-later
#include "ai/story_goals.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <numbers>
#include <utility>
#include <vector>

#include "ai/brain.h"
#include "ai/gangs.h"
#include "ai/move_action.h"
#include "ai/script_services.h"
#include "ai/turn_action.h"
#include "human/locomotion.h"

namespace coney::ai {

namespace {

// Degrees to radians.
float radians(float degrees) { return degrees * std::numbers::pi_v<float> / 180.0F; }

// A placement's position as a vector.
anim::Vec3 positionOf(const world_objects::Placement& placement) {
    return anim::Vec3{placement.position[0], placement.position[1], placement.position[2]};
}

// The distance across the ground between two points.
float flatDistance(anim::Vec3 a, anim::Vec3 b) { return std::hypot(a.x - b.x, a.y - b.y); }

// The search period of the enemy search, ms.
constexpr std::uint64_t kSearchPeriodMs = 1000;
// The throw goal re-aims every this many brain updates while the throw has not happened.
constexpr std::uint64_t kThrowRetryUpdates = 30;
// The dynamic idle's walk: gait 2 until within 1 m.
constexpr int kIdleGait = 2;
constexpr float kIdleRadius = 1.0F;

} // namespace

// ---- MoveToExitFlagGoal ----

MoveToExitFlagGoal::MoveToExitFlagGoal(const MoveToFlagOrder& order, ExitServices services)
    : Goal(GoalType::MoveToExitFlag), m_order(order), m_services(std::move(services)) {}

bool MoveToExitFlagGoal::aim() {
    const std::optional<world_objects::Placement> flag = m_services.flag ? m_services.flag(m_order.flag) : std::nullopt;
    if (!flag) {
        return false;
    }
    m_target = positionOf(*flag);
    // The offset only when the angle or the distance is above 0.
    if (m_order.angleDegrees > 0.0F || m_order.distance > 0.0F) {
        m_target = anim::add(m_target, anim::scale(human::facing(radians(m_order.angleDegrees)), m_order.distance));
    }
    return true;
}

bool MoveToExitFlagGoal::switchExit(const Brain& brain) {
    const std::optional<double> next =
        m_services.nearestExit ? m_services.nearestExit(brain.human().position(), m_order.flag) : std::nullopt;
    if (!next) {
        return false;
    }
    m_order.flag = *next;
    m_order.angleDegrees = 0.0F;
    m_order.distance = 0.0F;
    return aim();
}

void MoveToExitFlagGoal::start(Brain& brain) {
    if (!aim()) {
        m_target = brain.human().position();
    }
    m_nextCheckMs = brain.nowMs() + kExitCheckMs;
    resume(brain);
}

void MoveToExitFlagGoal::resume(Brain& brain) { brain.clearActions(); }

GoalStatus MoveToExitFlagGoal::process(Brain& brain) {
    const std::optional<anim::Vec3> viewer = m_services.viewer ? m_services.viewer() : std::nullopt;
    const anim::Vec3 position = brain.human().position();
    // The check every 8 s: far from every player and out of sight, it is killed there and the goal ends.
    if (brain.nowMs() >= m_nextCheckMs) {
        m_nextCheckMs = brain.nowMs() + kExitCheckMs;
        if (viewer && anim::distance(*viewer, position) > kExitOutOfSight) {
            if (m_services.kill) {
                m_services.kill(brain);
            }
            return GoalStatus::Done;
        }
    }
    if (brain.actionCount() > 0) {
        return GoalStatus::Stop;
    }
    // A blocked way: another exit.
    if (brain.moveFailure() != MoveFailure::None) {
        brain.setMoveFailure(MoveFailure::None);
        if (!switchExit(brain)) {
            return GoalStatus::Done;
        }
    }
    if (flatDistance(m_target, position) <= std::max(m_order.radius, kMoveNothingToDo)) {
        // Arrived: seen, it goes on to another exit; unseen, it leaves the level.
        if (viewer && anim::distance(*viewer, position) <= kExitSeenRange) {
            if (!switchExit(brain)) {
                return GoalStatus::Stop;
            }
        } else {
            if (m_services.remove) {
                m_services.remove(brain);
            }
            return GoalStatus::Done;
        }
    }
    brain.queueAction(std::make_unique<MoveAction>(
        MoveRequest{.point = m_target, .radius = m_order.radius, .gait = m_order.gait, .option = false, .delayMs = 0}));
    return GoalStatus::Stop;
}

// ---- TravelPathGoal ----

TravelPathGoal::TravelPathGoal(std::vector<double> points, int mode, bool reverse, int gait, float radius,
                               FlagServices& services)
    : Goal(GoalType::TravelPath), m_points(std::move(points)), m_mode(mode), m_step(reverse ? -1 : 1), m_gait(gait),
      m_radius(radius), m_services(&services) {}

GoalStatus TravelPathGoal::process(Brain& brain) {
    if (m_points.empty()) {
        return GoalStatus::Done;
    }
    const int count = static_cast<int>(m_points.size());
    if (m_current < 0) {
        m_current = m_step > 0 ? 0 : count - 1;
    } else {
        int next = m_current + m_step;
        if (next < 0 || next >= count) {
            if (m_mode == 1) {
                next = m_step > 0 ? 0 : count - 1;
            } else if (m_mode == 2) {
                m_step = -m_step;
                next = count > 1 ? m_current + m_step : m_current;
            } else {
                return GoalStatus::Done;
            }
        }
        m_current = next;
    }
    static_cast<void>(goalMoveToFlag(
        brain,
        MoveToFlagOrder{.flag = m_points.at(static_cast<std::size_t>(m_current)), .gait = m_gait, .radius = m_radius},
        *m_services));
    return GoalStatus::Stop;
}

// ---- FindEnemyGoal ----

Brain* nearestHostile(Brain& brain, std::span<Brain* const> candidates) {
    return nearestHostileWithin(brain, candidates, brain.sightRange());
}

std::vector<Brain*> knownBrains(Brain& brain) {
    std::vector<Brain*> candidates;
    if (const Gang* own = brain.gang(); own != nullptr) {
        Gangs& gangs = own->owner();
        for (int id = 0; id < static_cast<int>(kGangSlots); ++id) {
            if (const Gang* gang = gangs.find(id); gang != nullptr) {
                candidates.insert(candidates.end(), gang->members().begin(), gang->members().end());
            }
        }
    }
    candidates.insert(candidates.end(), brain.enemies().begin(), brain.enemies().end());
    return candidates;
}

Brain* nearestHostileWithin(Brain& brain, std::span<Brain* const> candidates, float range) {
    Brain* best = nullptr;
    float bestDistance = range;
    for (Brain* other : candidates) {
        if (other == &brain || !Brain::fightable(*other) || other->human().state() != human::TargetState::Standing ||
            !other->human().targetable()) {
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

GoalStatus FindEnemyGoal::process(Brain& brain) {
    if (m_limitMs != 0 && brain.nowMs() >= m_limitMs) {
        return GoalStatus::Done;
    }
    // The target still there: the fight again (this goal is popped and the three pushed anew).
    if (Brain* target = brain.target(); target != nullptr && Brain::fightable(*target) &&
                                        !target->human().outOfWorld() && brain.fight(*target, m_durationMs)) {
        return brain.topGoal() == this ? GoalStatus::Stop : GoalStatus::Again;
    }
    if (!m_searches) {
        return GoalStatus::Done;
    }
    if (brain.nowMs() < m_nextSearchMs) {
        return GoalStatus::Stop;
    }
    m_nextSearchMs = brain.nowMs() + kSearchPeriodMs;
    if (brain.actionCount() == 0) {
        brain.stopMove();
    }
    // The candidates: every brain the gangs know (the members of every gang in use), and player 1's.
    const std::vector<Brain*> candidates = knownBrains(brain);
    if (Brain* enemy = nearestHostile(brain, candidates); enemy != nullptr && brain.fight(*enemy, m_durationMs)) {
        return brain.topGoal() == this ? GoalStatus::Stop : GoalStatus::Again;
    }
    return GoalStatus::Stop;
}

// ---- ThrowObjectGoal ----

ThrowObjectGoal::ThrowObjectGoal(std::function<std::optional<anim::Vec3>(double)> locate, double target, float range,
                                 int gait, std::string callback, ScriptServices* services)
    : Goal(GoalType::ThrowObject), m_locate(std::move(locate)), m_target(target), m_range(range), m_gait(gait),
      m_callback(std::move(callback)), m_services(services) {}

GoalStatus ThrowObjectGoal::process(Brain& brain) {
    human::ScriptState& script = brain.human().script();
    const std::optional<anim::Vec3> target = m_locate ? m_locate(m_target) : std::nullopt;
    if (script.heldObject == 0.0 || !target) {
        return GoalStatus::Done;
    }
    if (brain.actionCount() > 0) {
        return GoalStatus::Stop;
    }
    const anim::Vec3 position = brain.human().position();
    if (flatDistance(*target, position) > m_range) {
        brain.queueAction(std::make_unique<MoveAction>(
            MoveRequest{.point = *target, .radius = m_range, .gait = m_gait, .option = false, .delayMs = 0}));
        return GoalStatus::Stop;
    }
    const float heading = std::atan2(-(target->x - position.x), target->y - position.y);
    if (std::fabs(human::wrapAngle(heading - brain.human().heading())) > kFaceFlagAngle &&
        brain.updates() % kThrowRetryUpdates != 0) {
        brain.queueAction(TurnAction::toPoint(*target));
        return GoalStatus::Stop;
    }
    // The throw: what it holds leaves its hands.
    script.heldObject = 0.0;
    script.heldObjectName.clear();
    m_completed = true;
    return GoalStatus::Done;
}

void ThrowObjectGoal::end(Brain& brain) {
    if (!m_callback.empty() && m_services != nullptr) {
        const std::array<double, 2> args{brain.handle(), m_completed ? 1.0 : 0.0};
        m_services->schedule(m_callback, args, kGoalCallbackDelayMs);
    }
}

// ---- PlayDynIdleGoal ----

PlayDynIdleGoal::PlayDynIdleGoal(double flag, std::vector<std::string> clips, int timeMs, FlagServices& services)
    : Goal(GoalType::PlayDynIdle), m_flag(flag), m_clips(std::move(clips)), m_timeMs(timeMs), m_services(&services) {}

GoalStatus PlayDynIdleGoal::process(Brain& brain) {
    if (brain.actionCount() > 0) {
        return GoalStatus::Stop;
    }
    if (!m_idleSinceMs) {
        const std::optional<world_objects::Placement> flag = m_flag != 0.0 ? m_services->flag(m_flag) : std::nullopt;
        if (flag) {
            const anim::Vec3 spot = positionOf(*flag);
            if (flatDistance(spot, brain.human().position()) > kIdleRadius) {
                brain.queueAction(std::make_unique<MoveAction>(MoveRequest{
                    .point = spot, .radius = kIdleRadius, .gait = kIdleGait, .option = false, .delayMs = 0}));
                return GoalStatus::Stop;
            }
            const float heading = radians(flag->headingDegrees);
            if (std::fabs(human::wrapAngle(heading - brain.human().heading())) > kFaceFlagAngle) {
                brain.queueAction(TurnAction::toHeading(heading));
                return GoalStatus::Stop;
            }
        }
        m_idleSinceMs = brain.nowMs();
    }
    brain.stopMove();
    if (m_timeMs >= 0 && brain.nowMs() >= *m_idleSinceMs + static_cast<std::uint64_t>(m_timeMs)) {
        return GoalStatus::Done;
    }
    return GoalStatus::Stop;
}

} // namespace coney::ai
