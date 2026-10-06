// SPDX-License-Identifier: GPL-3.0-or-later
#include "ai/engage_goals.h"

#include <memory>

#include "ai/brain.h"
#include "ai/fight_goal.h"
#include "ai/move_action.h"
#include "ai/move_to_human_action.h"
#include "ai/script_services.h"
#include "animation/anim_math.h"
#include "human/human.h"

namespace coney::ai {

namespace {

// Whether `brain`'s human is down: out of health or on the ground.
bool down(const Brain& brain) {
    return brain.human().fighter().health().depleted() || brain.human().state() == human::TargetState::Grounded;
}

} // namespace

GoalStatus MoveToHumanGoal::process(Brain& brain) {
    const Brain* target = m_services->brain(m_target);
    if (down(brain) || target == nullptr || target->human().outOfWorld() || !target->human().alive()) {
        return GoalStatus::Done;
    }
    if (anim::distance(brain.human().position(), target->human().position()) <= m_radius) {
        brain.clearActions();
        return GoalStatus::Done;
    }
    // A failed route is waited out before the next try.
    if (brain.moveFailure() != MoveFailure::None) {
        if (++m_failedUpdates < kMoveToHumanRetryUpdates) {
            return GoalStatus::Stop;
        }
        brain.setMoveFailure(MoveFailure::None);
        m_failedUpdates = 0;
        m_nextMoveMs = 0;
    }
    if (brain.nowMs() >= m_nextMoveMs || brain.actionCount() == 0) {
        brain.clearActions();
        brain.queueAction(std::make_unique<MoveAction>(MoveRequest{.point = target->human().position(),
                                                                   .radius = m_radius,
                                                                   .gait = m_gait,
                                                                   .option = false,
                                                                   .delayMs = 0,
                                                                   .flagKind = false}));
        m_nextMoveMs = brain.nowMs() + kMoveToHumanReissueMs;
    }
    return GoalStatus::Stop;
}

GoalStatus EngageEnemyGoal::process(Brain& brain) {
    Brain* enemy = m_services->brain(m_enemy);
    if (enemy == nullptr || enemy == &brain || enemy->human().outOfWorld() || !Brain::fightable(*enemy)) {
        return GoalStatus::Done;
    }
    if (down(brain) || brain.actionCount() > 0) {
        return GoalStatus::Stop;
    }
    brain.addEnemy(*enemy);
    if (brain.target() != enemy) {
        brain.setTarget(enemy);
    }
    const float reach = kInReachShare * brain.meleeFar();
    if (brain.distanceTo(*enemy) > reach) {
        brain.queueAction(std::make_unique<MoveToHumanAction>(kEngageRunMs, reach));
    } else {
        brain.pushGoal(std::make_unique<FightGoal>());
    }
    return GoalStatus::Stop;
}

} // namespace coney::ai
