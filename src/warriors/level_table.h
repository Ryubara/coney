// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace coney {

/// One level record of the game state's level table, as `CfgLevelName` fills it from `config_preload3.lua`. The
/// original's record is 0x84 bytes; Coney keeps the same values, typed, with the record's offsets in the comments.
///
/// The scripts pass 18 arguments: the index, four names, the level number, then twelve numbers. The page lists the
/// fields the binding's writer (`0x0041f118`) fills but not which argument goes where, so the mapping is Coney's
/// reading of the disc's data (inferred): the index (0-110), the level name (record 0 is `level100`), the world name
/// (equal to the level name in every record with packs) and the level number (100 for record 0, as `GetLevelId(0)` is
/// at run time). The twelve numbers are kept in order until their fields are known.
///
/// Research: docs/research/frontend.md#the-level-table, docs/research/level-loading.md#the-level-record
struct LevelRecord {
    /// The longest each name field holds: the record's fixed character arrays less their terminator.
    static constexpr std::size_t kNameLength = 15;
    static constexpr std::size_t kSecondNameLength = 20;
    static constexpr std::size_t kWorldNameLength = 15;
    static constexpr std::size_t kFourthNameLength = 31;

    /// How many numbers follow the level number.
    static constexpr std::size_t kOtherValues = 12;

    double id = 0;          ///< `+0x00`, argument 1: the record's index in the table.
    std::string name;       ///< `+0x14`, argument 2: the level name (`level100`): its `.lev`, `.lua`, packs and lists.
    std::string secondName; ///< `+0x24`, argument 3: hashed to release the level's dependency list.
    std::string worldName;  ///< `+0x39`, argument 4: the streamed world's name.
    std::string fourthName; ///< `+0x49`, argument 5: meaning unknown.
    double number = 0;      ///< `+0x04`, argument 6: the level number (`GetLevelId`), naming the intro movie `L<n>_IN`.
    /// Arguments 7-18, in order. They fill the section count (`+0x08`), the byte at `+0x0c`, the three flags at
    /// `+0x0d` (bit 1 plays the intro movie, bit 2 an outro movie), `+0x10` and the values at `+0x6c`-`+0x80`, in an
    /// order not yet known.
    std::array<double, kOtherValues> values{};
};

/// The game state's level table: up to 128 records by index, filled by `CfgLevelName` (`config_preload3.lua` sets
/// 111), read by the level flow (level 0, `level100`, is the front end) and by `GetLevelId`.
///
/// Research: docs/research/frontend.md#the-level-table
class LevelTable {
  public:
    /// How many records the table holds.
    static constexpr std::size_t kCapacity = 128;

    /// Stores `record` at its index (`record.id`), replacing any record there. Returns false, storing nothing, for an
    /// index that is not a whole number below kCapacity (bad script data, not a programmer error).
    /// @orig 0x0041f118 W_GameState_SetLevelRecord (unknown)
    bool set(LevelRecord record);

    /// The record at `index`; null when none was set there.
    [[nodiscard]] const LevelRecord* at(std::size_t index) const;
    /// The index of the record named `name` (any letter case); nothing when no record has that name.
    [[nodiscard]] std::optional<std::size_t> find(std::string_view name) const;
    /// Records set.
    [[nodiscard]] std::size_t count() const;
    /// Removes every record.
    void clear();

  private:
    std::array<std::optional<LevelRecord>, kCapacity> m_records;
};

} // namespace coney
