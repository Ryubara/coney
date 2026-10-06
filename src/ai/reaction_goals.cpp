// SPDX-License-Identifier: GPL-3.0-or-later
#include "ai/reaction_goals.h"

#include <array>

#include "ai/brain.h"
#include "combat/player_combat.h"

namespace coney::ai {

GoalStatus ReactionGoal::process(Brain& brain) {
    return reactionHolds(type(), brain.human()) ? GoalStatus::Stop : GoalStatus::Done;
}

bool reactionHolds(GoalType type, const human::Human& human) {
    const human::Fighter& fighter = human.fighter();
    const human::Victim& victim = fighter.victim();
    const combat::CombatMode mode = fighter.combat().mode();
    switch (type) {
    case GoalType::ReactGrabbing:
        return mode == combat::CombatMode::Grabbing || mode == combat::CombatMode::Mugging;
    case GoalType::ReactTackling:
        return mode == combat::CombatMode::Tackling;
    case GoalType::ReactGrabbed:
        return fighter.grabbed();
    case GoalType::ReactKnockedDown:
        return victim.grounded() || fighter.health().depleted();
    case GoalType::ReactStunned:
        return (victim.stunned() || victim.stunExitPending()) && !victim.grounded();
    default:
        return false;
    }
}

std::unique_ptr<Goal> reactionGoalFor(const human::Human& human) {
    // Brain_UpdateReactionGoal's order: the first state that holds makes its goal.
    constexpr std::array<GoalType, 5> kOrder{GoalType::ReactGrabbing, GoalType::ReactTackling, GoalType::ReactGrabbed,
                                             GoalType::ReactKnockedDown, GoalType::ReactStunned};
    for (const GoalType type : kOrder) {
        if (reactionHolds(type, human)) {
            return std::make_unique<ReactionGoal>(type);
        }
    }
    return nullptr;
}

} // namespace coney::ai
