// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "ai/goal.h"
#include "human/human.h"

// The fight goal (type `0xf`): while it is on top, each update it checks its target, tries to block an attack
// announced on its human, waits for its actions and its pacing, picks an attack by weight, and either walks to the
// target or presses the attack.
// Research: docs/research/ai.md#fight

namespace coney::ai {

/// The fight goal's attack timer: a random 750-1000 ms (plus the goal's parameter) between attacks.
inline constexpr int kAttackTimerMinMs = 750;
inline constexpr int kAttackTimerMaxMs = 1000;
/// The fight goal ends when the target is farther than the far melee range × this.
inline constexpr float kFightRangeScale = 1.1F;
/// The move-to-human action's limit: the longer one when the target is farther than twice the reach (**Coney
/// choice** of which limit when; the research gives "1000 or 2000 ms").
inline constexpr int kShortMoveMs = 1000;
inline constexpr int kLongMoveMs = 2000;
/// A target is in reach within this share of the attack's far range: **Coney choice** standing in for `0x00230d00`
/// (not traced): the attack's own target search finds a target at any angle within 0.9 × the far range.
inline constexpr float kInReachShare = 0.9F;

/// The fight goal.
/// @orig 0x002b2c20 FightGoal_Init (unknown)
class FightGoal final : public Goal {
  public:
    /// A fight goal whose attack timer waits `extraDelayMs` more at its start (`GoalFight`'s parameter is unused, 0).
    explicit FightGoal(std::uint64_t extraDelayMs = 0) : Goal(GoalType::Fight), m_extraDelayMs(extraDelayMs) {}

    /// Sets the attack timer (`+0x24`): now + a random 750-1000 ms + the parameter.
    void start(Brain& brain) override;
    /// One update, in the original's order (docs/research/ai.md#fight): no target, no attack slot or a target beyond
    /// the far melee range × 1.1 ends it; once a second it re-targets; an announced attack may push the block goal (the
    /// stack's new top is processed at once); while actions are queued it waits; while the attack timer, the brain's
    /// next attack (`+0x1e8`) or the target's `+0x1ec` say no it waits; then it picks an attack by weight and walks to
    /// the target when it is out of that attack's reach, else queues the attack and sets the timer again.
    /// **Coney choices**: the update every 30 (`0x002221b0`), the fight stance and the taunt are not built (not
    /// traced), and the reposition when nothing else applies is standing still.
    /// @orig 0x002b3ab0 FightGoal_Process (unknown)
    [[nodiscard]] GoalStatus process(Brain& brain) override;

    /// When the attack timer allows the next attack, ms.
    [[nodiscard]] std::uint64_t attackAtMs() const { return m_attackAtMs; }

  private:
    std::uint64_t m_extraDelayMs;
    std::uint64_t m_attackAtMs = 0;   // +0x24
    std::uint64_t m_retargetAtMs = 0; // the once-a-second re-target
};

/// **Coney's stand-in** for the melee goal (type 8, `Goal_Melee`) that `Brain_PushFightGoal` pushes beneath the fight
/// goal (docs/research/ai.md#riot: goals `0x41` and 8, then the fight goal); its Process is not traced. It keeps a
/// `GoalFight` going whatever the distance: the fight goal ends once its target is beyond the far melee range × 1.1,
/// and then this goal, back on top, runs the human at its target (MoveToHumanAction, 2 s at a time) until it is within
/// that range and pushes the fight goal again. It waits while the human or the target is down, while actions are queued
/// and while the target has no attack slot for it; it ends when there is no target or the target is out of health.
/// Without it, `level99`'s sparring Warriors, sent from 8.6-9 m (docs/research/ai.md#level99-fight), stood still.
class CloseInGoal final : public Goal {
  public:
    CloseInGoal() : Goal(GoalType::Melee) {}

    /// One update: wait, run at the target, or push the fight goal, as above.
    [[nodiscard]] GoalStatus process(Brain& brain) override;
};

/// Queues attack kind `kind` as its chain of attack actions (ai::chainOf()), each later press delayed by the chain
/// delay of the press before (ai::chainDelayMs() in the human's anims).
/// @orig 0x0028e248 Brain_QueueAttack (unknown)
void queueAttack(Brain& brain, int kind);

/// The distance within which `human` can press attack `kind` at a target: kInReachShare × the far range of the kind's
/// first attack (ai::firstAnimOf()) in its Anim Range List, or of human::kDefaultStrikeReach without one.
[[nodiscard]] float attackReach(const human::Human& human, int kind);

} // namespace coney::ai
