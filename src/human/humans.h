// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
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
    /// Holds `human` still, or lets it go (the Entity inspector's freeze, Coney's own debug tooling): a held human
    /// keeps its record, state and place, and the step skips its animation, state update and actions; its brain still
    /// runs and the others still see and hit it. Nothing when it is not in the step.
    void setHeld(const Human& human, bool held);
    /// Whether `human` is held (setHeld()); false for one not in the step.
    [[nodiscard]] bool held(const Human& human) const;
    /// Sets who fights whom. Without one (the default) the humans added pad-controlled and the others fight each other,
    /// as the player and the AI humans of a scene without gangs do.
    void setOpposition(Opposition opposition) { m_opposition = std::move(opposition); }
    /// Takes `human` out of the step (a spawned AI cleared away); nothing when it is not in it.
    void remove(const Human& human);
    /// Sets what runs at the brains' place.
    void setBrains(BrainsHook brains) { m_brains = std::move(brains); }
    /// Sets what an airborne human's body touches (Human::BodyContact), given to every human it steps; empty: nothing.
    void setBodyContact(Human::BodyContact contact) { m_bodyContact = std::move(contact); }
    /// Sets what a walking human slides along beyond the walls (Human::ObjectPush), given to every human it steps;
    /// empty: nothing.
    void setObjectPush(Human::ObjectPush push) { m_objectPush = std::move(push); }
    /// Sets what a human's switched-on strike shapes meet beyond the humans (Human::testStrikes()); empty: nothing.
    void setStrikeContact(Human::StrikeContact contact) { m_strikeContact = std::move(contact); }

    /// Sets whose face shows (FaceRules::shown); without a rule (the default) every face shows.
    void setFaceShown(std::function<bool(const Human& human)> shown) { m_faceRules->shown = std::move(shown); }
    /// Draws the face clips from the game's random table (GameRandom::kTableSize numbers), their own stream.
    void setRandomTable(std::span<const std::uint32_t> table) { m_faceRules->random.setTable(table); }

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
    std::vector<bool> m_held;          // per slot: setHeld()
    Opposition m_opposition;
    std::vector<Combatant*> m_scratch; // gatherTargets()'s list
    BrainsHook m_brains;
    Human::BodyContact m_bodyContact;
    Human::ObjectPush m_objectPush;
    Human::StrikeContact m_strikeContact;
    std::vector<Human*> m_victims;     // the strike test's list
    std::vector<Human*> m_contactList; // the humans the walking bodies meet this step (Human::setHumanContacts())
    std::uint64_t m_steps = 0;
    // The rules every added human's face follows; on the heap, so a moved Humans keeps them where the humans point.
    std::unique_ptr<FaceRules> m_faceRules = std::make_unique<FaceRules>();
};

} // namespace coney::human
