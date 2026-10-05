// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "debug/tunables.h"

namespace coney::debug {

/// Registers combat's tunables in `registry` under the category **Combat**, each over the live value in
/// combat::combatTuning() and defaulting to its researched value (src/combat/combat_tuning.h,
/// docs/research/combat.md#constants): the history hold, snap attacks, the chain timing, the grab search, the power
/// costs and drain, the rage points and drain, the button mash, the mugging and the stereo theft.
///
/// Call it once at start-up, beside registerGameTunables(); the values live as long as the program. Unregister with
/// TunableRegistry::removeCategory("Combat").
void registerCombatTunables(TunableRegistry& registry);

} // namespace coney::debug
