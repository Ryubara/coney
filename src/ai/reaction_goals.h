// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <memory>

#include "ai/goal.h"
#include "human/human.h"

// The reaction goals: what a brain does while its human is in a state a fight puts it in (knocked down, stunned,
// grabbed, grabbing, tackling). One is made from the human's state when none is active, and it holds the goal stack
// off until the state is over; then it ends and the stack's top takes up again. Because the brains run before the
// humans' state update, a reaction goal comes and goes one update after its state (confirmed at runtime).
// Research: docs/research/ai/index.md#reaction-goals

namespace coney::ai {

/// A reaction goal that only holds the stack off: the stunned goal (`0x18`, `StunnedGoal_Process`), which presses
/// nothing and is done once the human is no longer stunned standing. **Gap** (#685): the original's also drops the
/// brain's target on every update while stunned; Coney keeps it, since its fight goal resumes on brain.target()
/// (FightGoal::resume) and how the original's finds its target again on the update the stun ends (the `civ_stun`
/// trace's move queued at once) is not traced. The fight reactions (grabbing, mounting, grabbed, mounted, grounded)
/// are in ai/fight_reactions.h.
class ReactionGoal final : public Goal {
  public:
    /// A reaction goal of `type`.
    explicit ReactionGoal(GoalType type) : Goal(type) {}

    /// Done once the human's state no longer calls for this type (reactionHolds()).
    [[nodiscard]] GoalStatus process(Brain& brain) override;
};

/// Whether `human`'s state calls for a reaction goal of `type`: grabbing (Coney's grab or mugging mode), tackling,
/// grabbed (in a player's grab, or held by a grabber), tackled (mounted by a tackler), knocked down (on the ground, or
/// out of health), stunned and not down (until the stun's end clip 357 starts).
/// **Coney reading**: Coney's fighter has no state word, so each state is read from its modes and timers.
/// Research: docs/research/combat-reactions.md#stun-end
/// @orig 0x0028f2b0 Brain_UpdateReactionGoal (unknown)
/// @orig 0x002b4bb8 StunnedGoal_Process (unknown)
[[nodiscard]] bool reactionHolds(GoalType type, const human::Human& human);

/// Whether `human` is busy (`Human_IsBusy`, docs/research/characters.md): a busy bit of his record's
/// `+0x08` (human::kBusyFlags), in the air, or a busy state: grabbed or mugged, tackling, tackled, down or out of
/// health, stunned. Grabbing is not busy. **Coney reading**: the state word's bits (`0x79b9e1e0f30`) are read from the
/// fighter's states, as the reaction goals read them. A hit also keeps an AI busy for his power class's `+0x32` ms
/// (record `+0x108`, human::Fighter::busyUntilMs(), docs/research/characters.md#power-class-bytes).
/// @orig 0x00228258 Human_IsBusy (unknown)
[[nodiscard]] bool humanBusy(const human::Human& human);

/// The reaction goal `human`'s state calls for, by the original's order (grabbing `0x12`, tackling `0x13`, grabbed
/// `0x14`, tackled `0x15`, knocked down `0x17`, stunned `0x18`); null when none. **Coney choice**: the types for states
/// Coney's humans do not have (`0x16`, `0x19`, `0x1a`) are not made.
[[nodiscard]] std::unique_ptr<Goal> reactionGoalFor(const human::Human& human);

} // namespace coney::ai
