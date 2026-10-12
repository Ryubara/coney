// SPDX-License-Identifier: GPL-3.0-or-later
#include "ai/move_to_human_action.h"

#include <cmath>

#include "ai/brain.h"
#include "animation/anim_math.h"

namespace coney::ai {

namespace {

// How far along the move its aim point lies, m (`MoveMelee`'s update, `0x002fd4e8`).
constexpr float kAimAhead = 2.0F;

} // namespace

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
    const anim::Vec3 position = brain.human().position();
    const anim::Vec3 way = anim::subtract(target->human().position(), position);
    // The aim, 2 m along the move (radius 0), as the original's update sets it (docs/research/ai-moving.md#move-melee):
    // in a fight stance the feet strafe toward it while the facing turns to the heading.
    const float length = std::hypot(way.x, way.y);
    if (length > 1e-4F) {
        brain.setMoveAim(anim::add(position, anim::Vec3{kAimAhead * way.x / length, kAimAhead * way.y / length, 0.0F}),
                         0.0F);
    }
    brain.setMove(way, distance > kRunDistance ? speeds.run : speeds.walk);
    return ActionStatus::Running;
}

bool MoveToHumanAction::abort(Brain& brain) {
    brain.stopMove();
    return true;
}

} // namespace coney::ai
