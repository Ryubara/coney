// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "ai/action.h"
#include "combat/commands.h"

// The set-command action: an AI presses one pad command through its per-player record, as a player's pad would, with
// no target, pacing or stick of its own. The grab spin a grabber hands his man over with, and the bosses' throws, go
// this way.
// Research: docs/research/ai-code.md, docs/research/ai.md#fight-reactions

namespace coney::ai {

/// One press of a command.
/// @orig 0x002faeb0 SetCommandAction_Init (unknown)
class SetCommandAction final : public Action {
  public:
    /// A press of `command` that starts `delayMs` after it reaches the front of the queue.
    SetCommandAction(combat::CommandId command, std::int16_t delayMs) : Action(delayMs), m_command(command) {}

    /// Writes the command into the record (`PlayerRecord_SetCommand`).
    /// @orig 0x002faee0 SetCommandAction_Start (unknown)
    [[nodiscard]] ActionStatus start(Brain& brain) override;
    /// Running on its first update, then until the record holds no flag.
    /// @orig 0x002faf20 SetCommandAction_Update (unknown)
    [[nodiscard]] ActionStatus update(Brain& brain) override;

    /// The command it presses.
    [[nodiscard]] combat::CommandId command() const { return m_command; }

  private:
    combat::CommandId m_command;
    bool m_updated = false;
};

} // namespace coney::ai
