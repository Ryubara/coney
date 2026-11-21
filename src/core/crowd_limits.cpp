// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/crowd_limits.h"

namespace coney::crowd {

// Stub for the tests written first (issue 1569): the original's numbers at any crowd size.
int scanTokens(std::size_t /*scanningBrains*/) { return kOrigScanTokens; }

// Stub for the tests written first (issue 1569): the original's numbers at any crowd size.
std::size_t humanListCap(std::size_t /*humans*/) { return kOrigMaxHumans; }

} // namespace coney::crowd
