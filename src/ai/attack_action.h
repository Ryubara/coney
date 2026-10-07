// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <memory>
#include <optional>

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
    /// A press of `kind` that starts `delayMs` after it reaches the front of the queue, with a full stick along
    /// `stickHeading` (radians, Coney's heading convention; `Brain_QueueAttack`'s angle) when one is given.
    AttackAction(int kind, std::int16_t delayMs, std::optional<float> stickHeading = std::nullopt)
        : Action(delayMs), m_kind(kind), m_stickHeading(stickHeading) {}

    /// A press of `kind` on the human himself after `delayMs` (the grounded goal's get-up attack, kind 42): it needs
    /// no target and sets no pacing; it only writes the command.
    [[nodiscard]] static std::unique_ptr<AttackAction> onSelf(int kind, std::int16_t delayMs);

    /// Checks the target (done when there is none or it has no health), sets the brain's next attack (`+0x1e8`) to
    /// now + the attack delay (halved when the target's own target is this human or the brain is type 3); when
    /// neither human is busy, sets the target's `+0x1ec` to the later of it and now, plus the kind's swing time over
    /// the target's spacing (ai::swingTimeMs(), ai::attackableGapMs()); then writes the command, with a full stick
    /// along the action's heading when it has one (the snap, kind 10, toward the target when none is given).
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
    std::optional<float> m_stickHeading; // +0x1c, the stick's heading
    bool m_stick = false;                // it wrote a stick, let go when it is done
    bool m_self = false;                 // pressed on himself, with no target
};

} // namespace coney::ai
