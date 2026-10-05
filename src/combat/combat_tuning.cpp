// SPDX-License-Identifier: GPL-3.0-or-later
#include "combat/combat_tuning.h"

namespace coney::combat {

CombatTuning& combatTuning() {
    static CombatTuning tuning;
    return tuning;
}

} // namespace coney::combat
