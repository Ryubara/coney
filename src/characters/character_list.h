// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "core/error.h"
#include "fileio/wad.h"

// The Character List of warriors.glr: how a character's model name leads to its three resources.
// Format: docs/research/characters.md#files.

namespace coney::characters {

/// The chunk type of the Character List.
inline constexpr std::uint32_t kCharacterListChunk = 0x44;

/// Bytes of one record, and where the records start in the chunk (after a 16-byte header whose first word is the
/// count).
inline constexpr std::size_t kCharacterRecordBytes = 32;
inline constexpr std::size_t kCharacterRecordsOffset = 0x10;

/// One record of the Character List.
struct CharacterRecord {
    std::uint32_t nameHash = 0;     ///< +0x00: CRC-32 of the model name, lower case, no path.
    std::uint32_t dataHash = 0;     ///< +0x04: names the character data resource (animations).
    std::uint32_t modelHash = 0;    ///< +0x08: names the model resource.
    std::uint32_t texturesHash = 0; ///< +0x0c: names the texture dictionary resource.
    std::uint32_t dataSize = 0;     ///< +0x10: the character data's size (inferred).
    std::uint32_t modelSize = 0;    ///< +0x14: the model's size (inferred).
    std::uint32_t modelSize2 = 0;   ///< +0x18: a size close to the model's; meaning not traced.
    std::uint32_t texturesSize = 0; ///< +0x1c: the texture dictionary's size (inferred).
};

/// The Character List, parsed.
class CharacterList {
  public:
    /// Parses the chunk's data: a count word, 12 more header bytes, then `count` records of 32 bytes. Fails with
    /// ErrorCode::Truncated when the records do not fit.
    /// The original's handler keeps the chunk and points the resource manager at its count and records.
    /// @orig 0x00178098 CharacterList_OnLoaded (unknown)
    [[nodiscard]] static std::expected<CharacterList, Error> parse(std::span<const std::byte> chunk);

    /// The record of the model `name` (any letter case, such as "warr_re_cv"), found by the CRC-32 of its lower-case
    /// spelling; nullptr when there is none. The pointer stays valid as long as this list.
    [[nodiscard]] const CharacterRecord* find(std::string_view name) const;

    /// Every record, in the chunk's order.
    [[nodiscard]] std::span<const CharacterRecord> records() const { return m_records; }

  private:
    std::vector<CharacterRecord> m_records;
};

/// The hash the Character List keeps for a model name: the CRC-32 of the name in lower case
/// (docs/research/name-hash.md).
[[nodiscard]] std::uint32_t characterNameHash(std::string_view name);

/// The WAD file name of a resource named by `hash`: the hash in decimal (`"%u"`), as the game builds it.
[[nodiscard]] std::string resourceFileName(std::uint32_t hash);

/// Loads `warriors.glr` from `wad` through the chunk system (its chunks read raw) and parses its Character List.
/// Fails with ErrorCode::NotFound when the file or the chunk is missing, and as the load and parse() do.
[[nodiscard]] std::expected<CharacterList, Error> loadCharacterList(const io::Wad& wad);

} // namespace coney::characters
