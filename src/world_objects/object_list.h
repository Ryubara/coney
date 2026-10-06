// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string_view>
#include <vector>

#include "core/error.h"
#include "fileio/wad.h"

// The Object List of warriors.glr: how an object type's name leads to its model and texture dictionary, as the
// Character List does for characters. Format: docs/research/formats/wad-contents.md#object-list.

namespace coney::world_objects {

/// The chunk type of the Object List.
inline constexpr std::uint32_t kObjectListChunk = 0x46;

/// Bytes of one record, and where the records start in the chunk (after a 16-byte header whose first word is the
/// count).
inline constexpr std::size_t kObjectRecordBytes = 36;
inline constexpr std::size_t kObjectRecordsOffset = 0x10;

/// One record of the Object List. Only the name, model and textures are used; the rest is kept as read.
struct ObjectRecord {
    std::uint32_t nameHash = 0;     ///< +0x00: CRC-32 of the object type's name, lower case (`dyn_bat`).
    std::uint32_t variantOf = 0;    ///< +0x04: 0, or the name hash of the record this one is a variant of.
    std::uint32_t modelHash = 0;    ///< +0x08: names the model resource.
    std::uint32_t texturesHash = 0; ///< +0x0c: names the texture dictionary resource.
    std::uint32_t fourthHash = 0;   ///< +0x10: names a fourth resource for a few objects; meaning not traced.
    std::uint32_t modelSize = 0;    ///< +0x14: the model's size (inferred).
    std::uint32_t modelSize2 = 0;   ///< +0x18: a size a little above the model's; meaning not traced.
    std::uint32_t texturesSize = 0; ///< +0x1c: the texture dictionary's size (inferred).
    std::uint32_t fourthSize = 0;   ///< +0x20: non-zero exactly when +0x10 is; meaning not traced.
};

/// The Object List, parsed.
class ObjectList {
  public:
    /// Parses the chunk's data: a count word, 12 more header bytes, then `count` records of 36 bytes. Fails with
    /// ErrorCode::Truncated when the records do not fit.
    /// The original's handler keeps the chunk and points the resource manager at its count and records.
    /// @orig 0x00181170 ObjectList_OnLoaded (unknown)
    [[nodiscard]] static std::expected<ObjectList, Error> parse(std::span<const std::byte> chunk);

    /// The record of the object type `name` (any letter case), found by the CRC-32 of its lower-case spelling;
    /// nullptr when there is none. The pointer stays valid as long as this list.
    [[nodiscard]] const ObjectRecord* find(std::string_view name) const;

    /// The record whose name hash is `nameHash` (an object type's model hash, CRC-32 of its name), by a linear search
    /// as the original's; nullptr when there is none. The pointer stays valid as long as this list.
    /// @orig 0x001811b0 ObjectList_FindByHash (unknown)
    [[nodiscard]] const ObjectRecord* findByHash(std::uint32_t nameHash) const;

    /// Every record, in the chunk's order.
    [[nodiscard]] std::span<const ObjectRecord> records() const { return m_records; }

  private:
    std::vector<ObjectRecord> m_records;
};

/// Loads `warriors.glr` from `wad` through the chunk system (its chunks read raw) and parses its Object List. Fails
/// with ErrorCode::NotFound when the file or the chunk is missing, and as the load and parse() do.
[[nodiscard]] std::expected<ObjectList, Error> loadObjectList(const io::Wad& wad);

} // namespace coney::world_objects
