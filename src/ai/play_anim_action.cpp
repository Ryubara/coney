// SPDX-License-Identifier: GPL-3.0-or-later
#include "ai/play_anim_action.h"

#include <optional>

#include "ai/brain.h"
#include "ai/script_services.h"

namespace coney::ai {

ActionStatus PlayAnimAction::start(Brain& brain) {
    brain.stopMove();
    return ActionStatus::Running;
}

ActionStatus PlayAnimAction::update(Brain& brain) {
    if (!m_playing) {
        const std::optional<std::uint32_t> held = m_services->playClip(brain, m_animId);
        if (!held) {
            return ActionStatus::Done;
        }
        m_playing = true;
        m_heldFlags = *held;
        return ActionStatus::Running;
    }
    return (brain.human().animator().flags() & m_heldFlags) == 0 ? ActionStatus::Done : ActionStatus::Running;
}

} // namespace coney::ai
