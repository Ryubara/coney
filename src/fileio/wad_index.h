// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "core/error.h"

namespace coney::io {

/// The folder the game puts before every file name before hashing it: `level1.lev` is looked up as
/// `./ee_files/level1.lev` (docs/research/file-io.md#resolving-a-name).
inline constexpr std::string_view kWadNamePrefix = "./ee_files/";

/// Bytes of WARRIORS.DIR's header: the entry count, then 12 bytes of padding.
inline constexpr std::size_t kWadDirHeaderSize = 16;
/// Bytes of one WARRIORS.DIR entry.
inline constexpr std::size_t kWadDirEntrySize = 12;

/// One entry of WARRIORS.DIR: where one file lives inside WARRIORS.WAD.
/// Layout: docs/research/formats/wad-dir.md
struct WadEntry {
    std::uint32_t index = 0;     ///< Position in WARRIORS.DIR (entries are sorted by wadOffset).
    std::uint32_t wadOffset = 0; ///< Byte offset into WARRIORS.WAD; a multiple of 2048 on the retail disc.
    std::uint32_t size = 0;      ///< Size of the file in bytes.
    std::uint32_t nameHash = 0;  ///< CRC-32 of the lowercased `./ee_files/<name>` (docs/research/name-hash.md).
};

/// The full path a bare file name is hashed as: `./ee_files/` + `name`, unless `name` already starts with `./`.
[[nodiscard]] std::string wadPath(std::string_view name);

/// The table of every file in WARRIORS.WAD, read from WARRIORS.DIR.
///
/// Research: docs/research/formats/wad-dir.md, docs/research/file-io.md#the-wad-index
class WadIndex {
  public:
    /// Parses a complete WARRIORS.DIR image. Unlike the original, which trusts the file, it fails with
    /// ErrorCode::Truncated or ErrorCode::Invalid when the size is not `16 + count x 12`, and, when `wadSize` is
    /// given, with ErrorCode::Invalid when an entry ends past the end of WARRIORS.WAD.
    /// The original does this work in its constructor; a factory function can report failure, a constructor can't.
    /// @orig 0x00149160 DVDWadIndex::DVDWadIndex (DVDWadIndexPS2.cpp)
    [[nodiscard]] static std::expected<WadIndex, Error> parse(std::span<const std::byte> dirData,
                                                              std::optional<std::uint64_t> wadSize = std::nullopt);

    /// Finds the entry for a file name such as `level1.lev` (hashed as `./ee_files/level1.lev`) or a full path
    /// starting with `./`, in any letter case. Returns nullptr when no entry matches. The pointer stays valid as long
    /// as this index. A hash table replaces the original's linear scan; every entry hash on the retail disc is
    /// distinct, and on a duplicate the first entry wins as in the scan.
    /// @orig 0x001490b8 DVDWadIndex::Find (DVDWadIndexPS2.cpp)
    [[nodiscard]] const WadEntry* find(std::string_view name) const;

    /// Finds the entry whose stored name hash is `hash`; nullptr when none has it.
    [[nodiscard]] const WadEntry* findHash(std::uint32_t hash) const;

    /// Every entry, in WARRIORS.DIR order.
    [[nodiscard]] std::span<const WadEntry> entries() const { return m_entries; }

  private:
    std::vector<WadEntry> m_entries;
    std::unordered_map<std::uint32_t, std::uint32_t> m_byHash; ///< name hash -> position in m_entries
};

} // namespace coney::io
