// SPDX-License-Identifier: GPL-3.0-or-later
#include "ai/tactic_confront.h"

#include <memory>

#include "ai/brain.h"
#include "ai/gangs.h"
#include "ai/move_to_human_action.h"

namespace coney::ai {

namespace {

// How long one close-in move lasts before the goal looks again, ms.
constexpr std::uint32_t kConfrontMoveMs = 1000;

// Whether `brain` stands: alive and not on the ground.
bool standingUp(const Brain& brain) {
    return Brain::fightable(brain) && brain.human().state() != human::TargetState::Grounded;
}

} // namespace

GoalStatus ConfrontGoal::process(Brain& brain) {
    const Brain* target = brain.target();
    if (target == nullptr || !Brain::fightable(*target)) {
        return GoalStatus::Done;
    }
    if (brain.actionCount() == 0 && brain.distanceTo(*target) > m_distance) {
        brain.queueAction(std::make_unique<MoveToHumanAction>(kConfrontMoveMs, m_distance));
    }
    return GoalStatus::Stop;
}

Brain* gangLeader(const Gang& gang) {
    Brain* fallback = nullptr;
    for (Brain* member : gang.members()) {
        if (!standingUp(*member)) {
            continue;
        }
        if (member->type() != BrainType::Player) {
            return member;
        }
        if (fallback == nullptr) {
            fallback = member;
        }
    }
    return fallback;
}

void TacticConfront::start(Gang& gang) {
    m_target = m_settings.targetGang;
    if (m_target < 0 && !gang.members().empty()) {
        const Brain* target = gang.members().front()->target();
        if (target != nullptr && target->gang() != nullptr) {
            m_target = target->gang()->id();
        }
    }
    const Gang* other = gang.owner().find(m_target);
    if (other == nullptr) {
        m_target = -1;
        return;
    }
    Brain* leader = gangLeader(*other);
    const float distance = m_settings.confrontation != 0 ? m_settings.approachRange : m_settings.criticalRange;
    for (Brain* member : gang.members()) {
        if (member->type() == BrainType::Player || leader == nullptr) {
            continue;
        }
        member->setTarget(leader);
        member->pushGoal(std::make_unique<ConfrontGoal>(distance));
    }
    m_nextMs = gang.owner().nowMs();
}

int TacticConfront::update(Gang& gang) {
    const std::uint64_t now = gang.owner().nowMs();
    if (m_target < 0 || now < m_nextMs) {
        return 0;
    }
    m_nextMs = now + kConfrontPeriodMs;
    const Gang* other = gang.owner().find(m_target);
    const Brain* ours = gangLeader(gang);
    const Brain* theirs = other != nullptr ? gangLeader(*other) : nullptr;
    if (ours == nullptr || theirs == nullptr) {
        return 0;
    }
    const float distance = ours->distanceTo(*theirs);
    if (distance <= m_settings.criticalRange) {
        m_inRange = true;
        return kTacInCriticalRange;
    }
    if (distance <= m_settings.approachRange) {
        m_inRange = true;
        return kTacInRange;
    }
    if (m_inRange) {
        m_inRange = false;
        return kTacLeftRange;
    }
    return 0;
}

bool TacticConfront::event(Gang& gang, Brain& /*member*/, const BrainEvent& event) {
    if (event.id == kEventDamaged) {
        fireCallback(gang, kTacDamage);
    } else if (event.id == kConfrontAttackedEvent) {
        fireCallback(gang, kTacAttacked);
    }
    return false;
}

} // namespace coney::ai
