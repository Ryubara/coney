// SPDX-License-Identifier: GPL-3.0-or-later
#include "ai/melee_goal.h"

#include <limits>
#include <memory>

#include "ai/brain.h"
#include "ai/engage_goals.h"
#include "ai/fight_goal.h"
#include "ai/move_action.h"
#include "ai/move_to_human_action.h"
#include "ai/route_planner.h"
#include "human/human.h"

namespace coney::ai {

namespace {

// Beyond this distance a target the straight line does not reach is walked to before the fight goal, m.
constexpr float kWalkOverDistance = 1.0F;
// The walk over: its gait (walk, in the fight stance) and arrival radius.
constexpr int kWalkOverGait = 2;
constexpr float kWalkOverRadius = 1.0F;
// The stand-in run of a human not allowed to approach, ms.
constexpr std::uint32_t kStraightRunMs = 2000;

// Whether `other` can be fought now: health left and in the world (the original's valid-target test `0x0028d4b0`).
bool valid(const Brain& other) { return Brain::fightable(other) && !other.human().outOfWorld(); }

// The target to fight: the current one while valid and listed, else the nearest valid enemy (**stand-in** for the
// enemies' scores); null when there is none.
Brain* chooseTarget(Brain& brain) {
    Brain* current = brain.target();
    Brain* nearest = nullptr;
    float best = std::numeric_limits<float>::max();
    for (Brain* enemy : brain.enemies()) {
        if (!valid(*enemy)) {
            continue;
        }
        if (enemy == current) {
            return current;
        }
        if (const float distance = brain.distanceTo(*enemy); distance < best) {
            best = distance;
            nearest = enemy;
        }
    }
    return nearest;
}

} // namespace

void MeleeGoal::start(Brain& brain) {
    m_limitMs = m_durationMs < 0 ? 0 : brain.nowMs() + static_cast<std::uint64_t>(m_durationMs);
}

GoalStatus MeleeGoal::process(Brain& brain) {
    // 1. The time limit, then the actions.
    if (m_limitMs != 0 && brain.nowMs() >= m_limitMs) {
        return GoalStatus::Done;
    }
    if (brain.actionCount() > 0) {
        return GoalStatus::Stop;
    }
    // 2. The target.
    Brain* target = nullptr;
    if (brain.threatResponse() != 0) {
        target = chooseTarget(brain);
        if (target != nullptr) {
            brain.setTarget(target);
        }
    } else {
        target = brain.target();
        if (target == nullptr || !valid(*target) || !brain.hasAttackSlot()) {
            brain.setTarget(nullptr);
            return GoalStatus::Done;
        }
    }
    // 3. None to fight.
    if (target == nullptr) {
        return GoalStatus::Done;
    }
    // 4. With a target: a new one may be approached; a failed move stops the approach.
    if (target != m_lastTarget) {
        m_lastTarget = target;
        brain.setMayApproach(true);
    }
    if (brain.moveFailure() != MoveFailure::None) {
        brain.setMayApproach(false);
    }
    const float distance = brain.distanceTo(*target);
    const float range = brain.meleeFar() * kFightRangeScale;
    const bool armed = brain.human().fighter().animSet() != 0;
    const anim::Vec3 at = target->human().position();
    if (armed || (brain.mayApproach() && distance <= range)) {
        // Over to him in the fight stance when the straight line does not reach him, else the fight.
        const RoutePlanner* planner = brain.planner();
        if (distance > kWalkOverDistance && planner != nullptr && !planner->lineClear(brain.human().position(), at)) {
            brain.queueAction(std::make_unique<MoveAction>(
                MoveRequest{.point = at, .radius = kWalkOverRadius, .gait = kWalkOverGait}));
            return GoalStatus::Stop;
        }
        brain.pushGoal(std::make_unique<FightGoal>(kMeleeFightMs));
        return GoalStatus::Again;
    }
    if (brain.mayApproach()) {
        brain.pushGoal(std::make_unique<EngageEnemyGoal>());
        return GoalStatus::Again;
    }
    // Not allowed to approach (stand-in): straight at him, and the approach allowed again.
    brain.setMoveFailure(MoveFailure::None);
    brain.setMayApproach(true);
    brain.queueAction(std::make_unique<MoveToHumanAction>(kStraightRunMs, kInReachShare * brain.meleeFar()));
    return GoalStatus::Stop;
}

void MeleeGoal::end(Brain& brain) { brain.setMayApproach(true); }

} // namespace coney::ai
