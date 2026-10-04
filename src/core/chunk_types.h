// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string_view>

namespace coney::chunk {

/// Number of real chunk types, 0x00 to 0x53. The original's type table has one more record, 0x54, a terminator
/// whose fields are all 0xffffffff, so 0x54 and above are never valid types (docs/research/chunk-system.md).
inline constexpr std::uint32_t kChunkTypeCount = 0x54;

/// Bytes of every header in a chunk container: the container's, a group's and each chunk's.
inline constexpr std::uint32_t kHeaderSize = 16;

/// Chunk types the reimplemented handlers refer to. The full list of names is chunkTypeName().
inline constexpr std::uint32_t kCameraAnimation = 0x0E;
inline constexpr std::uint32_t kNullPointer = 0x14; ///< An untyped "pointer" result (docs/research/chunk-system.md).
inline constexpr std::uint32_t kGbhScript = 0x30;

/// The fourth word of a pack's container header: crc32("package") (docs/research/formats/wad-contents.md).
inline constexpr std::uint32_t kPackageMarker = 0xDE686795;

/// The 16 bytes before every chunk. Layout: docs/research/chunk-system.md#container-layout
struct ChunkHeader {
    std::uint32_t type = 0; ///< Index into the chunk type table, below kChunkTypeCount.
    std::uint32_t size = 0; ///< Bytes of chunk data after this header.
    std::uint32_t zero = 0; ///< 0 in nearly every chunk (inferred); not read by the loaders.
    std::uint32_t id = 0;   ///< Unknown meaning (a name hash in many chunks); not read by the loaders.
};

/// The 16 bytes at the start of a container.
struct ContainerHeader {
    std::uint32_t count = 0;        ///< Flat: number of chunks. Grouped: the most groups to read.
    std::uint32_t payloadBytes = 0; ///< Inferred: the sum of the chunks' sizes (a pack's own size for a pack).
    std::uint32_t zero = 0;         ///< Inferred: 0.
    std::uint32_t id = 0;           ///< Unknown; kPackageMarker in a pack.
};

/// The 16 bytes before each group of a grouped container (a resource of a pack).
struct GroupHeader {
    std::uint32_t chunkCount = 0; ///< Chunks in this group.
    std::uint32_t unknown1 = 0;   ///< Not read by the loader; wad-contents.md reads it as the sum of chunk sizes.
    std::uint32_t unknown2 = 0;   ///< Not read by the loader; 0 in the data (wad-contents.md, inferred).
    std::uint32_t resourceId = 0; ///< The resource this group belongs to; 0 ends the container.
};

/// The debug name the original's chunk type table gives `type` ("Level Header" for 0x17), for diagnostics only.
/// Returns an empty view for a type at or above kChunkTypeCount.
///
/// Research: docs/research/chunk-system.md#chunk-type-table
[[nodiscard]] std::string_view chunkTypeName(std::uint32_t type);

} // namespace coney::chunk
