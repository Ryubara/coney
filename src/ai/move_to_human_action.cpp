// SPDX-License-Identifier: GPL-3.0-or-later
#include "ai/move_to_human_action.h"

#include "ai/brain.h"

namespace coney::ai {

ActionStatus MoveToHumanAction::start(Brain& brain) {
    m_untilMs = brain.nowMs() + m_limitMs;
    return ActionStatus::Running;
}

ActionStatus MoveToHumanAction::update(Brain& brain) {
    const Brain* target = brain.target();
    if (target == nullptr || !Brain::fightable(*target) || brain.nowMs() >= m_untilMs) {
        brain.stopMove();
        return ActionStatus::Done;
    }
    const float distance = brain.distanceTo(*target);
    if (distance <= m_stopDistance) {
        brain.stopMove();
        return ActionStatus::Done;
    }
    const human::Speeds& speeds = brain.human().speeds();
    brain.setMove(anim::subtract(target->human().position(), brain.human().position()),
                  distance > kRunDistance ? speeds.run : speeds.walk);
    return ActionStatus::Running;
}

bool MoveToHumanAction::abort(Brain& brain) {
    brain.stopMove();
    return true;
}

} // namespace coney::ai
