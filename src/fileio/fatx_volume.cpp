// SPDX-License-Identifier: GPL-3.0-or-later
#include "fileio/fatx_volume.h"

#include <algorithm>
#include <array>
#include <format>
#include <span>
#include <unordered_set>

namespace coney::io {

namespace {

// The superblock's size and magic; the FAT starts right after it (docs/research/save.md#xbox-containers).
constexpr std::uint64_t kSuperblockBytes = 0x1000;
constexpr std::array<std::byte, 4> kMagic{std::byte{'F'}, std::byte{'A'}, std::byte{'T'}, std::byte{'X'}};
// The FAT is padded to a whole number of these.
constexpr std::uint64_t kFatAlign = 4096;
// A directory entry's size, its name's longest length, and the marks of its first byte.
constexpr std::size_t kEntryBytes = 64;
constexpr std::size_t kNameMax = 42;
constexpr std::uint8_t kDeleted = 0xe5;
constexpr std::uint8_t kEndMarkFf = 0xff;
constexpr std::uint8_t kEndMarkZero = 0x00;
constexpr std::uint8_t kDirectoryAttribute = 0x10;
// FAT entries at or above these end a chain (16-bit and 32-bit FATs); 0 is a free cluster.
constexpr std::uint32_t kEnd16 = 0xfff0;
constexpr std::uint32_t kEnd32 = 0xfffffff0;
// The cluster count from which the FAT's entries are 32-bit.
constexpr std::uint64_t kWideFatClusters = 0xfff0;
// The longest folder read: 4,096 entries. Coney's choice: a longer chain is taken for damage, so a crafted FAT cannot
// make one folder cost memory without bound (the console's save folders hold a handful of entries).
constexpr std::uint64_t kFolderMaxBytes = 4096 * kEntryBytes;

// A little-endian word of `bytes` at `offset`.
std::uint32_t le32(std::span<const std::byte> bytes, std::size_t offset) {
    return static_cast<std::uint32_t>(bytes[offset]) | (static_cast<std::uint32_t>(bytes[offset + 1]) << 8U) |
           (static_cast<std::uint32_t>(bytes[offset + 2]) << 16U) |
           (static_cast<std::uint32_t>(bytes[offset + 3]) << 24U);
}

// Reads `out.size()` bytes at `position` of `stream`.
std::expected<void, Error> readAt(Stream& stream, std::uint64_t position, std::span<std::byte> out) {
    if (auto moved = stream.seek(position); !moved) {
        return moved;
    }
    return stream.read(out);
}

} // namespace

bool FatxVolume::looksLikeFatx(Stream& stream, std::uint64_t offset) {
    std::array<std::byte, 4> magic{};
    return readAt(stream, offset, magic).has_value() && magic == kMagic;
}

std::expected<FatxVolume, Error> FatxVolume::open(Stream& stream, std::uint64_t offset, std::uint64_t size) {
    std::array<std::byte, 16> super{};
    if (auto read = readAt(stream, offset, super); !read) {
        return std::unexpected(read.error());
    }
    if (!std::equal(kMagic.begin(), kMagic.end(), super.begin())) {
        return fail(ErrorCode::Invalid, "not a FATX volume: no FATX magic");
    }
    FatxVolume volume(stream, offset);
    const std::uint32_t sectors = le32(super, 8);
    // A cluster is a power of two of 512-byte sectors; the console uses 32 (16 KB).
    if (sectors == 0 || sectors > 1024 || (sectors & (sectors - 1)) != 0) {
        return fail(ErrorCode::Invalid, std::format("not a FATX volume: {} sectors per cluster", sectors));
    }
    volume.m_clusterBytes = sectors * 512;
    const std::uint64_t clusters = size / volume.m_clusterBytes;
    if (clusters < 2 || clusters > 0xfffffff0) {
        return fail(ErrorCode::Invalid, std::format("not a FATX volume: {} clusters", clusters));
    }
    volume.m_clusterCount = static_cast<std::uint32_t>(clusters);
    volume.m_entryBytes = clusters < kWideFatClusters ? 2 : 4;
    const std::uint64_t fatBytes = (clusters * volume.m_entryBytes + kFatAlign - 1) / kFatAlign * kFatAlign;
    volume.m_dataOffset = kSuperblockBytes + fatBytes;
    volume.m_rootCluster = le32(super, 12);
    if (volume.m_rootCluster == 0 || volume.m_rootCluster >= volume.m_clusterCount) {
        return fail(ErrorCode::Invalid,
                    std::format("not a FATX volume: root folder at cluster {}", volume.m_rootCluster));
    }
    if (offset + volume.m_dataOffset > stream.size()) {
        return fail(ErrorCode::Truncated, "the FATX volume ends inside its FAT");
    }
    return volume;
}

std::expected<std::uint32_t, Error> FatxVolume::next(std::uint32_t cluster) const {
    std::array<std::byte, 4> entry{};
    const std::span<std::byte> bytes = std::span(entry).first(m_entryBytes);
    if (auto read = readAt(*m_stream, m_offset + kSuperblockBytes + std::uint64_t{cluster} * m_entryBytes, bytes);
        !read) {
        return std::unexpected(read.error());
    }
    return le32(entry, 0);
}

std::expected<std::vector<std::uint32_t>, Error> FatxVolume::chain(std::uint32_t first, std::uint64_t bytes,
                                                                   bool toEnd) const {
    const std::uint64_t needed = (bytes + m_clusterBytes - 1) / m_clusterBytes;
    const std::uint32_t end = m_entryBytes == 2 ? kEnd16 : kEnd32;
    std::vector<std::uint32_t> clusters;
    std::unordered_set<std::uint32_t> seen;
    std::uint32_t cluster = first;
    while (toEnd || clusters.size() < needed) {
        // A chain followed to its end (a folder's) is bounded by `bytes`, the longest it may be.
        if (toEnd && clusters.size() * std::uint64_t{m_clusterBytes} >= bytes) {
            return fail(ErrorCode::Invalid,
                        std::format("the FATX volume is damaged: the folder at cluster {} is longer than {} bytes",
                                    first, bytes));
        }
        if (cluster == 0 || cluster >= m_clusterCount) {
            return fail(ErrorCode::Invalid,
                        std::format("the FATX volume is damaged: a chain reaches cluster {}, outside "
                                    "its {} clusters",
                                    cluster, m_clusterCount));
        }
        if (!seen.insert(cluster).second) {
            return fail(ErrorCode::Invalid,
                        std::format("the FATX volume is damaged: a chain loops at cluster {}", cluster));
        }
        clusters.push_back(cluster);
        const auto link = next(cluster);
        if (!link) {
            return std::unexpected(link.error());
        }
        if (*link >= end) {
            break;
        }
        if (*link == 0) {
            return fail(
                ErrorCode::Invalid,
                std::format("the FATX volume is damaged: the chain from cluster {} meets a free cluster", first));
        }
        cluster = *link;
    }
    // A file's chain must reach its size; a folder's (`toEnd`) ends where its end mark is.
    if (!toEnd && clusters.size() < needed) {
        return fail(ErrorCode::Invalid,
                    std::format("the FATX volume is damaged: the chain from cluster {} ends after {} of {} clusters",
                                first, clusters.size(), needed));
    }
    return clusters;
}

std::expected<void, Error> FatxVolume::readCluster(std::uint32_t cluster, std::span<std::byte> out) const {
    return readAt(*m_stream, m_offset + m_dataOffset + (std::uint64_t{cluster} - 1) * m_clusterBytes, out);
}

std::expected<std::vector<FatxEntry>, Error> FatxVolume::readFolder(std::uint32_t first) const {
    const auto clusters = chain(first, std::max<std::uint64_t>(kFolderMaxBytes, m_clusterBytes), true);
    if (!clusters) {
        return std::unexpected(clusters.error());
    }
    std::vector<FatxEntry> entries;
    std::vector<std::byte> block(m_clusterBytes);
    for (const std::uint32_t cluster : *clusters) {
        if (auto read = readCluster(cluster, block); !read) {
            return std::unexpected(read.error());
        }
        for (std::size_t at = 0; at + kEntryBytes <= block.size(); at += kEntryBytes) {
            const auto length = static_cast<std::uint8_t>(block[at]);
            if (length == kEndMarkFf || length == kEndMarkZero) {
                return entries;
            }
            if (length == kDeleted || length > kNameMax) {
                continue;
            }
            FatxEntry entry;
            entry.name.assign(reinterpret_cast<const char*>(&block[at + 2]), length);
            entry.directory = (static_cast<std::uint8_t>(block[at + 1]) & kDirectoryAttribute) != 0;
            entry.firstCluster = le32(block, at + 0x2c);
            entry.size = entry.directory ? 0 : le32(block, at + 0x30);
            entries.push_back(std::move(entry));
        }
    }
    return entries;
}

std::expected<std::vector<FatxEntry>, Error> FatxVolume::root() const { return readFolder(m_rootCluster); }

std::expected<std::vector<FatxEntry>, Error> FatxVolume::list(const FatxEntry& folder) const {
    if (!folder.directory) {
        return fail(ErrorCode::Invalid, std::format("{} is not a folder", folder.name));
    }
    return readFolder(folder.firstCluster);
}

std::expected<std::vector<std::byte>, Error> FatxVolume::read(const FatxEntry& file, std::uint32_t limit) const {
    if (file.directory) {
        return fail(ErrorCode::Invalid, std::format("{} is a folder", file.name));
    }
    if (file.size > limit) {
        return fail(ErrorCode::Invalid,
                    std::format("{} is {} bytes, more than the {} expected", file.name, file.size, limit));
    }
    std::vector<std::byte> bytes(file.size);
    if (file.size == 0) {
        return bytes;
    }
    const auto clusters = chain(file.firstCluster, file.size, false);
    if (!clusters) {
        return std::unexpected(clusters.error());
    }
    std::size_t at = 0;
    for (const std::uint32_t cluster : *clusters) {
        const std::size_t take = std::min<std::size_t>(m_clusterBytes, bytes.size() - at);
        if (auto read = readCluster(cluster, std::span(bytes).subspan(at, take)); !read) {
            return std::unexpected(read.error());
        }
        at += take;
    }
    return bytes;
}

} // namespace coney::io
