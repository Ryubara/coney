// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "ai/action.h"

// The move-to-human action: walks or runs the human to its brain's target, for at most a time limit. It writes no
// stick: it sets the brain's move (a heading and a speed, brain `+0x110`, `+0x114`), which the human's locomotion
// follows with the same turns and gaits as the player's stick.
// Research: docs/research/ai.md#moving, docs/research/ai.md#fight

namespace coney::ai {

/// Beyond this distance the move runs, closer it walks (**Coney choice**: at runtime a civilian ran from 8 m and walked
/// close by; the boundary is not traced).
inline constexpr float kRunDistance = 4.0F;

/// The move to the target.
/// @orig 0x002fcf50 MoveToHumanAction_Init (unknown)
class MoveToHumanAction final : public Action {
  public:
    /// A move of at most `limitMs` that ends within `stopDistance` of the target.
    MoveToHumanAction(std::uint32_t limitMs, float stopDistance) : m_limitMs(limitMs), m_stopDistance(stopDistance) {}

    /// Sets the time limit.
    [[nodiscard]] ActionStatus start(Brain& brain) override;
    /// Done (the move stopped) when the target is gone, within the stop distance, or the limit has passed; otherwise
    /// the move heads for the target at the run speed beyond kRunDistance and the walk speed within it (the human's
    /// gait speeds, `0x0022ae40`). **Coney choice**: straight at the target; the path areas and the steering round
    /// other humans (`0x002fc158`, `0x00289138`) are not built.
    [[nodiscard]] ActionStatus update(Brain& brain) override;
    /// Stops the move; never refused.
    [[nodiscard]] bool abort(Brain& brain) override;

  private:
    std::uint32_t m_limitMs;
    float m_stopDistance;
    std::uint64_t m_untilMs = 0;
};

} // namespace coney::ai
