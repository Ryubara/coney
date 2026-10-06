// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>
#include <string_view>

#include "ai/goal.h"

// GoalPlayDynAnimation: the human plays a level-loaded clip in its dynamic animation slot (anim 668) once it is free,
// then the goal ends and calls its script back with whether the clip played to its end. A goal pushed above it
// interrupts it for good.
// Research: docs/research/ai.md#dyn-animation, docs/research/ai.md#scripted

namespace coney::ai {

class ScriptServices;

/// Record (`+0x08`) flags the goal waits on before it plays: any but this one.
inline constexpr std::uint32_t kDynAnimationIgnoredFlag = 0x40000000;

/// The play-dynamic-animation goal (type `0x22`).
/// @orig 0x002d2eb0 PlayDynAnimationGoal_Init (unknown)
class PlayDynAnimationGoal final : public Goal {
  public:
    /// A goal that plays the clip already asked for in the brain's slot, calls `callback` (empty for none) back
    /// through `services` (which must outlive it), and gives its play-anim action `option`.
    PlayDynAnimationGoal(ScriptServices& services, std::string callback, bool option)
        : Goal(GoalType::PlayDynAnimation), m_services(&services), m_callback(std::move(callback)), m_option(option) {}

    /// **Coney choice**: the original sets bit `0x10000` of the human's state word here (`0x002d2f60`); Coney has no
    /// state word, so nothing is set.
    /// @orig 0x002d2f60 PlayDynAnimationGoal_Start (unknown)
    void start(Brain& brain) override;
    /// A goal pushed above it: interrupted.
    /// @orig 0x002d2f88 PlayDynAnimationGoal_Suspend (unknown)
    void suspend(Brain& brain) override;
    /// Done when interrupted or the human is not on its feet (**Coney choice** standing in for the state test
    /// `0x7bf9e9f7ff0`); waits while actions are queued; until it has queued its clip, waits for the clip to load and
    /// for the record to hold nothing but kDynAnimationIgnoredFlag, then queues the play-anim action (anim 668); once
    /// that action has ended, completed and done.
    /// @orig 0x002d3060 PlayDynAnimationGoal_Process (unknown)
    [[nodiscard]] GoalStatus process(Brain& brain) override;
    /// Schedules the callback with (handle, completed) and frees the slot. **Coney choice**: the slot is freed here,
    /// not in the destructor, which has no brain (the original frees it in Destroy, right after End).
    /// @orig 0x002d2f98 PlayDynAnimationGoal_End (unknown)
    void end(Brain& brain) override;

    /// Whether the clip played to its end (`+0x15`), and whether a goal above interrupted it (`+0x14`).
    [[nodiscard]] bool completed() const { return m_completed; }
    [[nodiscard]] bool interrupted() const { return m_interrupted; }

  private:
    ScriptServices* m_services;
    std::string m_callback; // +0x10
    bool m_option;          // +0x17
    bool m_interrupted = false;
    bool m_completed = false;
    bool m_queued = false; // +0x16
};

/// What `GoalPlayDynAnimation(human, anim, callback, option)` does for `brain`: nothing for an empty name; otherwise
/// asks for clip `name` in the brain's slot and pushes the goal. Returns whether it was pushed.
/// @orig 0x002d2df8 Goal_PlayDynamicAnimation (unknown)
bool goalPlayDynAnimation(Brain& brain, ScriptServices& services, std::string_view name, std::string callback,
                          bool option);

/// Schedules `callback` (nothing when empty) with `brain`'s handle and `completed` kGoalCallbackDelayMs from now: how
/// a goal calls its script back from its End.
void scheduleGoalCallback(ScriptServices& services, const Brain& brain, std::string_view callback, bool completed);

} // namespace coney::ai
