// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

// A brain's action: a short step (press one attack, walk to the target) in its queue of eight. Only the front action
// runs: once its delay is over the brain starts it and updates it in the same update, and pops it when it is done.
// Research: docs/research/ai.md#actions, docs/research/ai.md#update-goals

namespace coney::ai {

class Brain;

/// What an action's start() and update() return: still running, or done (2; the brain pops it).
enum class ActionStatus : std::uint8_t {
    Running = 0,
    Done = 2,
};

/// A delay that means "a random 0-500 ms" (`+0x04` = -1).
inline constexpr std::int16_t kRandomDelay = -1;

/// An action. Subclasses override update(), and start() and abort() when they need to.
class Action {
  public:
    /// An action that starts `delayMs` milliseconds after it reaches the front of the queue (kRandomDelay: 0-500).
    explicit Action(std::int16_t delayMs = 0) : m_delayMs(delayMs) {}
    Action(const Action&) = delete;
    Action& operator=(const Action&) = delete;
    Action(Action&&) = delete;
    Action& operator=(Action&&) = delete;
    virtual ~Action() = default;

    /// Starts it (vtable `+0x14`); Done means there was nothing to do.
    [[nodiscard]] virtual ActionStatus start(Brain& /*brain*/) { return ActionStatus::Running; }
    /// One update's work (`+0x2c`).
    [[nodiscard]] virtual ActionStatus update(Brain& brain) = 0;
    /// Asked to stop (`+0x1c`): false refuses, and the action stays (an attack in progress is never cut).
    [[nodiscard]] virtual bool abort(Brain& /*brain*/) { return true; }

    /// The delay left before it starts, ms (`+0x04`).
    [[nodiscard]] std::int16_t delayMs() const { return m_delayMs; }
    /// Whether it has started (`+0x06`).
    [[nodiscard]] bool started() const { return m_started; }

  private:
    friend class Brain;

    std::int16_t m_delayMs;
    bool m_started = false;
};

} // namespace coney::ai
