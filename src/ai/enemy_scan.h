// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <map>

#include "ai/brain.h"
#include "ai/gangs.h"

// How an AI finds enemies on its own: a periodic scan of the enemy gangs' members it can see, which rebuilds its
// enemy list and tells it of each new one (event 0xb, a script's "seen the player"). The scan is a free function over
// the gangs, so whatever runs the brains (or a tactic, for its own members) can call it.
// Research: docs/research/ai.md#enemy-scan, docs/research/stealth.md#seen

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
/// The scan interval a brain is made with (`+0x144`), and a cop's, ms; × kScanSlowFactor in the fight stance.
inline constexpr std::uint64_t kScanIntervalMs = 2000;
inline constexpr std::uint64_t kScanIntervalCopMs = 1000;
inline constexpr std::uint64_t kScanSlowFactor = 4;
/// At most this many scans run in one update across all brains (`AI_TakeScanToken`).
inline constexpr int kScanTokens = 5;

/// Whether `scanner`'s scan would list `member` now, one of the members of an enemy gang (steps 1.1 to 1.4 of the
/// scan): within its sight range; the shadow's rule (shadowAllowsSight()); for an AI scanner not a Warrior, `member`
/// less than 1.9 m above it; the filter (validEnemy(), and for a Warrior a cop only within 10 m and no civilian); then
/// sight: within the near radius (1.5 m when `member` walks or stands, 5.2 m for a cop and a running member, else 3 m)
/// only the line of sight counts, unless `member` is hidden off shadow ground (the grace), when he must also be in the
/// view cone; beyond it, the view cone (the full field of view) and the line of sight. **Coney's reading**: the gangs
/// always seen (`+0xdc`), the fight count of `Human_MaySpectate` and the civilians' ped type are not modelled.
[[nodiscard]] bool scanSees(const Brain& scanner, const Brain& member);

/// `scanner`'s scan (`Brain_ScanEnemies`): a human who is down empties his list. Otherwise every member of a gang in
/// use that is an enemy of his (Gangs::enemies()) that scanSees() is found; old entries not found again stay while
/// within the sight range, still valid and within 3 m or in line of sight, and the others are dropped
/// (Brain::forget()); beyond 16, the nearest 16 are kept. Each one new to the list is added (Brain::addEnemy(), which
/// tells a tactic that runs its members) and sent to the scanner's own handlers as event `0xb`: its script's
/// (`HuSetMessageHandler`, a level's "seen the player"), else its type's. Returns how many were new. **Coney's
/// reading**: the gang's chosen target, the hostile counts and the radar's enemy marks are not built.
/// @orig 0x0028b358 Brain_ScanEnemies (unknown)
int scanEnemies(const Gangs& gangs, Brain& scanner);

/// When each brain scans (`Brain_MaybeScanEnemies`): every interval (kScanIntervalMs, a cop's kScanIntervalCopMs,
/// or one set with setInterval(); × 4 while it is locked on in the fight stance), at most kScanTokens scans an update.
/// Kept apart from the brains so a caller may own it. **Coney's reading**: the human's detail level (its distance to
/// the camera), which also slows the scan, is not built, nor the rescan when the gang changes.
class ScanSchedule {
  public:
    /// Sets `brain`'s interval (brain `+0x144`, the Scout goal's 500 ms) until reset with 0.
    void setInterval(const Brain& brain, std::uint64_t ms);
    /// `brain`'s interval now.
    [[nodiscard]] std::uint64_t interval(const Brain& brain) const;
    /// Starts an update: the tokens are full again.
    void beginUpdate() { m_tokens = kScanTokens; }
    /// Scans `brain` when its interval has passed since its last scan at `nowMs` and a token is left; returns
    /// whether it scanned. A brain refused a token tries again on the next update.
    /// @orig 0x0028fa18 Brain_MaybeScanEnemies (unknown)
    bool maybeScan(const Gangs& gangs, Brain& brain, std::uint64_t nowMs);
    /// Forgets `brain` (one being removed).
    void forget(const Brain& brain);

  private:
    struct Entry {
        std::uint64_t lastMs = 0;
        bool scanned = false;
        std::uint64_t intervalMs = 0; // 0: the type's own
    };
    std::map<const Brain*, Entry> m_entries;
    int m_tokens = kScanTokens;
};

} // namespace coney::ai
