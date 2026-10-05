// SPDX-License-Identifier: GPL-3.0-or-later
#include "warriors/unlockables.h"

namespace coney {

Unlockables Unlockables::freshProfile() {
    Unlockables unlocks;
    // The two modes, the arena and the two gangs a fresh boot shows (docs/research/frontend.md#rumble-setup).
    unlocks.unlock(UnlockKind::RumbleMode, 12);
    unlocks.unlock(UnlockKind::RumbleMode, 14);
    unlocks.unlock(UnlockKind::RumbleArena, 102);
    unlocks.unlock(UnlockKind::RumbleGang, 5);
    unlocks.unlock(UnlockKind::RumbleGang, 3);
    return unlocks;
}

} // namespace coney
