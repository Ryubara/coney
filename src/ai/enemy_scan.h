// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <vector>

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

/// One member of an enemy gang that a scan's step 1 has tested up to his line of sight (scanSees() less its ray).
struct SightCandidate {
    float distance = 0.0F; ///< From the scanner, metres (Brain::distanceTo()).
    bool needsRay = false; ///< Only the line of sight is left to decide him; false when his gang is always seen.
    bool referred = false; ///< The scanner refers to him (Brain::refersTo()), so it must know whether he is found.
};

/// Which of `candidates` a scan's step 1 finds, one flag per candidate in their order. A candidate is found when he
/// needs no ray or `clearLine(k)` (his line of sight, by his index) says it is clear; the lines are cast nearest first,
/// and once `keep` are found no line is cast to a candidate farther than the `keep`-th found, unless the scanner
/// refers to him: such a one is not found. The result of the scan is the same as with every line cast: step 4 keeps
/// the nearest `keep` of all it found, so a member farther than `keep` found ones would leave the list again, and
/// forgetting him (Brain::forget()) changes nothing when the scanner does not refer to him. **Coney's order** (#1555):
/// the original casts every candidate's line, in the gangs' order (at most 59 other humans there; Coney's 32 gangs of
/// 16 slots allow up to 512 a scan). **Holds only while step 4 always sorts** (the #1597 stand-in): under the
/// original's rule of sorting only past 16, a scan that found exactly 16 before skipping would keep the gangs' order
/// where casting every line finds 17 or more and sorts, so fixing #1597 must cast until 17 are found, or otherwise know
/// whether more than 16 would be.
/// @orig 0x0028b358 Brain_ScanEnemies (unknown)
[[nodiscard]] std::vector<bool> nearestSeen(std::span<const SightCandidate> candidates, std::size_t keep,
                                            const std::function<bool(std::size_t)>& clearLine);

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
