// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <vector>

#include "ai/brain.h"
#include "ai/brains.h"

// The ground rule of hiding: each update, a human on shadow ground who nobody hunts enters the hidden state, one who
// steps off it or is hunted leaves it, and stepping onto it shakes off the far hunters who cannot see him. The hidden
// state itself (its grace, its move style) is the human's (human::Human::enterHiding()).
// Research: docs/research/stealth.md#shadow-ground

namespace coney::ai {

/// What the ground rule keeps for one human from update to update.
struct HideMemory {
    bool onShadow = false;               ///< Brain `+0x2d5`: he stood on shadow ground at the last update.
    std::vector<const Brain*> shakenOff; ///< The hunters no longer counted since he stepped onto the shadow.
};

/// What the rule found, for the player's radar tint.
struct HideResult {
    bool mayHide = false; ///< On shadow ground, not hunted and allowed: the radar turns blue.
};

/// How many brains hunt `hider`: every other brain that is fightable and has him as its target or among its enemies,
/// less the ones `memory` shook off. **Coney's reading** of the hostile count (brain `+0x152`): Coney keeps no count,
/// so it is taken from the hunters' own books.
[[nodiscard]] int huntersOf(const Brains& brains, const Brain& hider, const HideMemory& memory);

/// One update of the ground rule for `hider` (`Human_SnapToGround`'s hide rules, run after his ground snap):
/// - off shadow ground: `memory` is cleared and, if hidden, he leaves the hidden state (human::Human::leaveHiding());
/// - stepping onto it: every hunter farther from him than twice the hunter's far melee range (brain `+0x140`) with no
///   line of sight to him is shaken off (no longer counted while he stays on it);
/// - on it, not hunted and not `carryingMolotov`: he enters the hidden state (nothing when already hidden) and
///   HideResult::mayHide is set; hunted or carrying a molotov, he leaves it.
///
/// **Coney's reading**: run for player 1 only (the original runs it for every player and the members of his gang;
/// the Warriors' hide order is not built).
/// @orig 0x0023eab8 Human_SnapToGround (unknown)
/// @orig 0x0028f000 Brain_ShakeOffPursuers (unknown)
HideResult updateHiding(const Brains& brains, Brain& hider, HideMemory& memory, bool carryingMolotov);

} // namespace coney::ai
