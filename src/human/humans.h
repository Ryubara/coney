// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <functional>
#include <span>
#include <utility>
#include <vector>

#include "human/combatant.h"
#include "human/human.h"
#include "raycast/collision_mesh.h"

// The characters' step: every human stepped together in the original's order, each pass over all of them before the
// next (records, brains, animation, state update, actions), so what one human's actions start is seen by every other
// human on the next update, as in the original. Every human is driven through its per-player record: a pad writes the
// player's before the step, and a brain writes an AI human's at the brains' place in it (src/ai/brains.h).
// Research: docs/research/tasks.md#humans-update

namespace coney::human {

/// The humans a level steps, and the characters' step over them.
///
/// **Coney choice**: the step runs once per fixed 1/30 s update. The original calls it on every 60 Hz tick of its task
/// manager and works on every second one (docs/research/tasks.md#tick); with Coney's fixed step the result is the
/// same for the humans. The 60 Hz tick and the timing wheel that steps the world's other objects are left for when
/// those objects (doors, pick-ups, cars) come.
class Humans {
  public:
    /// What runs at the brains' place in the step (after the records, before the animation): it writes the AI humans'
    /// records, as `Brains_Update` writes their commands (docs/research/ai.md#update); ai::Brains::hook() gives one.
    using BrainsHook = std::function<void(std::span<Human* const> humans)>;

    /// Adds `human` (not owned; it must outlive the step, or be removed) as the next slot. With `padControlled` a pad
    /// writes its record before each step; otherwise its command and buttons are cleared at the step's start, as the
    /// original clears the command of a record no pad drives, and only a brain gives it one. `side` is who it fights:
    /// a human on side 0 (the player's) fights the step's passive targets and every human of another side, a human on
    /// another side every human not on its own. **Coney choice**, standing in for the gangs and their enemies
    /// (`GangMakeEnemies`, docs/research/ai.md#level99), which are not built.
    void add(Human& human, bool padControlled, int side = 0);
    /// Takes `human` out of the step (a spawned AI cleared away); nothing when it is not in it.
    void remove(const Human& human);
    /// Sets what runs at the brains' place.
    void setBrains(BrainsHook brains) { m_brains = std::move(brains); }

    /// One characters' step, in the original's order (docs/research/tasks.md#humans-update): the records no pad
    /// drives lose their command; the brains write theirs; every human's animation; every human's state update (the
    /// locomotion); every human's actions (the dispatcher from its record, against the passive `targets` and the
    /// humans of the other sides, as add() says), the order alternating between first-to-last and last-to-first from
    /// one step to the next. `mesh` is what they stand on (may be null).
    /// @orig 0x00249108 Humans_Update (unknown)
    void update(const raycast::CollisionMesh* mesh, std::span<Combatant* const> targets = {});

    /// The humans, in slot order.
    [[nodiscard]] std::span<Human* const> humans() const { return m_humans; }
    /// Steps run.
    [[nodiscard]] std::uint64_t steps() const { return m_steps; }

  private:
    // Who slot `slot` fights this step: the passive `targets` on side 0, and the humans of the other sides.
    void gatherTargets(std::size_t slot, std::span<Combatant* const> targets);

    std::vector<Human*> m_humans;      // not owned
    std::vector<bool> m_padControlled; // per slot
    std::vector<int> m_sides;          // per slot
    std::vector<Combatant*> m_scratch; // gatherTargets()'s list
    BrainsHook m_brains;
    std::uint64_t m_steps = 0;
};

} // namespace coney::human
