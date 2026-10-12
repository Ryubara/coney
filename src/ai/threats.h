// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Who is a threat to whom: the test the fight goal's re-target, picking a side in a fight, giving way and the enemy
// scan's Warrior filter share. Two pad players are always threats to each other, as are a Warrior hitting back and any
// pad player; otherwise only the members of an enemy gang are (Gangs::enemies(), which a truce lifts).
// Research: docs/research/ai-reimpl.md#brain-is-threat, docs/research/ai.md#enemy-scan

namespace coney::ai {

class Brain;

/// Whether `other`'s human is a threat to `brain` (`Brain_IsThreat`): never `brain` itself; true when either is a
/// Warrior (BrainType::Warrior) hitting back (Brain::hittingBack(), `+0x2e5`) and the other a pad player; else true
/// when `brain`'s gang has `other`'s as an enemy (Gangs::enemies(), `Gang_IsEnemyOf` on brain `+0x20c`) or both are
/// pad players. `Human_IsThreatTo` is the same test on two humans' brains. **Coney's reading**: a pad player is a
/// brain of type BrainType::Player (the original reads human `+0x1b0`, the pad, which only a player's human has).
/// @orig 0x00290138 Brain_IsThreat (unknown)
/// @orig 0x00222a48 Human_IsThreatTo (unknown)
[[nodiscard]] bool isThreat(const Brain& brain, const Brain& other);

} // namespace coney::ai
