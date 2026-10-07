// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "ai/goal.h"

// Two goals that keep a human out of a fight: Idle (type 0), which holds its place, and Spectate (type `0x10`), which
// watches a man from a distance for a while, for the Melee goal with no one to fight, the dealer and the crowd.
// Research: docs/research/ai.md#tactics, docs/research/ai.md#dealer, docs/research/ai.md#spectate

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

/// `SpectateGoal_Init`'s arguments.
struct SpectateArgs {
    float keepDistance = 0.0F; ///< `+0x24`: the distance he keeps from the watched man; 0 for the far melee range.
    bool join = false;         ///< `+0x28`: he watches an enemy of his own, and may join in.
    bool mayEngage = false;    ///< `+0x29`: with join, he may run in with an EngageEnemy goal.
    int shortestPauseMs = 0;   ///< `+0x1c`: the shortest pause between looks.
    int longestPauseMs = 0;    ///< `+0x20`: the longest.
    int timeLimitMs = 0;       ///< How long the goal lasts.
    bool taunt = false;        ///< `+0x2a`: he taunts at each look.

    /// The Melee goal's, with no target (join, may engage, 1-3 s pauses, 2 s, taunting), keeping `keepDistance`.
    [[nodiscard]] static SpectateArgs melee(float keepDistance);
    /// The dealer's wary goal's: 8 m, 2-4 s pauses, 8 s, no join.
    [[nodiscard]] static SpectateArgs dealerWary();
};

/// The spectate goal (type `0x10`, vtable `0x00540270`): the human watches a man from a distance, and with join may
/// run in at him when he runs.
/// @orig 0x002b4098 SpectateGoal_Init (unknown)
class SpectateGoal final : public Goal {
  public:
    /// A spectator with `args`.
    explicit SpectateGoal(const SpectateArgs& args) : Goal(GoalType::Spectate), m_args(args) {}
    /// A spectator of the crowd tactic's, which stands for a random `minMs` to `maxMs`. **Coney stand-in**: the crowd's
    /// arguments are not traced.
    SpectateGoal(int minMs, int maxMs);

    /// Picks whom to watch (SpectateGoal_PickTarget: with join the nearest of his enemies, else the nearest member of
    /// another gang), sets the next re-pick 3 s on and the time limit, and rolls the chase flag (50 %). **Coney
    /// stand-in**: the weapon pick-up roll is not made (no AI holds objects).
    /// @orig 0x002b4200 SpectateGoal_Start (unknown)
    /// @orig 0x002b4158 SpectateGoal_PickTarget (unknown)
    void start(Brain& brain) override;
    /// One update (docs/research/ai.md#spectate): past the time limit, done; a failed last move clears the chase flag;
    /// every 3 s the watched man is picked again. No valid man: done with join, else wait. With join he is made an
    /// enemy. Out of sight, done. With join and may engage, not after a failed move: an EngageEnemy goal at him when
    /// he runs (checked one update in five) or, with the chase flag, beyond the far range. Otherwise, with the actions
    /// free and the pause over: beyond 10 m a move toward him (gait 2, 4 when he runs or with join, radius far + 2 m);
    /// with join, between the keep distance and 2 m more, 51 % a turn to face him; else a move that holds him between
    /// 0.95 × and 1 × the keep distance. **Coney stand-ins**: the target is not cleared each update (a spectator never
    /// holds one but for the EngageEnemy run-in); the fight stance and the taunt are not made; the watch action is a
    /// turn to face him; the keep-distance move is a move action to the band's edge; the chase is always allowed; the
    /// tackling man's kind-`0x24` attack and the shadow test are not built.
    /// @orig 0x002b4330 SpectateGoal_Process (unknown)
    [[nodiscard]] GoalStatus process(Brain& brain) override;
    /// Clears the target, the actions and the last move's failure.
    /// @orig 0x002b42f0 SpectateGoal_End (unknown)
    void end(Brain& brain) override;

    /// The man he watches (`+0x10`); null when none.
    [[nodiscard]] const Brain* watched() const { return m_watched; }
    /// Its arguments.
    [[nodiscard]] const SpectateArgs& args() const { return m_args; }

  private:
    SpectateArgs m_args;
    bool m_still = false; // the crowd's stand-in: stands still until the time limit
    int m_stillMinMs = 0;
    int m_stillMaxMs = 0;
    Brain* m_watched = nullptr;     // +0x10
    std::uint64_t m_nextLookMs = 0; // +0x14
    std::uint64_t m_nextPickMs = 0; // +0x18
    std::uint64_t m_untilMs = 0;
    bool m_chase = false; // +0x2c
};

} // namespace coney::ai
