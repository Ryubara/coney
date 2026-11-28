// SPDX-License-Identifier: GPL-3.0-or-later
#include "warriors/unlockables.h"

#include <array>

namespace coney {

namespace {

// The Rumble modes, arena and gangs of the records `UM_Unlock(0, 0, 0)` unlocks at boot (records 161, 162, 582 and
// 170, 182, 188, 190 of docs/references/unlockables.md), gangs in `rumble_gang.lua`'s order.
constexpr std::array kFreshModes{12, 14};
constexpr std::array kFreshArenas{102};
constexpr std::array kFreshGangs{5, 3, 4, 1};

// The character types of the records `UM_Unlock(0, 0, 0)` (216-224) and `UM_Unlock(8, 0, 0)` (270-283) unlock.
constexpr std::array kFreshTypes{2,  6,   12,  16,  19,  22,  27,  31,  34,  91,  92, 93,
                                 94, 101, 102, 103, 104, 105, 224, 225, 226, 227, 228};

} // namespace

// @coney-only stand-in for running global.lua's records at boot, until Coney has the unlockables manager and saves
Unlockables Unlockables::freshProfile() {
    // What global.lua's CfgUnlockables unlocks on every boot, of the kinds the Rumble menu asks
    // (docs/research/frontend.md#rumble-data).
    Unlockables unlocks;
    for (const int mode : kFreshModes) {
        unlocks.unlock(UnlockKind::RumbleMode, mode);
    }
    for (const int arena : kFreshArenas) {
        unlocks.unlock(UnlockKind::RumbleArena, arena);
    }
    for (const int gang : kFreshGangs) {
        unlocks.unlock(UnlockKind::RumbleGang, gang);
    }
    for (const int type : kFreshTypes) {
        unlocks.unlock(UnlockKind::CharacterType, type);
    }
    return unlocks;
}

} // namespace coney
