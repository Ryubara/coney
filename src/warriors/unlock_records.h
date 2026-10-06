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
    [[nodiscard]] const UnlockRecord* record(std::size_t index) const;

    /// `UM_Unlock(level, group, item)`: unlocks every record whose first three bytes match, marking the newly
    /// unlocked ones new. Returns how many were newly unlocked.
    /// @orig 0x004237e8 UM_Unlock (unknown)
    std::size_t unlock(SavedProgress& progress, std::uint8_t level, std::uint8_t group, std::uint8_t item) const;
    /// `UM_IsLevelComplete(level)`: whether the level's record (level, 0, 0) is unlocked; false with none.
    /// @orig 0x004238a8 UM_IsLevelComplete (unknown)
    [[nodiscard]] bool isLevelComplete(const SavedProgress& progress, std::uint8_t level) const;
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
    [[nodiscard]] std::optional<std::size_t> find(std::uint8_t type, std::uint32_t data) const;

    std::size_t m_count = 0; // UM_SetNumUnlockables' count; 0 until set
    std::vector<UnlockRecord> m_records;
};

/// The "new" mark of unlockable record `index` (SavedProgress::newBits, laid out as the locked bits).
[[nodiscard]] bool isMarkedNew(const SavedProgress& progress, std::size_t index);
/// Sets or clears the "new" mark of record `index`; out of range does nothing.
void setMarkedNew(SavedProgress& progress, std::size_t index, bool marked);

} // namespace coney
