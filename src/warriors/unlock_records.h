// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "warriors/profile_record.h"

namespace coney {

/// One 12-byte record of the unlockables table (docs/research/player-state.md#unlockables).
struct UnlockRecord {
    std::uint8_t level = 0;  ///< `+0`: the level the unlock belongs to.
    std::uint8_t group = 0;  ///< `+1`: a group within the level.
    std::uint8_t item = 0;   ///< `+2`: the item within the group (0: the level-complete record).
    std::uint8_t type = 0;   ///< `+3`: the unlockable's type (docs/references/unlockables.md).
    std::uint16_t extra = 0; ///< `+4`: read only by `UM_GetRecordData`.
    std::uint32_t data = 0;  ///< `+8`: the data id the type's tests match.
};

/// An unlocked / total pair of a count the menus show.
struct UnlockTally {
    std::size_t unlocked = 0; ///< How many of the counted records (or items) are unlocked.
    std::size_t total = 0;    ///< How many are counted.
    /// Whether every counted one is unlocked (true for none): the screens draw a complete count in red.
    [[nodiscard]] bool complete() const { return unlocked == total; }
};

/// What `Unlocks_CountUnlocked` gives the statistics screen's summary.
struct UnlockCounts {
    /// The records of every type but 0, 5 and 7-11 (on the disc: 1, 2, 3, 4 and 6) whose level is neither 1 nor 4.
    UnlockTally records;
    /// The distinct (level, item) pairs of those types' group-1 records, any level; a pair is unlocked when its first
    /// record is.
    UnlockTally groupItems;
    /// The completion percentage: records.unlocked × 100 / records.total, truncated (`Float_ToUInt`); 0 with no
    /// records (Coney's guard: the original divides by the total).
    /// @orig 0x00215150 GameStats_BuildSummary (GameStats.cpp)
    [[nodiscard]] unsigned percent() const;
};

/// What `Unlockables_GetLevelTargets` gives one mission's line on the statistics and mission-select screens.
struct LevelTargets {
    std::uint32_t scoreTarget = 0; ///< The highest item of the level's group-0 records × 1000.
    UnlockTally characters;        ///< Its type-4 records (character types).
    UnlockTally arenas;            ///< Its type-2 records (Rumble arenas).
    UnlockTally groupItems;        ///< The distinct items of its group-1 records of types 1, 2, 3, 4 and 6.
};

/// The unlockables manager's records (`0x006fe998`), which `global.lua` fills with `UM_SetNumUnlockables` and
/// `UM_SetUnlockable`, and the story's unlocks over them. The two bit sets, locked (`0x006fe8f8`) and new
/// (`0x006fe948`), are the profile's (SavedProgress::lockedBits, SavedProgress::newBits), so a save carries what was
/// unlocked: every query takes the progress it works on.
///
/// Research: docs/research/player-state.md#unlockables, docs/references/bindings/level.md#um_unlock
class UnlockRecords {
  public:
    /// The most records: the size of the bit sets.
    static constexpr std::size_t kCapacity = SavedProgress::kUnlockables;

    /// `UM_SetNumUnlockables(count)`: how many records the table holds (at most kCapacity); ignored once it exists.
    /// @orig 0x004236f0 UM_SetNumUnlockables (unknown)
    void setCount(std::size_t count);
    /// `UM_SetUnlockable(index, ...)`: fills record `index`, making the table first if there is none (kCapacity
    /// records, Coney's choice: the page does not give the default size). Returns false for an index past the table.
    /// @orig 0x00423718 Unlocks_SetRecord (unknown)
    bool set(std::size_t index, const UnlockRecord& record);
    /// `UM_Reset()`: drops the records (the profile's bits stay).
    /// @orig 0x004236d0 UM_Reset (unknown)
    void reset() {
        m_records.clear();
        m_count = 0;
    }
    /// The records.
    [[nodiscard]] const std::vector<UnlockRecord>& records() const { return m_records; }
    /// Record `index`; null past the table.
    /// @coney-only Coney's checked access (the original's `Unlockables_GetRecord` does not check)
    [[nodiscard]] const UnlockRecord* record(std::size_t index) const;

