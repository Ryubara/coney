// SPDX-License-Identifier: GPL-3.0-or-later
#include "ai/reaction_goals.h"

#include <algorithm>
#include <array>

#include "ai/brain.h"
#include "ai/fight_reactions.h"
#include "combat/player_combat.h"
#include "human/locomotion_gate.h"

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
        return fighter.grabbed() || fighter.holdState() == human::TargetState::Held;
    case GoalType::ReactTackled:
        return fighter.holdState() == human::TargetState::Mounted;
    case GoalType::ReactKnockedDown:
        return victim.grounded() || fighter.health().depleted();
    case GoalType::ReactStunned:
        return victim.stunned() && !victim.grounded();
    default:
        return false;
    }
}

bool humanBusy(const human::Human& human) {
    // The reaction states that count as busy.
    constexpr std::array<GoalType, 5> kBusyStates{GoalType::ReactGrabbed, GoalType::ReactTackling,
                                                  GoalType::ReactTackled, GoalType::ReactKnockedDown,
                                                  GoalType::ReactStunned};
    // Coney stand-in for the state mask 0x1f80974000: the record's busy bits, the air and the reaction states Coney's
    // fighter models. Every Coney human has a record, so the original's test for none has no counterpart.
    const bool busyState =
        (human.animator().flags() & human::kBusyFlags) != 0 || human.airborne() ||
        std::ranges::any_of(kBusyStates, [&human](GoalType type) { return reactionHolds(type, human); });
    if (busyState) {
        return true;
    }
    // Otherwise busy while the game time is below the hit timer, record +0x108 (Fighter::busyUntilMs()), read on the
    // human's own clock as it was set.
    return human.nowMs() < human.fighter().busyUntilMs();
}

std::unique_ptr<Goal> reactionGoalFor(const human::Human& human) {
    // Brain_UpdateReactionGoal's order: the first state that holds makes its goal.
    constexpr std::array<GoalType, 6> kOrder{GoalType::ReactGrabbing,    GoalType::ReactTackling,
                                             GoalType::ReactGrabbed,     GoalType::ReactTackled,
                                             GoalType::ReactKnockedDown, GoalType::ReactStunned};
    for (const GoalType type : kOrder) {
        if (!reactionHolds(type, human)) {
            continue;
        }
        // The fight reactions (docs/research/ai/index.md#fight-reactions); the stun only holds the stack off.
        switch (type) {
        case GoalType::ReactGrabbing:
            return std::make_unique<GrabbingGoal>();
        case GoalType::ReactTackling:
            return std::make_unique<MountingGoal>();
        case GoalType::ReactGrabbed:
            return std::make_unique<HeldGoal>(false);
        case GoalType::ReactTackled:
            return std::make_unique<HeldGoal>(true);
        case GoalType::ReactKnockedDown:
            return std::make_unique<GroundedGoal>();
        default:
            return std::make_unique<ReactionGoal>(type);
        }
    }
    return nullptr;
}

} // namespace coney::ai
