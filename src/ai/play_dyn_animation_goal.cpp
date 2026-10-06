// SPDX-License-Identifier: GPL-3.0-or-later
#include "ai/play_dyn_animation_goal.h"

#include <array>
#include <memory>

#include "ai/brain.h"
#include "ai/play_anim_action.h"
#include "ai/script_services.h"

namespace coney::ai {

void PlayDynAnimationGoal::start(Brain& /*brain*/) {}

void PlayDynAnimationGoal::suspend(Brain& /*brain*/) { m_interrupted = true; }

GoalStatus PlayDynAnimationGoal::process(Brain& brain) {
    if (m_interrupted || brain.human().state() != human::TargetState::Standing) {
        return GoalStatus::Done;
    }
    if (brain.actionCount() > 0) {
        return GoalStatus::Stop;
    }
    // The play-anim action has ended: the clip played.
    if (m_queued) {
        m_completed = true;
        return GoalStatus::Done;
    }
    if (!m_services->dynamicClipLoaded(brain) || (brain.human().animator().flags() & ~kDynAnimationIgnoredFlag) != 0) {
        return GoalStatus::Stop;
    }
    brain.queueAction(std::make_unique<PlayAnimAction>(*m_services, kDynamicAnimId, m_option));
    m_queued = true;
    return GoalStatus::Stop;
}

void PlayDynAnimationGoal::end(Brain& brain) {
    scheduleGoalCallback(*m_services, brain, m_callback, m_completed);
    m_services->freeDynamicClip(brain);
}

bool goalPlayDynAnimation(Brain& brain, ScriptServices& services, std::string_view name, std::string callback,
                          bool option) {
    if (name.empty()) {
        return false;
    }
    services.loadDynamicClip(brain, name);
    return brain.pushGoal(std::make_unique<PlayDynAnimationGoal>(services, std::move(callback), option));
}

void scheduleGoalCallback(ScriptServices& services, const Brain& brain, std::string_view callback, bool completed) {
    if (callback.empty()) {
        return;
    }
    const std::array<double, 2> args{brain.handle(), completed ? 1.0 : 0.0};
    services.schedule(callback, args, kGoalCallbackDelayMs);
}

} // namespace coney::ai
