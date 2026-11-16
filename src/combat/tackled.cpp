// SPDX-License-Identifier: GPL-3.0-or-later
#include "combat/tackled.h"

namespace coney::combat {

// Stub for the test commit: no struggle is allowed.
bool mountStruggleAllowed(const TackledInput& /*input*/) { return false; }

// Stub for the test commit: the mounted human does nothing, as before.
TackledOutcome updateTackled(const TackledInput& /*input*/, CombatRandom& /*random*/) { return {}; }

} // namespace coney::combat
