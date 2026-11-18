// SPDX-License-Identifier: GPL-3.0-or-later
#include "console/command_queue.h"

#include <utility>

namespace coney::console {

std::vector<QueuedCommand> CommandQueue::take() { return std::exchange(m_waiting, {}); }

} // namespace coney::console
