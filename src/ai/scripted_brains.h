// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <map>
#include <optional>

#include "ai/brain.h"
#include "ai/move_to_flag_goal.h"
#include "animation/anim_math.h"
#include "scripting/ai_bindings.h"
#include "world_objects/flags.h"

// The level scripts' hold on brains: which brain each human handle names, and what the AI bindings do with it. It
// gives GoalMoveToFlag its flags (the level's world flags) and finds what ActLookAt turns to (a human of a brain, a
// flag, or any object the locator knows).
// Research: docs/research/ai.md#scripted

namespace coney::ai {

/// The brains the scripts drive by handle.
class ScriptedBrains final : public script::AiBindingHost, public FlagServices {
  public:
    /// Brains found by handle, with `flags` the level's world flags and `locate` (may be empty) for other objects;
    /// both must outlive it.
    ScriptedBrains(const world_objects::WorldFlags& flags, world_objects::ObjectLocator locate = {});

    /// Names `brain` (which must outlive the binding, or be unbound first) by `handle`.
    void bind(double handle, Brain& brain) { m_brains[handle] = &brain; }
    /// Forgets the brain named by `handle`.
    void unbind(double handle) { m_brains.erase(handle); }
    /// The brain named by `handle`; null when none.
    [[nodiscard]] Brain* brain(double handle) const;
    /// Where the object with `handle` is: a bound brain's human, a flag, or what the locator finds; nothing when
    /// none is.
    [[nodiscard]] std::optional<anim::Vec3> locate(double handle) const;

    /// Pushes the move-to-flag goal on the human's brain; nothing for a handle with no brain.
    void goalMoveToFlag(const script::MoveToFlagCall& call) override;
    /// Queues the look-at on the human's brain, its target found by locate(); nothing for a handle with no brain.
    /// @orig 0x002fe0c8 Action_LookAt (unknown)
    void actLookAt(const script::LookAtCall& call) override;

    /// The flag with `handle`, where its parent (if live) puts it.
    [[nodiscard]] std::optional<world_objects::Placement> flag(double handle) const override;
    /// Counts the arrival: Coney's flags take no message 8 yet.
    void arrived(double handle, Brain& user) override;
    /// Arrivals at flags so far.
    [[nodiscard]] std::size_t arrivals() const { return m_arrivals; }

  private:
    const world_objects::WorldFlags* m_flags;
    world_objects::ObjectLocator m_locate;
    std::map<double, Brain*> m_brains;
    std::size_t m_arrivals = 0;
};

} // namespace coney::ai
