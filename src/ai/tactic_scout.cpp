// SPDX-License-Identifier: GPL-3.0-or-later
#include "ai/tactic_scout.h"

#include <cmath>
#include <memory>

#include "ai/brain.h"
#include "ai/gangs.h"
#include "ai/move_action.h"
#include "ai/script_services.h"
#include "ai/tactic_attack.h"
#include "ai/turn_action.h"
#include "human/human.h"
#include "human/locomotion.h"

namespace coney::ai {

namespace {

// Whether `brain` is an AI member who can take orders: no player's, fightable and not on the ground.
bool canAct(const Brain& brain) {
    return brain.type() != BrainType::Player && Brain::fightable(brain) &&
           brain.human().state() != human::TargetState::Grounded;
}

// Plan distance between two points.
float planDistance(anim::Vec3 a, anim::Vec3 b) { return std::hypot(a.x - b.x, a.y - b.y); }

} // namespace

// ---- ScoutGoal ----

void ScoutGoal::start(Brain& brain) { apply(brain); }

void ScoutGoal::resume(Brain& brain) { apply(brain); }

void ScoutGoal::suspend(Brain& brain) { restore(brain); }

void ScoutGoal::end(Brain& brain) { restore(brain); }

void ScoutGoal::apply(Brain& brain) {
    if (m_applied) {
        return;
    }
    // Threat response 0 and the 500 ms scan while he keeps his post.
    m_oldThreatResponse = brain.threatResponse();
    brain.setThreatResponse(0);
    m_schedule->setInterval(brain, kScoutScanMs);
    m_applied = true;
}

void ScoutGoal::restore(Brain& brain) {
    if (!m_applied) {
        return;
    }
    brain.setThreatResponse(m_oldThreatResponse);
    m_schedule->setInterval(brain, 0);
    m_applied = false;
}

GoalStatus ScoutGoal::process(Brain& brain) {
    apply(brain);
    if (brain.actionCount() > 0) {
        return GoalStatus::Stop;
    }
    const human::Human& body = brain.human();
    // Off his post: back to it, at a walk within the roam radius or near a hidden player, else at a jog.
    const float off = planDistance(body.position(), m_post);
    if (off > kScoutPostSlack) {
        const bool hiddenNear = m_player != nullptr && m_player->human().hidden() &&
                                planDistance(m_player->human().position(), body.position()) <= kScoutHiddenNear;
        const int gait = off <= m_roamRadius || hiddenNear ? 2 : 3;
        brain.queueAction(
            std::make_unique<MoveAction>(MoveRequest{.point = m_post, .radius = kScoutPostSlack, .gait = gait}));
        return GoalStatus::Stop;
    }
    // At his post: facing his heading.
    if (std::fabs(human::wrapAngle(m_heading - body.heading())) > kScoutHeadingSlack) {
        brain.queueAction(TurnAction::toHeading(m_heading));
    }
    return GoalStatus::Stop;
}

// ---- CallGangGoal ----

void CallGangGoal::start(Brain& brain) {
    m_phone = brain.human().position();
    if (m_services->phone) {
        if (const std::optional<anim::Vec3> phone = m_services->phone(m_phone, m_order.radius); phone) {
            m_phone = *phone;
        }
    }
    m_services->callerActive = true;
}

void CallGangGoal::end(Brain& /*brain*/) { m_services->callerActive = false; }

GoalStatus CallGangGoal::process(Brain& brain) {
    if (brain.actionCount() > 0) {
        return GoalStatus::Stop;
    }
    // The run to the phone.
    if (!m_arrived) {
        if (planDistance(brain.human().position(), m_phone) > kCallReach) {
            brain.queueAction(
                std::make_unique<MoveAction>(MoveRequest{.point = m_phone, .radius = kCallReach, .gait = kCallGait}));
            return GoalStatus::Stop;
        }
        m_arrived = true;
        m_arrivedMs = brain.nowMs();
    }
    // The call, once its length has passed: the responders and the enemy's second wanted timer.
    if (brain.nowMs() < m_arrivedMs + kCallMs) {
        return GoalStatus::Stop;
    }
    if (m_services->queueGangCall) {
        m_services->queueGangCall(m_order.count, m_order.delaySeconds, m_phone);
    }
    if (m_services->setSecondWanted && m_order.gang >= 0) {
        m_services->setSecondWanted(m_order.gang);
    }
    m_called = true;
    return GoalStatus::Done;
}

// ---- ScoutTactic ----

void ScoutTactic::start(Gang& gang) {
    const Brain* player = services().scripts != nullptr ? services().scripts->player() : nullptr;
    for (Brain* member : gang.members()) {
        if (canAct(*member)) {
            const human::Human& body = member->human();
            member->pushGoal(
                std::make_unique<ScoutGoal>(body.position(), body.heading(), call().range2, m_schedule, player));
        }
    }
}

int ScoutTactic::update(Gang& gang) {
    const std::uint64_t now = gang.owner().nowMs();
    // The members at their posts scan.
    m_schedule.beginUpdate();
    for (Brain* member : gang.members()) {
        if (canAct(*member) && member->topGoal() != nullptr && member->topGoal()->type() == GoalType::Scout) {
            static_cast<void>(m_schedule.maybeScan(gang.owner(), *member, now));
        }
    }
    // Every 200 ms: a member with an enemy and no fight is given one.
    if (now >= m_nextCheckMs) {
        m_nextCheckMs = now + kScoutTacticCheckMs;
        for (Brain* member : gang.members()) {
            if (canAct(*member) && member->findGoal(kMeleeGoal) == nullptr &&
                member->findGoal(GoalType::CallGang) == nullptr &&
                std::ranges::any_of(member->enemies(), [](const Brain* e) { return Brain::fightable(*e); })) {
                giveMelee(*member, gang.owner());
            }
        }
    }
    return 0;
}

bool ScoutTactic::event(Gang& gang, Brain& member, const BrainEvent& event) {
    if (event.id != kEventDamaged && event.id != kEventEnemyAdded && event.id != kEventAttackWarning) {
        return StoryTactic::event(gang, member, event);
    }
    // 1. Who: the human in the event; nothing for a member who cannot act, a friend or no one.
    Brain* who = event.other;
    if (who == nullptr || !canAct(member) || !Brain::fightable(*who) || Gangs::friends(member.gang(), who->gang())) {
        return true;
    }
    // 2. A member with no goal does nothing more.
    if (member.topGoal() == nullptr) {
        return true;
    }
    // 3. Already fighting: a hit or an attack makes the human his target. Busy with anything but his post: nothing.
    if (member.findGoal(kMeleeGoal) != nullptr) {
        if (event.id != kEventEnemyAdded) {
            member.setTarget(who);
        }
        return true;
    }
    if (member.topGoal()->type() != GoalType::Scout) {
        return true;
    }
    // 5. A hit from someone he cannot see: **Coney stand-in**, the investigation is not built, so nothing.
    if (event.id == kEventDamaged && !member.hasLineOfSight(*who)) {
        return true;
    }
    // 4. He fights, then may run to call the gang.
    member.addEnemy(*who);
    member.setTarget(who);
    giveMelee(member, gang.owner());
    maybeCall(member, *who);
    return true;
}

void ScoutTactic::maybeCall(Brain& member, const Brain& enemy) {
    ScoutServices* scout = services().scout;
    if (scout == nullptr || !scout->responderReady || !scout->responderReady() || scout->callerActive) {
        return;
    }
    const int gang = enemy.gang() != nullptr ? enemy.gang()->id() : -1;
    if (gang < 0 || (scout->secondWanted && scout->secondWanted(gang))) {
        return;
    }
    const float radius = call().range < 0.0F ? 2.0F * member.meleeFar() : call().range;
    member.pushGoal(std::make_unique<CallGangGoal>(CallOrder{.gang = gang,
                                                             .radius = radius,
                                                             .count = static_cast<int>(call().count),
                                                             .delaySeconds = static_cast<int>(call().count2)},
                                                   *scout));
}

} // namespace coney::ai
