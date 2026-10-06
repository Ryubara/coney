// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "ai/action.h"
#include "combat/commands.h"

// The attack action: one press of an attack kind. It writes the kind's command into the human's per-player record
// once, as it starts, and then waits until the human's record `+0x08` holds none of the attack's flags; the clip's held
// flags decide when the AI is free again, exactly as they decide it for the player.
// Research: docs/research/ai.md#attack-action, docs/research/tasks.md#held-flags

namespace coney::ai {

/// The record `+0x08` bits the attack action waits on and refuses to abort under (`0x5c0221f`).
inline constexpr std::uint32_t kAttackWaitFlags = 0x05c0221f;

/// One press of an attack kind.
/// @orig 0x002fa918 AttackAction_Init (unknown)
class AttackAction final : public Action {
  public:
    /// A press of `kind` that starts `delayMs` after it reaches the front of the queue.
    AttackAction(int kind, std::int16_t delayMs) : Action(delayMs), m_kind(kind) {}

    /// Checks the target (done when there is none or it has no health), sets the brain's next attack (`+0x1e8`) to
    /// now + the attack delay (halved when the target's own target is this human or the brain is type 3) and the
    /// target's `+0x1ec`, then writes the command; the snap (kind 10) also writes a full stick toward the target.
    /// **Coney choices**: the target's `+0x1ec` takes the kind's `CfgAttackDelay` unscaled, standing in for the
    /// separate per-kind time (`0x00231590`, not traced) and its spacing; it is set whether or not either human is
    /// busy.
    /// @orig 0x002fa9a8 AttackAction_Start (unknown)
    [[nodiscard]] ActionStatus start(Brain& brain) override;
    /// Done once the record's `+0x08` has none of kAttackWaitFlags. The command is not written again.
    /// @orig 0x002fad70 AttackAction_Update (unknown)
    [[nodiscard]] ActionStatus update(Brain& brain) override;
    /// Refused while the record's `+0x08` has any of kAttackWaitFlags.
    /// @orig 0x002fad30 AttackAction_Abort (unknown)
    [[nodiscard]] bool abort(Brain& brain) override;

    /// The attack kind it presses.
    [[nodiscard]] int kind() const { return m_kind; }
    /// The command it pressed (0 before it started).
    [[nodiscard]] combat::CommandId command() const { return m_command; }

  private:
    int m_kind;
    combat::CommandId m_command = combat::command::kNone;
    bool m_stick = false; // it wrote a stick, let go when it is done
};

} // namespace coney::ai
