// SPDX-License-Identifier: GPL-3.0-or-later
#include "ai/move_to_flag_goal.h"

#include <cmath>
#include <memory>
#include <numbers>

#include "ai/brain.h"
#include "ai/turn_action.h"
#include "human/locomotion.h"

namespace coney::ai {

namespace {

// Degrees to radians.
float radians(float degrees) { return degrees * std::numbers::pi_v<float> / 180.0F; }

// A placement's position as a vector.
anim::Vec3 positionOf(const world_objects::Placement& placement) {
    return anim::Vec3{placement.position[0], placement.position[1], placement.position[2]};
}

} // namespace

void MoveToFlagGoal::start(Brain& brain) {
    const std::optional<world_objects::Placement> flag = m_services->flag(m_order.flag);
    const anim::Vec3 base = flag ? positionOf(*flag) : brain.human().position();
    m_target = anim::add(base, anim::scale(human::facing(radians(m_order.angleDegrees)), m_order.distance));
    if (m_order.intervalMs != 0) {
        m_nextTickMs = brain.nowMs() + m_order.intervalMs;
    }
    resume(brain);
}

void MoveToFlagGoal::resume(Brain& brain) { brain.clearActions(); }

GoalStatus MoveToFlagGoal::process(Brain& brain) {
    // 1. The flag gone.
    const std::optional<world_objects::Placement> flag = m_services->flag(m_order.flag);
    if (!flag) {
        return GoalStatus::Done;
    }
    // 2. The interval's tick (its gesture is not built).
    if (m_order.intervalMs != 0 && brain.nowMs() >= m_nextTickMs) {
        m_nextTickMs = brain.nowMs() + m_order.intervalMs;
    }
    // 3. Wait for the queued move or turn.
    if (brain.actionCount() > 0) {
        return GoalStatus::Stop;
    }
    // 4. Inside the radius: face the flag when asked, else arrived. Outside: move there again.
    const anim::Vec3 position = brain.human().position();
    if (std::hypot(m_target.x - position.x, m_target.y - position.y) <= m_order.radius) {
        const float heading = radians(flag->headingDegrees);
        if (m_order.faceFlag && std::fabs(human::wrapAngle(heading - brain.human().heading())) > kFaceFlagAngle) {
            brain.queueAction(TurnAction::toHeading(heading));
            return GoalStatus::Stop;
        }
        m_arrived = true;
        return GoalStatus::Done;
    }
    const auto delay = static_cast<std::int16_t>(rollRange(brain.random(), 0, kMoveToFlagDelayMaxMs));
    brain.queueAction(std::make_unique<MoveAction>(MoveRequest{.point = m_target,
                                                               .radius = m_order.radius,
                                                               .gait = m_order.gait,
                                                               .option = m_order.option,
                                                               .delayMs = delay,
                                                               .flagKind = false}));
    return GoalStatus::Stop;
}

void MoveToFlagGoal::end(Brain& brain) {
    if (!m_arrived) {
        return;
    }
    m_services->arrived(m_order.flag, brain);
    m_services->tellGang(brain, m_order.flag);
}

bool goalMoveToFlag(Brain& brain, const MoveToFlagOrder& order, FlagServices& services) {
    return brain.pushGoal(std::make_unique<MoveToFlagGoal>(order, services));
}

} // namespace coney::ai
