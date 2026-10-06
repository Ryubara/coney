// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "ai/goal.h"

// Two goals that keep a human where it is, for the tactics and the dealer: Idle (type 0), which holds its place, and
// Spectate (type `0x10`), which watches for a while. Their processes are not traced: Coney's stand still, and a
// spectator is done once its time is up.
// Research: docs/research/ai.md#tactics, docs/research/ai.md#dealer

namespace coney::ai {

/// The idle goal (type 0, vtable `0x005418f0`): the human stays where it was given it. **Coney choice**: its process
/// (not traced) stands still and never ends.
/// @orig 0x002caf78 IdleGoal_Init (unknown)
class IdleGoal final : public Goal {
  public:
    IdleGoal() : Goal(GoalType::Idle) {}
    /// Stands still.
    [[nodiscard]] GoalStatus process(Brain& brain) override;
};

/// The spectate goal (type `0x10`, vtable `0x00540270`): the human stands and watches. **Coney choice**: its process
/// (not traced) stands still and ends once a time rolled at its start between the goal's two durations has passed.
/// @orig 0x002b4098 SpectateGoal_Init (unknown)
class SpectateGoal final : public Goal {
  public:
    /// A spectator for a random `minMs` to `maxMs`.
    SpectateGoal(int minMs, int maxMs) : Goal(GoalType::Spectate), m_minMs(minMs), m_maxMs(maxMs) {}
    /// Rolls how long it watches.
    void start(Brain& brain) override;
    /// Stands still; done once its time is up.
    [[nodiscard]] GoalStatus process(Brain& brain) override;

  private:
    int m_minMs; // +0x1c
    int m_maxMs; // +0x20
    std::uint64_t m_untilMs = 0;
};

} // namespace coney::ai
