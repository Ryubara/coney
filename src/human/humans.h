// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <functional>
#include <span>
#include <utility>
#include <vector>

#include "human/human.h"
#include "human/target_human.h"
#include "raycast/collision_mesh.h"

// The characters' step: every human stepped together in the original's order, each pass over all of them before the
// next (records, brains, animation, state update, actions), so what one human's actions start is seen by every other
// human on the next update, as in the original. Every human is driven through its per-player record: a pad writes the
// player's before the step, and a brain will write an AI human's at the brains' place in it.
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
    /// records, as `Brains_Update` writes their commands (docs/research/ai.md#update). Empty until Coney has brains.
    using BrainsHook = std::function<void(std::span<Human* const> humans)>;

    /// Adds `human` (not owned; it must outlive the step) as the next slot. With `padControlled` a pad writes its
    /// record before each step; otherwise its command is cleared at the step's start, as the original clears a record
    /// no pad drives, and only a brain gives it one.
    void add(Human& human, bool padControlled);
    /// Sets what runs at the brains' place.
    void setBrains(BrainsHook brains) { m_brains = std::move(brains); }

    /// One characters' step, in the original's order (docs/research/tasks.md#humans-update): the records no pad
    /// drives lose their command; the brains write theirs; every human's animation; every human's state update (the
    /// locomotion); every human's actions (the dispatcher from its record, against `targets`, and the anim state),
    /// the order alternating between first-to-last and last-to-first from one step to the next. Then `mesh` is what
    /// they stand on (may be null).
    /// @orig 0x00249108 Humans_Update (unknown)
    void update(const raycast::CollisionMesh* mesh, std::span<TargetHuman* const> targets = {});

    /// The humans, in slot order.
    [[nodiscard]] std::span<Human* const> humans() const { return m_humans; }
    /// Steps run.
    [[nodiscard]] std::uint64_t steps() const { return m_steps; }

  private:
    std::vector<Human*> m_humans;      // not owned
    std::vector<bool> m_padControlled; // per slot
    BrainsHook m_brains;
    std::uint64_t m_steps = 0;
};

} // namespace coney::human
