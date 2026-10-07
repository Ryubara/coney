// SPDX-License-Identifier: GPL-3.0-or-later
#include "ai/fight_goal.h"

#include <cstddef>
#include <memory>
#include <optional>
#include <vector>

#include "ai/attack_action.h"
#include "ai/attack_kinds.h"
#include "ai/block_goal.h"
#include "ai/brain.h"
#include "ai/move_to_human_action.h"
#include "human/fighter.h"

namespace coney::ai {

namespace {

// The re-target's period, ms.
constexpr std::uint64_t kRetargetMs = 1000;
// The move stops this share of the reach inside it, so the next attack finds the target in reach.
constexpr float kMoveStopShare = 0.9F;

// A fresh attack timer: now + a random 750-1000 ms + `extraMs`.
std::uint64_t attackTimer(Brain& brain, std::uint64_t extraMs) {
    return brain.nowMs() + static_cast<std::uint64_t>(rollRange(brain.random(), kAttackTimerMinMs, kAttackTimerMaxMs)) +
           extraMs;
}

} // namespace

void FightGoal::start(Brain& brain) {
    m_attackAtMs = attackTimer(brain, m_extraDelayMs);
    m_retargetAtMs = brain.nowMs() + kRetargetMs;
}

GoalStatus FightGoal::process(Brain& brain) {
    // 1. A target to fight, a slot on it, and within the far melee range × 1.1.
    const Brain* target = brain.target();
    if (target == nullptr || !Brain::fightable(*target) || !brain.hasAttackSlot()) {
        return GoalStatus::Done;
    }
    const float far = brain.meleeFar() * kFightRangeScale;
    if (brain.distanceTo(*target) > far) {
        return GoalStatus::Done;
    }
    // 2. Once a second, the nearest threat.
    if (brain.nowMs() >= m_retargetAtMs) {
        m_retargetAtMs = brain.nowMs() + kRetargetMs;
        brain.retarget();
        target = brain.target();
        if (target == nullptr || !brain.hasAttackSlot()) {
            return GoalStatus::Done;
        }
    }
    // 3. An announced attack may push the block goal, which is then the top: process it at once.
    if (tryBlock(brain)) {
        return GoalStatus::Again;
    }
    // 4. While actions are queued, wait.
    if (brain.actionCount() > 0) {
        return GoalStatus::Stop;
    }
    // 5. The pacing: the goal's timer, this brain's next attack and the target's.
    const std::uint64_t now = brain.nowMs();
    if (now < m_attackAtMs || now < brain.nextAttackMs() || now < target->attackableAtMs()) {
        return GoalStatus::Stop;
    }
    // 6. The attack, by weight.
    const std::optional<int> kind = pickAttack(brain.attackWeights(), brain.random());
    if (!kind.has_value()) {
        return GoalStatus::Stop;
    }
    // 8. Out of reach: walk to the target. In reach: press the attack, and the timer starts again.
    const float reach = attackReach(brain.human(), *kind);
    const float distance = brain.distanceTo(*target);
    if (distance > reach) {
        const int limit = distance > 2.0F * reach ? kLongMoveMs : kShortMoveMs;
        brain.queueAction(std::make_unique<MoveToHumanAction>(limit, reach * kMoveStopShare));
        return GoalStatus::Stop;
    }
    queueAttack(brain, *kind);
    m_attackAtMs = attackTimer(brain, 0);
    return GoalStatus::Stop;
}

GoalStatus CloseInGoal::process(Brain& brain) {
    Brain* target = brain.target();
    if (target == nullptr || !Brain::fightable(*target)) {
        return GoalStatus::Done;
    }
    // Waits while either is down, while the human is busy, and while the target's attack slots are all taken.
    const human::Human& human = brain.human();
    if (human.fighter().health().depleted() || human.state() == human::TargetState::Grounded ||
        target->human().state() == human::TargetState::Grounded || brain.actionCount() > 0) {
        return GoalStatus::Stop;
    }
    brain.setTarget(target);
    if (!brain.hasAttackSlot()) {
        return GoalStatus::Stop;
    }
    // Beyond the fight goal's range: run at the target, stopping inside the reach. Within it: the fight goal again.
    const float reach = kInReachShare * brain.meleeFar();
    if (brain.distanceTo(*target) > brain.meleeFar() * kFightRangeScale) {
        brain.queueAction(std::make_unique<MoveToHumanAction>(kLongMoveMs, reach));
        return GoalStatus::Stop;
    }
    brain.pushGoal(std::make_unique<FightGoal>());
    return GoalStatus::Again;
}

void queueAttack(Brain& brain, int kind) {
    const std::vector<int> chain = chainOf(kind);
    for (std::size_t i = 0; i < chain.size(); ++i) {
        const int delay = i == 0 ? 0 : chainDelayMs(chain[i - 1], brain.human().animator().anims());
        brain.queueAction(std::make_unique<AttackAction>(chain[i], static_cast<std::int16_t>(delay)));
    }
}

float attackReach(const human::Human& human, int kind) {
    const combat::AnimRangeList* ranges = human.ranges();
    const float far = ranges != nullptr ? ranges->farRange(firstAnimOf(kind)) : 0.0F;
    return kInReachShare * (far > 0.0F ? far : human::kDefaultStrikeReach);
}

} // namespace coney::ai
