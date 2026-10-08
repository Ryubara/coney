// SPDX-License-Identifier: GPL-3.0-or-later
#include "ai/scripted_goals.h"

#include <cmath>
#include <optional>
#include <utility>

#include "ai/brain.h"
#include "ai/move_action.h"
#include "ai/script_services.h"
#include "animation/anim_math.h"

namespace coney::ai {

namespace {

// The walk's and the run's gait ids (`Human_SpeedForGait`).
constexpr int kWalkGait = 2;
constexpr int kRunGait = 4;

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

TagGoal::TagGoal(double flag, double tag, double extra, FlagServices& flags, TagServices services,
                 std::function<void()> release)
    : Goal(GoalType::Tag), m_move(std::make_unique<MoveToFlagGoal>(
                               MoveToFlagOrder{.flag = flag, .gait = kRunGait, .radius = kTagReach}, flags)),
      m_flags(&flags), m_services(std::move(services)), m_release(std::move(release)), m_flag(flag), m_tag(tag),
      m_extra(extra) {}

void TagGoal::start(Brain& brain) { m_move->start(brain); }

void TagGoal::resume(Brain& brain) {
    if (m_state == 0) {
        m_move->resume(brain);
    }
}

void TagGoal::end(Brain& brain) {
    brain.stopMove();
    if (m_release) {
        m_release();
        m_release = nullptr;
    }
}

GoalStatus TagGoal::process(Brain& brain) {
    // Done when the flag is gone, the human cuffed or down, or another human holds the flag.
    const std::optional<world_objects::Placement> flag = m_flags->flag(m_flag);
    if (!flag || !brain.human().alive() || (m_services.heldByOther && m_services.heldByOther(m_flag, brain.handle()))) {
        return GoalStatus::Done;
    }
    const double human = brain.handle();
    if (m_state == 0) {
        if (m_move->process(brain) != GoalStatus::Done) {
            return GoalStatus::Stop;
        }
        // Stopped short of the flag: give up.
        const anim::Vec3 at = brain.human().position();
        if (std::hypot(at.x - flag->position[0], at.y - flag->position[1]) > kTagReach) {
            return GoalStatus::Done;
        }
        m_state = 1;
    }
    if (m_state == 1) {
        // The other tag is made blank once, then the spray starts.
        brain.stopMove();
        if (m_extra != 0.0 && m_services.blank) {
            m_services.blank(m_extra);
        }
        if (m_services.startTag) {
            m_services.startTag(human, m_tag, m_flag);
        }
        m_state = 2;
        return GoalStatus::Stop;
    }
    // Spraying until the tag is over.
    return m_services.tagging && m_services.tagging(human) ? GoalStatus::Stop : GoalStatus::Done;
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
