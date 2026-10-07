// SPDX-License-Identifier: GPL-3.0-or-later
#include "ai/tactic_attack.h"

#include <algorithm>
#include <cstddef>
#include <limits>
#include <memory>

#include "ai/brain.h"
#include "ai/fight_goal.h"
#include "ai/gangs.h"
#include "ai/move_to_human_action.h"

namespace coney::ai {

namespace {

// How long one run at the enemy lasts before the goal looks again, ms.
constexpr std::uint32_t kMeleeRunMs = 2000;
// The threat response the tactic gives its members (brain `+0x21c`).
constexpr int kAttackThreatResponse = 2;

// Whether `brain` is an AI member the tactic runs: not a player's.
bool isAi(const Brain& brain) { return brain.type() != BrainType::Player; }

} // namespace

Brain* nearestGangEnemy(Brain& brain, Gangs& gangs) {
    const Gang* own = brain.gang();
    Brain* nearest = nullptr;
    float best = std::numeric_limits<float>::max();
    for (std::size_t id = 0; id < kGangSlots; ++id) {
        Gang* other = gangs.find(static_cast<int>(id));
        if (other == nullptr || other == own || !(Gangs::enemies(own, other) || Gangs::enemies(other, own))) {
            continue;
        }
        for (Brain* member : other->members()) {
            if (!Brain::fightable(*member)) {
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

GoalStatus TacticMeleeGoal::process(Brain& brain) {
    Brain* enemy = nearestGangEnemy(brain, *m_gangs);
    if (enemy == nullptr) {
        return GoalStatus::Done;
    }
    brain.addEnemy(*enemy);
    if (brain.target() != enemy) {
        brain.setTarget(enemy);
    }
    // Wait while down or busy with a move.
    if (brain.human().fighter().health().depleted() || brain.human().state() == human::TargetState::Grounded ||
        brain.actionCount() > 0) {
        return GoalStatus::Stop;
    }
    const float reach = kInReachShare * brain.meleeFar();
    if (brain.distanceTo(*enemy) > reach) {
        brain.queueAction(std::make_unique<MoveToHumanAction>(kMeleeRunMs, reach));
        return GoalStatus::Stop;
    }
    brain.pushGoal(std::make_unique<FightGoal>());
    return GoalStatus::Stop;
}

void giveMelee(Brain& brain, Gangs& gangs) {
    if (brain.findGoal(kMeleeGoal) == nullptr) {
        brain.pushGoal(std::make_unique<TacticMeleeGoal>(gangs));
    }
}

void TacticAttack::start(Gang& gang) {
    for (Brain* member : gang.members()) {
        if (isAi(*member)) {
            member->setThreatResponse(kAttackThreatResponse);
            giveMelee(*member, gang.owner());
        }
    }
    const std::uint64_t now = gang.owner().nowMs();
    m_nextEnemyCheckMs = now + kAttackEnemyCheckMs;
    m_nextIdleCheckMs = now + kAttackIdleCheckMs;
}

int TacticAttack::update(Gang& gang) {
    const std::uint64_t now = gang.owner().nowMs();
    if (now >= m_nextIdleCheckMs) {
        m_nextIdleCheckMs = now + kAttackIdleCheckMs;
        for (Brain* member : gang.members()) {
            if (isAi(*member) && member->goalCount() == 0 && Brain::fightable(*member)) {
                giveMelee(*member, gang.owner());
            }
        }
    }
    if (now >= m_nextEnemyCheckMs) {
        m_nextEnemyCheckMs = now + kAttackEnemyCheckMs;
        const bool anyEnemy = std::ranges::any_of(gang.members(), [](const Brain* member) {
            return std::ranges::any_of(member->enemies(), [](const Brain* enemy) { return Brain::fightable(*enemy); });
        });
        if (!anyEnemy) {
            return kTacNoEnemies;
        }
    }
    return 0;
}

bool TacticAttack::event(Gang& gang, Brain& member, const BrainEvent& event) {
    switch (event.id) {
    case kEventDamaged:
    case kEventEnemyAdded:
        if (isAi(member) && member.goalCount() == 0 && Brain::fightable(member)) {
            giveMelee(member, gang.owner());
        }
        return false;
    case kGangMessageHeadcount:
        fireCallback(gang, kTacMemberDied);
        return false;
    default:
        return false;
    }
}

} // namespace coney::ai
