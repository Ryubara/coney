// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>

#include "ai/action.h"
#include "animation/anim_math.h"

// Giving way: a standing AI in a mover's way steps one sector (45°) aside. Brain_PushAside guards the recursion and
// Brain_GiveWayTo tests who may give way, picks a free sector of the stander's record and queues a GiveWay action, or
// asks the human in each of those sectors to make room in turn. The step is the TakeStep action's: the human's step
// control plays one step clip on the sector's heading (human::Human::enterStepControl()).
// Research: docs/research/ai.md#giving-way, docs/research/characters.md#step-control

namespace coney::ai {

class Brain;

/// A GiveWay action starts after a random 0 to this, less one, ms.
inline constexpr int kGiveWayDelayRange = 125;
/// A script's ActGiveWay acts only on a human with fewer than this many queued actions.
inline constexpr std::size_t kGiveWayQueueLimit = 8;

/// Whether `other` is a threat to `brain`: their gangs are enemies, or both are players. **Coney stand-in**: a
/// Warrior's mode (`+0x2e5`), which can force it either way, is not read.
/// @orig 0x00222a48 Human_IsThreatTo (unknown)
[[nodiscard]] bool isThreat(const Brain& brain, const Brain& other);

/// Asks `stander` to give way to `mover`, whose path is the line through `point` along `step`: yes at once when the
/// stander already gives way; no while he is inside a push-aside; otherwise giveWayTo() with both marked meanwhile.
/// @orig 0x0028a248 Brain_PushAside (unknown)
bool pushAside(Brain& mover, Brain& stander, anim::Vec3 point, anim::Vec3 step);

/// The give-way itself. Only an AI's brain (or a "dead" one, `BrDead`), neither human a threat to the other, the
/// stander idle under its control. The first free sector of kGiveWayOrder round the one straight off the mover's line
/// (steering.h) gets a GiveWay action on its heading, after 0-124 ms, dashing from a player at a jog or faster: the
/// stander's actions are cleared first, and when one refuses the answer is yes with nothing queued. With no free
/// sector, the human nearest in each of those sectors in turn is asked to make room (pushAside() from the stander's
/// own position); the first yes is the answer.
/// @orig 0x00289ed0 Brain_GiveWayTo (unknown)
bool giveWayTo(Brain& mover, Brain& stander, anim::Vec3 point, anim::Vec3 step);

/// One step on a heading: the step control on the human, done when its clip has played.
/// @orig 0x002fe380 TakeStepAction_Init (unknown)
class TakeStepAction : public Action {
  public:
    /// A step toward `heading` (radians, 0 facing +y) after `delayMs`.
    explicit TakeStepAction(float heading, std::int16_t delayMs = 0) : Action(delayMs), m_heading(heading) {}

    /// Stops the brain's move and enters the human's step control on the heading with the brain's turn boost.
    /// **Coney's reading**: the heading goes to the step control directly rather than through the brain's heading
    /// (`+0x110`), which the human's own control would otherwise turn to once the step ends.
    /// @orig 0x002fe3a8 TakeStepAction_Start (unknown)
    [[nodiscard]] ActionStatus start(Brain& brain) override;
    /// Running while the step control waits or a step or turn clip holds the record (`0x20080000`); then done.
    /// @orig 0x002fe428 TakeStepAction_Update (unknown)
    [[nodiscard]] ActionStatus update(Brain& brain) override;
    /// Leaves the step control; refused while the step's clip holds the record. **Coney choice**: every abort is
    /// the polite one (no forced abort reaches an action).
    /// @orig 0x002fe3d8 TakeStepAction_Abort (unknown)
    [[nodiscard]] bool abort(Brain& brain) override;

    [[nodiscard]] float heading() const { return m_heading; }

  private:
    float m_heading;
};

/// A step aside for a mover: the brain's give-way mark (`+0xcc` bit 1) while it lives, and with its boost flag a turn
/// boost raised by one for the step, so a player at a jog or faster is dashed from.
/// @orig 0x002fe568 GiveWayAction_Init (unknown)
class GiveWayAction final : public TakeStepAction {
  public:
    /// A step for `owner` toward `heading` from `mover` (kept to look at), dashing with `boost`, after `delayMs`.
    GiveWayAction(Brain& owner, float heading, const Brain& mover, bool boost, std::int16_t delayMs);
    GiveWayAction(const GiveWayAction&) = delete;
    GiveWayAction& operator=(const GiveWayAction&) = delete;
    GiveWayAction(GiveWayAction&&) = delete;
    GiveWayAction& operator=(GiveWayAction&&) = delete;
    /// Clears the brain's giving-way mark.
    /// @orig 0x002fe6a8 GiveWayAction_Destroy (unknown)
    ~GiveWayAction() override;

    /// With the boost flag the turn boost is saved and raised by one; then the step starts. **Coney stand-in**: the
    /// look at the mover for 2000 ms is not made (the head look-ats are not built).
    /// @orig 0x002fe5d8 GiveWayAction_Start (unknown)
    [[nodiscard]] ActionStatus start(Brain& brain) override;
    /// The step's update.
    /// @orig 0x002fe6c8 GiveWayAction_Update (unknown)
    [[nodiscard]] ActionStatus update(Brain& brain) override;
    /// The step's abort; on success the boost is restored. A finished action is aborted too before it is freed
    /// (Brain::finishAction()), so this is the one place the boost goes back.
    /// @orig 0x002fe658 GiveWayAction_Abort (unknown)
    [[nodiscard]] bool abort(Brain& brain) override;

    [[nodiscard]] const Brain* mover() const { return m_mover; }
    [[nodiscard]] bool boost() const { return m_boost; }

  private:
    // Puts the turn boost back when this raised it.
    void restoreBoost(Brain& brain);

    Brain* m_owner;
    const Brain* m_mover;
    bool m_boost;
    bool m_raised = false;
    int m_savedBoost = 0;
};

} // namespace coney::ai
