// SPDX-License-Identifier: GPL-3.0-or-later
#include "ai/address_person_goal.h"

#include <cmath>
#include <memory>

#include "ai/brain.h"
#include "ai/script_services.h"
#include "ai/turn_action.h"
#include "human/locomotion.h"

namespace coney::ai {

namespace {

// The horizontal distance between two points.
float planDistance(anim::Vec3 a, anim::Vec3 b) { return std::hypot(b.x - a.x, b.y - a.y); }

} // namespace

void PlayAnimationGoal::start(Brain& brain) { m_services->playScene(m_scene, brain, m_callback); }

GoalStatus PlayAnimationGoal::process(Brain& brain) {
    if (brain.human().outOfWorld() || m_services->sceneFinished(m_scene)) {
        return GoalStatus::Done;
    }
    return GoalStatus::Stop;
}

void PlayAnimationGoal::end(Brain& /*brain*/) { m_services->stopScene(m_scene); }

void AddressPersonGoal::start(Brain& brain) {
    const Brain* target = m_services->brain(m_order.target);
    if (target == nullptr || brain.distanceTo(*target) >= m_order.approach + m_order.approach) {
        return;
    }
    brain.clearActions();
    if (brain.actionCount() == 0) {
        brain.queueAction(TurnAction::toPoint(target->human().position(), kRandomDelay));
    }
}

GoalStatus AddressPersonGoal::process(Brain& brain) {
    // 1. The target gone; actions queued.
    const Brain* target = m_services->brain(m_order.target);
    if (target == nullptr || target->human().outOfWorld()) {
        return GoalStatus::Done;
    }
    if (brain.actionCount() > 0) {
        return GoalStatus::Stop;
    }
    // 2. Within the range: keep facing the target, turning to where it is heading when it moves.
    const anim::Vec3 at = target->human().position();
    const float distance = planDistance(brain.human().position(), at);
    if (distance < m_order.range) {
        const anim::Vec3 way = anim::subtract(at, brain.human().position());
        const float off = std::fabs(human::wrapAngle(human::headingOf(way) - brain.human().heading()));
        if (off > kAddressTurnAngle) {
            // **Coney choice**: the lead is one second of the target's velocity.
            const anim::Vec3 lead = anim::add(at, target->human().velocity());
            brain.queueAction(TurnAction::toPoint(lead, kRandomDelay));
            return GoalStatus::Stop;
        }
    }
    // 3, 4. The states.
    switch (m_state) {
    case 0:
        if (distance < m_order.approach) {
            m_state = 1;
            if (m_order.scene >= 0) {
                brain.pushGoal(std::make_unique<PlayAnimationGoal>(*m_services, m_order.scene, m_order.callback));
            }
        }
        return GoalStatus::Stop;
    case 1:
        if (m_order.scene >= 0) {
            m_state = 2;
        }
        return GoalStatus::Stop;
    case 2:
        return GoalStatus::Done;
    default:
        return GoalStatus::Stop;
    }
}

bool goalAddressPerson(Brain& brain, ScriptServices& services, AddressOrder order) {
    return brain.pushGoal(std::make_unique<AddressPersonGoal>(services, std::move(order)));
}

} // namespace coney::ai
