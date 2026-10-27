// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>

#include "ai/brain.h"
#include "ai/gangs.h"
#include "ai/scan_schedule.h"

// How an AI finds enemies on its own: a periodic scan of the enemy gangs' members it can see, which rebuilds its
// enemy list and tells it of each new one (event 0xb, a script's "seen the player"). The scan is a free function over
// the gangs, so whatever runs the brains (or a tactic, for its own members) can call it.
// Research: docs/research/ai-fights.md#enemy-scan, docs/research/stealth.md#seen

namespace coney::ai {

/// The largest enemy list a scan leaves (`+0x164`, 16 slots).
inline constexpr std::size_t kScanMaxEnemies = 16;
/// The scan's near radius, and its values for a member walking or standing and for a cop looking at a running member,
/// metres: within it only the line of sight counts, all round.
inline constexpr float kScanNearRadius = 3.0F;
inline constexpr float kScanNearWalking = 1.5F;
inline constexpr float kScanNearCopRunning = 5.2F;
/// How much higher than the scanner a member may stand to be listed by an AI scanner, metres.
inline constexpr float kScanHeightLimit = 1.9F;
/// The Warriors' filter: a cop is a threat only within this, metres.
inline constexpr float kScanWarriorCopRange = 10.0F;

/// Whether `scanner`'s scan would list `member` now, one of the members of an enemy gang (steps 1.1 to 1.4 of the
/// scan): within its sight range; the shadow's rule (shadowAllowsSight()); unless `member`'s gang is always seen
/// (Gang::alwaysSeen(), `+0xdc`), the spectate test (`Human_MaySpectate`: for an AI scanner not a Warrior, `member`
/// less than 1.9 m above it); the filter (validEnemy(), and for a Warrior a cop only
/// within 10 m and no civilian); then, unless the gang is always seen, sight: within the near radius (1.5 m when
/// `member` walks or stands, 5.2 m for a cop and a running member, else 3 m) only the line of sight counts, unless
/// `member` is hidden off shadow ground (the grace), when he must also be in the view cone; beyond it, the view cone
/// (the full field of view) and the line of sight. **Coney stand-ins** (#1011): the spectate test's pass for a
/// scanner who may not start a fight, its investigation count and vertical sight modifier, and the Warriors' filter's
/// civilians (warriorThreat()).
[[nodiscard]] bool scanSees(const Brain& scanner, const Brain& member);

/// `scanner`'s scan (`Brain_ScanEnemies`): a human who is down empties his list. Otherwise every member of a gang in
/// use that is an enemy of his (Gangs::enemies()) that scanSees() is found, and his gang's shared target
/// (Gang::setSharedTarget()) when validEnemy() allows him; old entries not found again stay while
/// within the sight range, still valid and within 3 m or in line of sight, and the others are dropped
/// (Brain::forget()); beyond 16, the nearest 16 are kept. Each one new to the list is added (Brain::addEnemy(), which
/// tells a tactic that runs its members) and sent to the scanner's own handlers as event `0xb`: its script's
/// (`HuSetMessageHandler`, a level's "seen the player"), else its type's. Returns how many were new. **Coney
/// stand-in** (#1011): the hostile counts (`Brain_CountHostile`, `Brain_UncountHostile`) and the radar's enemy marks
/// are not built.
/// @orig 0x0028b358 Brain_ScanEnemies (unknown)
int scanEnemies(const Gangs& gangs, Brain& scanner);

} // namespace coney::ai
