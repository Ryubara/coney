// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "ai/action.h"

// The play-anim action: the human stands and plays one clip by anim id, and the action lasts as long as the clip
// holds the record flags it started with. A clip that cannot start ends the action at once.
// Research: docs/research/ai.md#dyn-animation, docs/research/ai.md#tactics

namespace coney::ai {

class ScriptServices;

/// The play-anim action (vtable `0x00542b60`).
/// @orig 0x002fa5a0 PlayAnimAction_Init (unknown)
class PlayAnimAction final : public Action {
  public:
    /// Plays anim `animId` through `services` (which must outlive it) after `delayMs`; `option` is kept, not read
    /// (whether it loops or holds is not traced).
    PlayAnimAction(ScriptServices& services, int animId, bool option, std::int16_t delayMs = 0)
        : Action(delayMs), m_services(&services), m_animId(animId), m_option(option) {}

    /// The human stands (the brain's speed 0).
    [[nodiscard]] ActionStatus start(Brain& brain) override;
    /// The first update starts the clip and keeps the record flags it holds; done when it cannot start, or once the
    /// record (`+0x08`) holds none of those flags.
    [[nodiscard]] ActionStatus update(Brain& brain) override;
    /// Always allowed.
    [[nodiscard]] bool abort(Brain& /*brain*/) override { return true; }

    /// The anim it plays, and its option.
    [[nodiscard]] int animId() const { return m_animId; }
    [[nodiscard]] bool option() const { return m_option; }

  private:
    ScriptServices* m_services;
    int m_animId;
    bool m_option;
    bool m_playing = false;
    std::uint32_t m_heldFlags = 0;
};

} // namespace coney::ai