    /// `UM_Unlock(level, group, item)`: unlocks every record whose first three bytes match, marking the newly
    /// unlocked ones new. Returns how many were newly unlocked.
    /// @orig 0x004237e8 UM_Unlock (unknown)
    std::size_t unlock(SavedProgress& progress, std::uint8_t level, std::uint8_t group, std::uint8_t item) const;
    /// `UM_IsLevelComplete(level)`: whether the level's record (level, 0, 0) is unlocked; false with none.
    /// Also the engine's `Unlocks_IsLevelComplete`, which the statistics and mission-select screens ask: the first
    /// record (level, 0, 0) decides. The engine's looks among the level's first 32 group-0 records only; Coney
    /// applies no such bound (Coney's choice: the bound only matters past 32 such records).
    /// @orig 0x004238a8 UM_IsLevelComplete (unknown)
    /// @orig 0x004241d8 Unlocks_IsLevelComplete (unknown)
    [[nodiscard]] bool isLevelComplete(const SavedProgress& progress, std::uint8_t level) const;

    /// The statistics screen's summary counts (docs/research/game-stats.md#counts).
    /// @orig 0x004245d8 Unlocks_CountUnlocked (unknown)
    [[nodiscard]] UnlockCounts countUnlocked(const SavedProgress& progress) const;
    /// One mission's targets and counts over the records of `level` (60: 60-64; 101: 101-137), groups 0 and 1 only.
    /// @orig 0x00424298 Unlockables_GetLevelTargets (unknown)
    [[nodiscard]] LevelTargets levelTargets(const SavedProgress& progress, int level) const;
    /// Every type-4 record (character type): the Rumble soldier screen's count.
    /// @orig 0x004247d0 Unlockables_CountCharacters (unknown)
    [[nodiscard]] UnlockTally countCharacters(const SavedProgress& progress) const;
    /// `UM_IsDataUnlocked(type, data)`: whether the first record of that type and data is unlocked; false with none.
    /// @orig 0x00424130 Unlocks_IsDataUnlocked (unknown)
    [[nodiscard]] bool isDataUnlocked(const SavedProgress& progress, std::uint8_t type, std::uint32_t data) const;
    /// `UM_IsTypeDirty(type, clear)`: whether any record of `type` is marked new; `clear` also clears those marks.
    /// @orig 0x00423988 UM_IsTypeDirty (unknown)
    bool isTypeDirty(SavedProgress& progress, std::uint8_t type, bool clear) const;
    /// `UM_IsDataDirty(type, data, clear)`: whether the first record of that type and data is marked new; `clear` also
    /// clears its mark.
    /// @orig 0x004239b0 UM_IsDataDirty (unknown)
    bool isDataDirty(SavedProgress& progress, std::uint8_t type, std::uint32_t data, bool clear) const;

  private:
    // The first record of `type` and `data`; nothing when none matches.
    // @coney-only the search isDataUnlocked() and isDataDirty() share
    [[nodiscard]] std::optional<std::size_t> find(std::uint8_t type, std::uint32_t data) const;

    std::size_t m_count = 0; // UM_SetNumUnlockables' count; 0 until set
    std::vector<UnlockRecord> m_records;
};

/// The "new" mark of unlockable record `index` (SavedProgress::newBits, laid out as the locked bits).
/// @coney-only the bit access the UM ports share
[[nodiscard]] bool isMarkedNew(const SavedProgress& progress, std::size_t index);
/// Sets or clears the "new" mark of record `index`; out of range does nothing.
/// @coney-only the bit access the UM ports share
void setMarkedNew(SavedProgress& progress, std::size_t index, bool marked);

} // namespace coney
