// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "console/registry.h"

// @coney-only the Coney console's step-boundary queue (docs/guides/console.md#queue); the original has no console.

namespace coney::console {

/// A checked line waiting for the step boundary: its command by id, its arguments with the defaults filled in, the
/// slot it is for and the line as entered (for the run log).
struct QueuedCommand {
    CommandId id;
    std::vector<std::string> args;
    PlayerSlot slot = 0;
    std::string line;
};

/// The commands that change the game, waiting to run between two simulation steps in the order they were entered, so
/// a step never sees the game change half-way (docs/guides/studio/console.md#terminal). Input, as a pad is: a replay
/// would record each with the step it ran at.
class CommandQueue {
  public:
    /// Adds `command` after those already waiting.
    void push(QueuedCommand command) { m_waiting.push_back(std::move(command)); }
    /// Takes every waiting command, oldest first, leaving the queue empty: a command one of them queues waits for the
    /// next boundary.
    [[nodiscard]] std::vector<QueuedCommand> take();
    /// How many wait.
    [[nodiscard]] std::size_t size() const { return m_waiting.size(); }

  private:
    std::vector<QueuedCommand> m_waiting;
};

} // namespace coney::console
