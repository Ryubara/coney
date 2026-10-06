// SPDX-License-Identifier: GPL-3.0-or-later
#include "ai/scripted_goals.h"

#include <utility>

#include "ai/brain.h"
#include "ai/move_action.h"
#include "ai/script_services.h"
#include "animation/anim_math.h"

namespace coney::ai {

namespace {

// The walk's gait id (`Human_SpeedForGait`).
constexpr int kWalkGait = 2;

} // namespace

void BackoffGoal::start(Brain& brain) { m_startMs = brain.nowMs(); }

void BackoffGoal::end(Brain& brain) { brain.stopMove(); }

GoalStatus BackoffGoal::process(Brain& brain) {
    const Brain* from = m_services->brain(m_from);
    if (from == nullptr) {
        return GoalStatus::Done;
    }
    if (m_timeMs >= 0 && brain.nowMs() >= m_startMs + static_cast<std::uint64_t>(m_timeMs)) {
        return GoalStatus::Done;
    }
    // Too near: walk straight away from the other human; far enough: stand.
    if (brain.distanceTo(*from) < m_distance) {
        const anim::Vec3 away = anim::subtract(brain.human().position(), from->human().position());
        brain.setMove(away, gaitSpeed(brain.human().speeds(), kWalkGait));
    } else {
        brain.stopMove();
    }
    return GoalStatus::Stop;
}

GoalStatus BumLogicGoal::process(Brain& brain) {
    if (brain.actionCount() == 0) {
        brain.stopMove();
    }
    return GoalStatus::Stop;
}

MoveToUseFlagGoal::MoveToUseFlagGoal(const MoveToFlagOrder& move, FlagServices& services, const UseFlagOrder& use,
                                     std::function<void()> release)
    : Goal(GoalType::MoveToUseFlag), m_move(std::make_unique<MoveToFlagGoal>(move, services)), m_use(use),
      m_release(std::move(release)) {}

void MoveToUseFlagGoal::start(Brain& brain) { m_move->start(brain); }

void MoveToUseFlagGoal::resume(Brain& brain) {
    if (!m_arrived) {
        m_move->resume(brain);
    }
}

void MoveToUseFlagGoal::end(Brain& brain) {
    brain.stopMove();
    if (m_release) {
        m_release();
        m_release = nullptr;
    }
}

GoalStatus MoveToUseFlagGoal::process(Brain& brain) {
    if (!m_arrived) {
        if (m_move->process(brain) != GoalStatus::Done) {
            return GoalStatus::Stop;
        }
        m_arrived = true;
    }
    // At the flag: the use (a stand-in) keeps the human there.
    if (brain.actionCount() == 0) {
        brain.stopMove();
    }
    return GoalStatus::Stop;
}

} // namespace coney::ai
