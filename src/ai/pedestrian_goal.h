// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <memory>
#include <optional>

#include "ai/goal.h"
#include "ai/move_to_flag_goal.h"
#include "world_objects/flag_net.h"

// The pedestrian goal (type 0x69) that `FlagNetTraverse` pushes: a human wandering along the level's flag network.
// Research: docs/references/bindings/world.md#flagnettraverse, docs/research/ai.md#goals

namespace coney::ai {

/// `FlagNetTraverse(human, mode, chance, flagA, flagB)`'s settings.
struct PedestrianOrder {
    int mode = 0;       ///< 0 keeps the current pace; 1 and 6-9 walk; 2 faster; 3 a third pace.
    int chance = 0;     ///< 0-100: a roll below it picks one traversal variant, otherwise another.
    bool flagA = false; ///< Passed to the traversal; meaning not traced.
    bool flagB = false; ///< The same.
};

/// The move gait (MoveRequest::gait) Coney gives a traversal `mode`. **Coney's stand-in**: the page gives the modes'
/// meanings only (inferred from stored speed values): 2 is jog, 3 run, every other mode walk.
[[nodiscard]] int pedestrianGait(int mode);

/// The pedestrian goal. **Coney's stand-in** (its constructor `0x002aae30` is known, its Process is not traced): it
/// walks to the network's node nearest the human, then on to a random linked node of the one it reached, for ever; a
/// node with no links ends the walk there (the goal stays, standing). Each leg is a GoalMoveToFlag to the flag with a
/// 1 m radius whose arrival tells nobody. With no network or no flags, it stands.
/// @orig 0x002aae30 PedestrianGoal_Init (unknown)
class PedestrianGoal final : public Goal {
  public:
    /// A goal walking `net`, finding flags through `flags`; both must outlive it.
    PedestrianGoal(const PedestrianOrder& order, const world_objects::FlagNet& net, FlagServices& flags)
        : Goal(GoalType::Pedestrian), m_order(order), m_net(&net), m_flags(&flags) {}

    /// Heads for the node nearest the human.
    void start(Brain& brain) override;
    /// Takes up the current leg again.
    void resume(Brain& brain) override;
    /// The current leg; once it arrives, the next leg to a random linked node. Never done.
    [[nodiscard]] GoalStatus process(Brain& brain) override;

    /// The flag the current leg goes to; nothing while it stands.
    [[nodiscard]] std::optional<double> heading() const { return m_flag; }

  private:
    // Starts a leg to `flag`.
    void walkTo(Brain& brain, double flag);

    PedestrianOrder m_order;
    const world_objects::FlagNet* m_net;
    FlagServices* m_flags;
    std::unique_ptr<MoveToFlagGoal> m_leg;
    std::optional<double> m_flag;
};

} // namespace coney::ai
