// SPDX-License-Identifier: GPL-3.0-or-later
#include "combat/revive.h"

namespace coney::combat {

bool mayRevive(const ReviveFacts& facts) {
    // 1. The reviver free, somebody found, the downed human free.
    if (facts.reviverHeld || facts.reviverBlocked || !facts.found || facts.downedHeld) {
        return false;
    }
    // 2. A way that is not clear: an AI human is refused; a player only takes a Warrior's help.
    if (!facts.wayClear && (!facts.downedIsPlayer || !facts.reviverIsWarrior)) {
        return false;
    }
    // 3. A flash from either of them.
    return facts.reviverHasFlash || facts.downedHasFlash;
}

} // namespace coney::combat
