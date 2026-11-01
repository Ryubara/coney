// SPDX-License-Identifier: GPL-3.0-or-later
// A stub: the signatures with default results, so the tests compile and fail (the fails-first test commit).
#include "console/command_queue.h"

namespace coney::console {

std::vector<QueuedCommand> CommandQueue::take() { return {}; }

} // namespace coney::console
