// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <map>

// When each AI brain runs its enemy scan (ai/enemy_scan.h): every brain on its own interval, and at most five scans in
// one update across all brains. The gangs own the one schedule of a scene (Gangs::scans()), refill its tokens at the
// start of each update and every brain's update asks it (Brain::update()).
// Research: docs/research/ai.md#enemy-scan, docs/research/ai-reimpl.md#unit-1

namespace coney::ai {

class Brain;
class Gangs;

/// The scan interval a brain is made with (`+0x144`), and a cop's, ms; × kScanSlowFactor in the fight stance.
inline constexpr std::uint64_t kScanIntervalMs = 2000;
inline constexpr std::uint64_t kScanIntervalCopMs = 1000;
inline constexpr std::uint64_t kScanSlowFactor = 4;
/// At most this many scans run in one update across all brains (`AI_TakeScanToken`).
inline constexpr int kScanTokens = 5;

/// When each brain scans (`Brain_MaybeScanEnemies`): every interval (kScanIntervalMs, a cop's kScanIntervalCopMs,
/// or one set with setInterval(); × 4 while it is locked on in the fight stance), at most kScanTokens scans an update.
/// **Coney's reading**: the human's detail level (its distance to the camera), which also slows the scan, is not built,
/// nor the rescan when the gang changes.
class ScanSchedule {
  public:
    /// Sets `brain`'s interval (brain `+0x144`, the Scout goal's 500 ms) until reset with 0.
    void setInterval(const Brain& brain, std::uint64_t ms);
    /// `brain`'s interval now.
    [[nodiscard]] std::uint64_t interval(const Brain& brain) const;
    /// Starts an update: the tokens are full again.
    void beginUpdate() { m_tokens = kScanTokens; }
    /// Scans `brain` (ai::scanEnemies()) when its interval has passed since its last scan at `nowMs` and a token is
    /// left; returns whether it scanned. A brain refused a token tries again on the next update.
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
