// SPDX-License-Identifier: GPL-3.0-or-later
#include "ai/set_command_action.h"

#include "ai/brain.h"
#include "human/human.h"

namespace coney::ai {

ActionStatus SetCommandAction::start(Brain& brain) {
    brain.press(m_command);
    return ActionStatus::Running;
}

ActionStatus SetCommandAction::update(Brain& brain) {
    // The first update lets the dispatcher take the press; then it waits out what the press started.
    if (!m_updated) {
        m_updated = true;
        return ActionStatus::Running;
    }
    return brain.human().animator().flags() != 0 ? ActionStatus::Running : ActionStatus::Done;
}

} // namespace coney::ai
