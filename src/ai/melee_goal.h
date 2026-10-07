// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "ai/goal.h"

// The Melee goal (type 8): the middle of a fight's three goals (FindEnemy, Melee, the fight goal), which takes over
// whenever the fight goal ends. It picks the enemy to fight, sends the human in after him with an EngageEnemy goal
// while he is beyond the fight goal's range, and pushes a fresh fight goal once he is within it.
// Research: docs/research/ai.md#melee-goal, docs/research/ai.md#fight-durations

namespace coney::ai {

/// The Melee goal.
/// @orig 0x002ade10 MeleeGoal_Init (unknown)
class MeleeGoal final : public Goal {
  public:
    /// A Melee goal whose time limit is `durationMs` from its first run (kNoFightLimit for none).
    explicit MeleeGoal(int durationMs = kNoFightLimit) : Goal(GoalType::Melee), m_durationMs(durationMs) {}

    /// Starts the time limit's clock: the goal first runs once the fight goal above it has ended.
    void start(Brain& brain) override;
    /// One update (docs/research/ai.md#melee-goal): past its time limit it is done without running; while actions are
    /// queued it waits; it chooses the target (with a threat response, the best valid enemy; without, only the current
    /// target while it holds an attack slot on him; ai::pickBestEnemy() with score_term::kMelee); with none it is done.
    /// Then, `R` being the far melee range × 1.1: armed, or unarmed and allowed to approach (Brain::mayApproach())
    /// within `R`, it walks over in the fight stance when he is beyond 1 m and the straight line to him is not walkable
    /// (a move at gait 2, radius 1 m), else pushes a fight goal of kMeleeFightMs and processes it at once; unarmed and
    /// allowed beyond `R`, it pushes an EngageEnemy goal. **Coney stand-ins**: the line of sight always holds here; the
    /// chase is always allowed (Coney keeps no gang wanted timers); with no valid target it is done rather than
    /// spectating; and a human not allowed to approach (its last move failed) runs straight at the target (a
    /// move-to-human action, 2 s) and may approach again, the original's weapon pick-up, throw and positioning moves
    /// not being traced.
    /// @orig 0x002aebf8 MeleeGoal_Process (unknown)
    /// @orig 0x002ae2e8 MeleeGoal_WithTarget (unknown)
    [[nodiscard]] GoalStatus process(Brain& brain) override;
    /// The human may approach again (brain `+0x2d3`).
    /// @orig 0x002ade60 MeleeGoal_End (unknown)
    void end(Brain& brain) override;

  private:
    int m_durationMs;
    std::uint64_t m_limitMs = 0;         // +0x08; 0 for none
    const Brain* m_lastTarget = nullptr; // +0x10, the target's handle in the original
};

} // namespace coney::ai
