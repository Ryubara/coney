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

    /// Whether one human fights another: `opposed(attacker, victim)`. The gangs give it (ai::Gangs::friends(), whose
    /// friends do not fight each other).
    using Opposition = std::function<bool(const Human& attacker, const Human& victim)>;

    /// Adds `human` (not owned; it must outlive the step, or be removed) as the next slot. With `padControlled` a pad
    /// writes its record before each step; otherwise its command and buttons are cleared at the step's start, as the
    /// original clears the command of a record no pad drives, and only a brain gives it one. A human added
    /// pad-controlled also fights the step's passive targets (the sandbox's dummies, which have no gang).
    void add(Human& human, bool padControlled);
    /// Takes the pad from `human` or gives it back (a player's brain set dead takes it, `BrDead`); nothing when it is
    /// not in the step.
    void setPadControlled(const Human& human, bool padControlled);
    /// Sets who fights whom. Without one (the default) the humans added pad-controlled and the others fight each other,
    /// as the player and the AI humans of a scene without gangs do.
    void setOpposition(Opposition opposition) { m_opposition = std::move(opposition); }
    /// Takes `human` out of the step (a spawned AI cleared away); nothing when it is not in it.
    void remove(const Human& human);
    /// Sets what runs at the brains' place.
    void setBrains(BrainsHook brains) { m_brains = std::move(brains); }
    /// Sets what an airborne human's body touches (Human::BodyContact), given to every human it steps; empty: nothing.
    void setBodyContact(Human::BodyContact contact) { m_bodyContact = std::move(contact); }
    /// Sets what a human's switched-on strike shapes meet beyond the humans (Human::testStrikes()); empty: nothing.
    void setStrikeContact(Human::StrikeContact contact) { m_strikeContact = std::move(contact); }

    /// One characters' step, in the original's order (docs/research/tasks.md#humans-update): the records no pad
    /// drives lose their command; the brains write theirs; every human's animation; every human's state update (the
    /// locomotion, then its strike test against the humans it is opposed to and setStrikeContact()'s objects); every
    /// human's actions (the dispatcher from its record, against the passive `targets` and the humans it is opposed to,
    /// as add() and setOpposition() say), the order alternating between first-to-last and last-to-first from one step
    /// to the next. `mesh` is what they stand on (may be null). Every human advances by `stepSeconds`, the characters'
    /// step, which slow motion shortens (docs/research/camera.md#slow-motion).
    /// @orig 0x00249108 Humans_Update (unknown)
    void update(const raycast::CollisionMesh* mesh, std::span<Combatant* const> targets = {},
                float stepSeconds = kStepSeconds);

    /// The humans, in slot order.
    [[nodiscard]] std::span<Human* const> humans() const { return m_humans; }
    /// Steps run.
    [[nodiscard]] std::uint64_t steps() const { return m_steps; }

  private:
    // Who slot `slot` fights this step: the passive `targets` for a human added pad-controlled, and the humans it is
    // opposed to.
    void gatherTargets(std::size_t slot, std::span<Combatant* const> targets);
    // Whether slot `slot` fights slot `other`.
    [[nodiscard]] bool opposed(std::size_t slot, std::size_t other) const;

    std::vector<Human*> m_humans;      // not owned
    std::vector<bool> m_padControlled; // per slot
    std::vector<bool> m_fightsTargets; // per slot: added pad-controlled
    Opposition m_opposition;
    std::vector<Combatant*> m_scratch; // gatherTargets()'s list
    BrainsHook m_brains;
    Human::BodyContact m_bodyContact;
    Human::StrikeContact m_strikeContact;
    std::vector<Human*> m_victims; // the strike test's list
    std::uint64_t m_steps = 0;
};

} // namespace coney::human
