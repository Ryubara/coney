// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

// A brain's goal: a long-lived intention (fight this human, block for a while, lie down until the knockdown is over)
// that a brain keeps on its stack of ten, or as its reaction goal. The brain drives it (ai::Brain): it starts the goal,
// or resumes it after the goal above it was popped, only when its action queue is empty, then processes it.
// Research: docs/research/ai.md#goals, docs/research/ai.md#update-goals

namespace coney::ai {

class Brain;

/// The goal types Coney builds, by the original's type ids (the vtable's `+0x0c`, docs/research/ai.md#goals).
enum class GoalType : std::uint8_t {
    Fight = 0x0f,            ///< FightGoal.
    ReactGrabbing = 0x12,    ///< Reaction: grabbing (state `0xc0`).
    ReactTackling = 0x13,    ///< Reaction: tackling (`0x400`).
    ReactGrabbed = 0x14,     ///< Reaction: grabbed (`0x30`) or mugged (`0x200`).
    ReactTackled = 0x15,     ///< Reaction: tackled (`0x800`).
    ReactKnockedDown = 0x17, ///< Reaction: knocked down (`0x80000`).
    ReactStunned = 0x18,     ///< Reaction: stunned (`0x100000`) and not down.
    Block = 0x1b,            ///< BlockGoal.
};

/// What a goal's process() returns (`Goal_Process`): stop for this update, process the stack's top again in the same
/// update, or done (the brain pops it and processes the new top in the same update).
enum class GoalStatus : std::uint8_t {
    Stop = 0,
    Again = 1,
    Done = 2,
};

/// A goal. Subclasses override what they do; every hook but process() does nothing by default.
class Goal {
  public:
    /// A goal of `type`, not started.
    explicit Goal(GoalType type) : m_type(type) {}
    Goal(const Goal&) = delete;
    Goal& operator=(const Goal&) = delete;
    Goal(Goal&&) = delete;
    Goal& operator=(Goal&&) = delete;
    virtual ~Goal() = default;

    /// Its type id.
    [[nodiscard]] GoalType type() const { return m_type; }
    /// Whether it has been started (`+0x04`).
    [[nodiscard]] bool started() const { return m_started; }

    /// Starts it (vtable `+0x24`), once, with the action queue empty.
    virtual void start(Brain& /*brain*/) {}
    /// Takes it up again after the goal above it was popped (`+0x34`), with the action queue empty.
    virtual void resume(Brain& /*brain*/) {}
    /// A goal is pushed above it (`Goal_Suspend`).
    virtual void suspend(Brain& /*brain*/) {}
    /// It is popped or flushed (`+0x2c`).
    virtual void end(Brain& /*brain*/) {}
    /// One update's work (`+0x44`).
    [[nodiscard]] virtual GoalStatus process(Brain& brain) = 0;

  private:
    friend class Brain;

    GoalType m_type;
    bool m_started = false;       // +0x04
    bool m_resumePending = false; // the goal above it was popped: resume() before the next process()
};

} // namespace coney::ai
