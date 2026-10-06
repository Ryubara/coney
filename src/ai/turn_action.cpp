// SPDX-License-Identifier: GPL-3.0-or-later
#include "ai/turn_action.h"

#include <cmath>
#include <utility>

#include "ai/brain.h"
#include "human/locomotion.h"

namespace coney::ai {

std::unique_ptr<TurnAction> TurnAction::lookAt(TargetLocator locate, double target, float turn, std::int16_t delayMs) {
    auto action = std::make_unique<TurnAction>(Aim::Target, delayMs);
    action->m_locate = std::move(locate);
    action->m_target = target;
    action->m_turn = turn;
    return action;
}

std::unique_ptr<TurnAction> TurnAction::toPoint(anim::Vec3 point, std::int16_t delayMs) {
    auto action = std::make_unique<TurnAction>(Aim::Point, delayMs);
    action->m_point = point;
    return action;
}

std::unique_ptr<TurnAction> TurnAction::toHeading(float heading, std::int16_t delayMs) {
    auto action = std::make_unique<TurnAction>(Aim::Heading, delayMs);
    action->m_heading = human::wrapAngle(heading);
    return action;
}

ActionStatus TurnAction::start(Brain& brain) {
    if ((brain.human().gateInput().flags & kTurnRefuseFlags) != 0) {
        return ActionStatus::Done;
    }
    m_limitMs = brain.nowMs() + kTurnLimitMs;
    // The turn to a point takes its heading once.
    if (m_aim == Aim::Point) {
        const anim::Vec3 way = anim::subtract(m_point, brain.human().position());
        m_heading = std::hypot(way.x, way.y) > 1e-4F ? human::headingOf(way) : brain.human().heading();
    }
    return ActionStatus::Running;
}

ActionStatus TurnAction::update(Brain& brain) {
    const human::Human& human = brain.human();
    // A look-at follows its target: its heading is taken again each update.
    if (m_aim == Aim::Target) {
        const std::optional<anim::Vec3> target = m_locate ? m_locate(m_target) : std::nullopt;
        if (!target) {
            brain.stopMove();
            return ActionStatus::Done;
        }
        const anim::Vec3 way = anim::subtract(*target, human.position());
        if (std::hypot(way.x, way.y) > 1e-4F) {
            m_heading = human::headingOf(way);
        }
    }
    const bool faces = (human.gateInput().flags & kTurnHoldFlags) == 0 &&
                       std::fabs(human::wrapAngle(m_heading - human.heading())) <= kTurnDoneAngle;
    if (m_aborted || brain.nowMs() > m_limitMs || faces) {
        brain.stopMove();
        return ActionStatus::Done;
    }
    brain.setMoveHeading(m_heading, 0.0F);
    return ActionStatus::Running;
}

bool TurnAction::abort(Brain& brain) {
    m_aborted = true;
    brain.stopMove();
    return true;
}

} // namespace coney::ai
