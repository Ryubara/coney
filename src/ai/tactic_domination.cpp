// SPDX-License-Identifier: GPL-3.0-or-later
#include "ai/tactic_domination.h"

#include <cmath>
#include <cstddef>
#include <limits>
#include <memory>
#include <optional>

#include "ai/brain.h"
#include "ai/fight_goal.h"
#include "ai/gangs.h"
#include "ai/move_action.h"
#include "ai/move_to_flag_goal.h"
#include "ai/move_to_human_action.h"

namespace coney::ai {

namespace {

// How long one run at an enemy lasts before the goal looks again, ms.
constexpr std::uint32_t kHoldRunMs = 1000;

// The distance in plan between two points.
float planDistance(anim::Vec3 a, anim::Vec3 b) { return std::hypot(a.x - b.x, a.y - b.y); }

// The nearest fightable enemy of `brain`'s gang standing within `range` of `centre`; null when none. Enemies outside
// the range are refused as targets (`0x002b9730`).
// @orig 0x002b9730 HoldFlagGoal_FilterTarget (unknown)
Brain* nearestEnemyNear(Brain& brain, Gangs& gangs, anim::Vec3 centre, float range) {
    const Gang* own = brain.gang();
    Brain* nearest = nullptr;
    float best = std::numeric_limits<float>::max();
    for (std::size_t id = 0; id < kGangSlots; ++id) {
        Gang* other = gangs.find(static_cast<int>(id));
        if (other == nullptr || other == own || !(Gangs::enemies(own, other) || Gangs::enemies(other, own))) {
            continue;
        }
        for (Brain* member : other->members()) {
            if (!Brain::fightable(*member) || planDistance(member->human().position(), centre) > range) {
                continue;
            }
            const float distance = brain.distanceTo(*member);
            if (distance < best) {
                best = distance;
                nearest = member;
            }
        }
    }
    return nearest;
}

// Gives every AI member of `gang` that is not down and has none the hold-flag goal.
void assignGoals(Gang& gang, double flag, float range, FlagServices& flags) {
    for (Brain* member : gang.members()) {
        if (member->type() == BrainType::Player || !Brain::fightable(*member) ||
            member->findGoal(kHoldFlagGoal) != nullptr) {
            continue;
        }
        member->pushGoal(std::make_unique<HoldFlagGoal>(flag, range, flags, gang.owner()));
    }
}

} // namespace

GoalStatus HoldFlagGoal::process(Brain& brain) {
    const std::optional<world_objects::Placement> placement = m_flags->flag(m_flag);
    if (!placement) {
        return GoalStatus::Done;
    }
    const anim::Vec3 centre{placement->position[0], placement->position[1], placement->position[2]};
    // Wait while down or busy with a move.
    if (brain.human().fighter().health().depleted() || brain.human().state() == human::TargetState::Grounded ||
        brain.actionCount() > 0) {
        return GoalStatus::Stop;
    }
    // An enemy on the ground held: fight him.
    if (Brain* enemy = nearestEnemyNear(brain, *m_gangs, centre, m_range); enemy != nullptr) {
        brain.addEnemy(*enemy);
        if (brain.target() != enemy) {
            brain.setTarget(enemy);
        }
        const float reach = kInReachShare * brain.meleeFar();
        if (brain.distanceTo(*enemy) > reach) {
            brain.queueAction(std::make_unique<MoveToHumanAction>(kHoldRunMs, reach));
        } else {
            brain.pushGoal(std::make_unique<FightGoal>());
        }
        return GoalStatus::Stop;
    }
    // Back to the flag: running from afar, walking within the range.
    // @orig 0x002b97c0 HoldFlagGoal_MoveToFlag (unknown)
    const float distance = planDistance(brain.human().position(), centre);
    if (distance > kHoldFlagArrival) {
        brain.queueAction(
            std::make_unique<MoveAction>(MoveRequest{.point = centre,
                                                     .radius = kHoldFlagArrival,
                                                     .gait = distance > m_range ? kHoldFlagRunGait : kHoldFlagWalkGait,
                                                     .option = false,
                                                     .delayMs = 0,
                                                     .flagKind = false}));
    }
    return GoalStatus::Stop;
}

void TacticDomination::start(Gang& gang) { assignGoals(gang, m_flag, m_range, *m_flags); }

bool TacticDomination::event(Gang& gang, Brain& /*member*/, const BrainEvent& event) {
    if (event.id == kDominationReassignA || event.id == kDominationReassignB) {
        assignGoals(gang, m_flag, m_range, *m_flags);
    }
    return false;
}

} // namespace coney::ai
