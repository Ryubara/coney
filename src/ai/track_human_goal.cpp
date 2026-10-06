// SPDX-License-Identifier: GPL-3.0-or-later
#include "ai/track_human_goal.h"

#include <cmath>
#include <memory>
#include <optional>

#include "ai/brain.h"
#include "ai/formations.h"
#include "ai/move_action.h"
#include "ai/script_services.h"
#include "ai/turn_action.h"
#include "human/locomotion.h"

namespace coney::ai {

void TrackHumanGoal::start(Brain& brain) {
    Brain* target = m_services->brain(m_target);
    if (target == nullptr) {
        return;
    }
    if (Formation* formation = m_formations->of(*target, true); formation != nullptr) {
        formation->join(brain);
    }
}

GoalStatus TrackHumanGoal::process(Brain& brain) {
    const Brain* target = m_services->brain(m_target);
    if (target == nullptr || target->human().outOfWorld()) {
        return GoalStatus::Done;
    }
    if (brain.updates() % kTrackClearPeriod == 0) {
        brain.clearActions();
    }
    if (brain.actionCount() > 0) {
        return GoalStatus::Stop;
    }
    if (brain.moveFailure() != MoveFailure::None) {
        ++m_failed;
    }
    // The point: its slot's, or where it stands.
    const anim::Vec3 position = brain.human().position();
    std::optional<anim::Vec3> point;
    if (const Formation* formation = brain.following(); formation != nullptr) {
        point = formation->slotPoint(brain);
    }
    const anim::Vec3 aim = point.value_or(position);
    if (std::hypot(aim.x - position.x, aim.y - position.y) > m_distance) {
        if (m_failed >= kTrackRetryUpdates) {
            brain.setMoveFailure(MoveFailure::None);
            m_failed = 0;
        }
        if (m_failed == 0) {
            brain.queueAction(std::make_unique<MoveAction>(MoveRequest{.point = aim,
                                                                       .radius = m_distance,
                                                                       .gait = kTrackGait,
                                                                       .option = true,
                                                                       .delayMs = 0,
                                                                       .flagKind = false}));
            return GoalStatus::Stop;
        }
    }
    // Facing the target.
    const anim::Vec3 way = anim::subtract(target->human().position(), position);
    if (std::hypot(way.x, way.y) > 1e-4F &&
        std::fabs(human::wrapAngle(human::headingOf(way) - brain.human().heading())) > kTrackTurnAngle) {
        brain.queueAction(TurnAction::toPoint(target->human().position(), kRandomDelay));
    }
    return GoalStatus::Stop;
}

void TrackHumanGoal::end(Brain& brain) {
    if (Formation* formation = brain.following(); formation != nullptr) {
        formation->leave(brain);
    }
}

bool goalTrackHuman(Brain& brain, ScriptServices& services, Formations& formations, double target, float distance) {
    return brain.pushGoal(std::make_unique<TrackHumanGoal>(services, formations, target, distance));
}

} // namespace coney::ai
