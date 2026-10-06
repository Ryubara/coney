// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <memory>

#include "ai/goal.h"
#include "human/human.h"

// The reaction goals: what a brain does while its human is in a state a fight puts it in (knocked down, stunned,
// grabbed, grabbing, tackling). One is made from the human's state when none is active, and it holds the goal stack
// off until the state is over; then it ends and the stack's top takes up again. Because the brains run before the
// humans' state update, a reaction goal comes and goes one update after its state (confirmed at runtime).
// Research: docs/research/ai.md#reaction-goals

namespace coney::ai {

/// A reaction goal of one of the reaction types (GoalType::ReactGrabbing ... ReactStunned). **Coney choice**: its
/// work is to hold the stack off; what the original's reaction goals press while they run (a struggle, a mash) is
/// not traced, so they press nothing.
class ReactionGoal final : public Goal {
  public:
    /// A reaction goal of `type`.
    explicit ReactionGoal(GoalType type) : Goal(type) {}

    /// Done once the human's state no longer calls for this type (reactionHolds()).
    [[nodiscard]] GoalStatus process(Brain& brain) override;
};

/// Whether `human`'s state calls for a reaction goal of `type`: grabbing (Coney's grab or mugging mode), tackling,
/// grabbed (in a player's grab, or held by a grabber), tackled (mounted by a tackler), knocked down (on the ground, or
/// out of health), stunned and not down (its stun or the stun's exit).
[[nodiscard]] bool reactionHolds(GoalType type, const human::Human& human);

/// The reaction goal `human`'s state calls for, by the original's order (grabbing `0x12`, tackling `0x13`, grabbed
/// `0x14`, tackled `0x15`, knocked down `0x17`, stunned `0x18`); null when none. **Coney choice**: the types for states
/// Coney's humans do not have (`0x16`, `0x19`, `0x1a`) are not made.
[[nodiscard]] std::unique_ptr<Goal> reactionGoalFor(const human::Human& human);

} // namespace coney::ai
