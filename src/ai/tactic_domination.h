// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string>

#include "ai/goal.h"
#include "ai/tactic.h"

// TacticDomination: a gang takes and holds the ground round a flag (king of the hill). Each AI member gets the
// hold-flag goal: run to the flag, stand there, and fight the enemies that come within the tactic's range of it.
// Research: docs/references/bindings/ai.md#tacticdomination, docs/research/rumble.md#game-types

namespace coney::ai {

class FlagServices;
class Gangs;

/// The domination tactic's type id. **Coney choice**: the id is not on the page; it is one of the gang tactics (at
/// least kFirstGangTactic), as it gives its members their goals.
inline constexpr int kDominationTactic = 0x40;
/// The hold-flag goal's type id. **Coney choice**: the id is not on the page.
inline constexpr GoalType kHoldFlagGoal = static_cast<GoalType>(0xf0);
/// A holder stops within this distance of the flag, metres.
inline constexpr float kHoldFlagArrival = 1.0F;
/// The gaits a holder runs to the flag at, and walks at within the range (in the original, in fight stance).
inline constexpr int kHoldFlagRunGait = 5;
inline constexpr int kHoldFlagWalkGait = 2;
/// The events that hand the goals out again (the tactic's event slot, `0x00310d78`).
inline constexpr int kDominationReassignA = 0x13;
inline constexpr int kDominationReassignB = 0x16;

/// The hold-flag goal (`Goal_HoldFlag`, `0x002b9538`). Each update: done once the flag is gone; waits while the human
/// is down or busy with actions; when an enemy of its gang stands within the range of the flag (the nearest one), it
/// takes him as its target and enemy, runs at him beyond its reach (MoveToHumanAction, 1 s at a time) and within it
/// pushes the fight goal; otherwise, farther than kHoldFlagArrival from the flag, it moves there (sprinting beyond the
/// range, walking within it) and else stands. **Coney stand-ins**: the taunt every 3-6 s, the fight stance, the
/// human's `+0xe0` bit 2 and the brain's `+0x0b` are not built, and the 1 s fight goal is the fight goal, which ends
/// when its target leaves its reach.
/// @orig 0x002b9538 Goal_HoldFlag (unknown)
class HoldFlagGoal final : public Goal {
  public:
    /// Holding the flag with `flag`'s handle within `range` metres, found through `flags`, its enemies among the gangs
    /// of `gangs` (both must outlive it).
    HoldFlagGoal(double flag, float range, FlagServices& flags, Gangs& gangs)
        : Goal(kHoldFlagGoal), m_flag(flag), m_range(range), m_flags(&flags), m_gangs(&gangs) {}

    /// @orig 0x002b98f0 HoldFlagGoal_Process (unknown)
    [[nodiscard]] GoalStatus process(Brain& brain) override;

    /// The flag held, and the range.
    [[nodiscard]] double flag() const { return m_flag; }
    [[nodiscard]] float range() const { return m_range; }

  private:
    double m_flag;
    float m_range;
    FlagServices* m_flags;
    Gangs* m_gangs;
};

/// The domination tactic (vtable `0x00543860`).
/// @orig 0x00310be0 DominationTactic_Init (unknown)
class TacticDomination final : public Tactic {
  public:
    /// Holding the flag with `flag`'s handle within `range` metres (found through `flags`, which must outlive it),
    /// calling `callback` (empty for none).
    TacticDomination(double flag, float range, FlagServices& flags, std::string callback)
        : Tactic(kDominationTactic, std::move(callback)), m_flag(flag), m_range(range), m_flags(&flags) {}

    /// Every AI member that is not down gets the hold-flag goal.
    /// @orig 0x00310c68 DominationTactic_AssignGoals (unknown)
    void start(Gang& gang) override;
    /// Events kDominationReassignA and kDominationReassignB hand the goals out again (to members without one).
    /// @orig 0x00310d78 DominationTactic_OnEvent (unknown)
    bool event(Gang& gang, Brain& member, const BrainEvent& event) override;

  private:
    double m_flag;
    float m_range;
    FlagServices* m_flags;
};

} // namespace coney::ai
