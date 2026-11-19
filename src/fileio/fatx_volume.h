// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <string>
#include <vector>

#include "core/error.h"
#include "fileio/stream.h"

namespace coney::io {

/// One entry of a FATX directory: a file or a folder.
struct FatxEntry {
    std::string name;               ///< 1-42 characters, as the entry holds them (FATX compares names ignoring case).
    bool directory = false;         ///< Attribute bit 0x10.
    std::uint32_t firstCluster = 0; ///< The first cluster of its chain, counted from 1.
    std::uint32_t size = 0;         ///< A file's bytes; 0 for a folder.
};

/// The Xbox hard disk's data partition (the console's `E:`, which holds `UDATA`), at a fixed place on every retail
/// disk.
///
/// Research: docs/research/save.md#xbox-containers
inline constexpr std::uint64_t kXboxDataPartitionOffset = 0xABE80000;
/// The data partition's size in bytes.
inline constexpr std::uint64_t kXboxDataPartitionSize = 0x131F00000;

/// A FATX volume: the Xbox's own file system, on each partition of its hard disk and on a memory unit. It reads the
/// 4 KB superblock, the FAT after it (16-bit entries for fewer than 0xfff0 clusters, else 32-bit), the cluster chains
/// and the 64-byte directory entries. Only reading: the volume is never changed. It reads its stream on demand, so a
/// whole hard-disk image costs no memory.
///
/// Research: docs/research/save.md#xbox-containers
/// @coney-only reads the console's file system for the importer; the game reaches it through the Xbox kernel
class FatxVolume {
  public:
    /// Whether `stream` holds the FATX magic at `offset`. Leaves the stream's position unspecified.
    [[nodiscard]] static bool looksLikeFatx(Stream& stream, std::uint64_t offset);

    /// Reads the superblock of the `size`-byte volume at `offset` of `stream`, which must outlive the volume and not be
    /// moved by anyone else while it is read. Fails with ErrorCode::Invalid when the magic is missing or the cluster
    /// size or root folder is not a volume's, and ErrorCode::Truncated when the stream is shorter than the volume's
    /// superblock and FAT.
    [[nodiscard]] static std::expected<FatxVolume, Error> open(Stream& stream, std::uint64_t offset,
                                                               std::uint64_t size);

    /// The entries of the root folder, in the directory's order (deleted entries left out).
    [[nodiscard]] std::expected<std::vector<FatxEntry>, Error> root() const;
    /// The entries of `folder`, as root() lists them. Fails with ErrorCode::Invalid when it is not a folder or its
    /// chain is damaged.
    [[nodiscard]] std::expected<std::vector<FatxEntry>, Error> list(const FatxEntry& folder) const;
    /// The bytes of `file`. Fails with ErrorCode::Invalid when it is larger than `limit` bytes, is a folder, or its
    /// chain leaves the volume, loops or ends before its size.
    [[nodiscard]] std::expected<std::vector<std::byte>, Error> read(const FatxEntry& file, std::uint32_t limit) const;

    /// Bytes in a cluster (the superblock's sectors per cluster × 512).
    [[nodiscard]] std::uint32_t clusterBytes() const { return m_clusterBytes; }
    /// Clusters on the volume.
    [[nodiscard]] std::uint32_t clusterCount() const { return m_clusterCount; }
    /// Bytes per FAT entry: 2 or 4.
    [[nodiscard]] std::uint32_t fatEntryBytes() const { return m_entryBytes; }

  private:
    FatxVolume(Stream& stream, std::uint64_t offset) : m_stream(&stream), m_offset(offset) {}

    // The FAT entry of `cluster`: the next cluster of its chain, or an end or free mark.
    [[nodiscard]] std::expected<std::uint32_t, Error> next(std::uint32_t cluster) const;
    // The clusters of the chain that starts at `first`, enough for `bytes` (every cluster to its end when `bytes` is
    // 0 and `toEnd` is set). Refuses a chain that loops, leaves the volume or ends early.
    [[nodiscard]] std::expected<std::vector<std::uint32_t>, Error> chain(std::uint32_t first, std::uint64_t bytes,
                                                                         bool toEnd) const;
    // Reads `bytes` bytes of cluster `cluster` from its start into `out`.
    [[nodiscard]] std::expected<void, Error> readCluster(std::uint32_t cluster, std::span<std::byte> out) const;
    // The entries of the folder whose chain starts at `first`.
    [[nodiscard]] std::expected<std::vector<FatxEntry>, Error> readFolder(std::uint32_t first) const;

    Stream* m_stream;
    std::uint64_t m_offset;
    std::uint32_t m_clusterBytes = 0;
    std::uint32_t m_clusterCount = 0;
    std::uint32_t m_entryBytes = 0;
    std::uint32_t m_rootCluster = 0;
    std::uint64_t m_dataOffset = 0; // cluster 1's offset from the volume's start
};

} // namespace coney::io
