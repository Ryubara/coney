// SPDX-License-Identifier: GPL-3.0-or-later
#include "fileio/qcow2_image.h"

#include <algorithm>
#include <array>
#include <format>

namespace coney::io {

namespace {

// The header's fields this reader uses, by their offsets (docs/research/save.md#xbox-containers): big-endian.
constexpr std::size_t kHeaderV2Bytes = 72;
constexpr std::size_t kHeaderV3Bytes = 104;
// Incompatible features of version 3: bit 0 the image was not closed cleanly (its refcounts may be stale, which a
// reader does not use), bit 1 corrupt, bit 2 an external data file, bit 3 a compression type field, bit 4 extended L2
// entries. A reader must refuse a bit it does not know.
constexpr std::uint64_t kDirty = 1U << 0U;
constexpr std::uint64_t kCompressionType = 1U << 3U;
constexpr std::uint64_t kKnownHarmless = kDirty | kCompressionType;
// Bits 9-55 of a table entry hold a cluster's offset in the file; bit 62 of an L2 entry marks a compressed cluster,
// and bit 0 (version 3) one that reads as zeros.
constexpr std::uint64_t kOffsetMask = 0x00fffffffffffe00ULL;
constexpr std::uint64_t kCompressed = 1ULL << 62U;
constexpr std::uint64_t kZeroes = 1ULL;
// Cluster sizes the format allows: 512 bytes to 2 MB.
constexpr std::uint32_t kMinClusterBits = 9;
constexpr std::uint32_t kMaxClusterBits = 21;
// The most L1 entries read: enough for a 32 TB disk of 64 KB clusters, far past any Xbox disk.
constexpr std::uint64_t kMaxL1Entries = std::uint64_t{1} << 22U;

// A big-endian value of `bytes` at `offset`.
std::uint64_t be(std::span<const std::byte> bytes, std::size_t offset, std::size_t width) {
    std::uint64_t value = 0;
    for (std::size_t i = 0; i < width; ++i) {
        value = (value << 8U) | static_cast<std::uint64_t>(bytes[offset + i]);
    }
    return value;
}

// Reads `out.size()` bytes at `position` of `stream`.
std::expected<void, Error> readAt(Stream& stream, std::uint64_t position, std::span<std::byte> out) {
    if (auto moved = stream.seek(position); !moved) {
        return moved;
    }
    return stream.read(out);
}

} // namespace

bool Qcow2Image::looksLikeQcow2(Stream& stream) {
    std::array<std::byte, 4> magic{};
    return readAt(stream, 0, magic).has_value() && be(magic, 0, 4) == kMagic;
}

std::expected<Qcow2Image, Error> Qcow2Image::open(Stream& file) {
    std::array<std::byte, kHeaderV3Bytes> header{};
    const std::size_t available = static_cast<std::size_t>(std::min<std::uint64_t>(file.size(), header.size()));
    if (available < kHeaderV2Bytes) {
        return fail(ErrorCode::Truncated, "the qcow2 image ends inside its header");
    }
    if (auto read = readAt(file, 0, std::span(header).first(available)); !read) {
        return std::unexpected(read.error());
    }
    if (be(header, 0, 4) != kMagic) {
        return fail(ErrorCode::Invalid, "not a qcow2 image: no QFI magic");
    }
    Qcow2Image image(file);
    image.m_version = static_cast<std::uint32_t>(be(header, 4, 4));
    if (image.m_version != 2 && image.m_version != 3) {
        return fail(ErrorCode::Invalid,
                    std::format("a qcow2 image of version {}; Coney reads versions 2 and 3", image.m_version));
    }
    if (be(header, 8, 8) != 0) {
        return fail(ErrorCode::Invalid, "the qcow2 image has a backing file, which Coney does not read");
    }
    image.m_clusterBits = static_cast<std::uint32_t>(be(header, 20, 4));
    if (image.m_clusterBits < kMinClusterBits || image.m_clusterBits > kMaxClusterBits) {
        return fail(ErrorCode::Invalid, std::format("not a qcow2 image: {} cluster bits", image.m_clusterBits));
    }
    image.m_clusterBytes = std::uint32_t{1} << image.m_clusterBits;
    image.m_virtualSize = be(header, 24, 8);
    if (be(header, 32, 4) != 0) {
        return fail(ErrorCode::Invalid, "the qcow2 image is encrypted, which Coney does not read");
    }
    if (image.m_version == 3) {
        if (available < kHeaderV3Bytes) {
            return fail(ErrorCode::Truncated, "the qcow2 image ends inside its header");
        }
        const std::uint64_t incompatible = be(header, 72, 8);
        if ((incompatible & ~kKnownHarmless) != 0) {
            return fail(ErrorCode::Invalid,
                        std::format("the qcow2 image uses features Coney does not read (incompatible bits {:#x}: a "
                                    "corrupt mark, an external data file or extended L2 entries)",
                                    incompatible & ~kKnownHarmless));
        }
    }
    const std::uint64_t l1Entries = be(header, 36, 4);
    const std::uint64_t l2Entries = image.m_clusterBytes / 8;
    const std::uint64_t clusters = (image.m_virtualSize + image.m_clusterBytes - 1) / image.m_clusterBytes;
    const std::uint64_t needed = (clusters + l2Entries - 1) / l2Entries;
    if (l1Entries > kMaxL1Entries || l1Entries < std::min(needed, kMaxL1Entries)) {
        return fail(ErrorCode::Invalid, std::format("not a qcow2 image: an L1 table of {} entries", l1Entries));
    }
    std::vector<std::byte> l1(static_cast<std::size_t>(l1Entries * 8));
    if (auto read = readAt(file, be(header, 40, 8), l1); !read) {
        return std::unexpected(read.error());
    }
    image.m_l1.resize(static_cast<std::size_t>(l1Entries));
    for (std::size_t i = 0; i < image.m_l1.size(); ++i) {
        image.m_l1[i] = be(l1, i * 8, 8);
    }
    return image;
}

std::expected<std::uint64_t, Error> Qcow2Image::hostOffset(std::uint64_t cluster) {
    const std::uint64_t l2Entries = m_clusterBytes / 8;
    const std::uint64_t l1Index = cluster / l2Entries;
    if (l1Index >= m_l1.size()) {
        return fail(ErrorCode::Invalid, "the qcow2 image's L1 table is shorter than its disk");
    }
    const std::uint64_t tableOffset = m_l1[l1Index] & kOffsetMask;
    if (tableOffset == 0) {
        return 0; // no L2 table: the whole stretch was never written
    }
    auto table = m_l2.find(tableOffset);
    if (table == m_l2.end()) {
        std::vector<std::byte> bytes(m_clusterBytes);
        if (auto read = readAt(*m_file, tableOffset, bytes); !read) {
            return fail(ErrorCode::Invalid,
                        std::format("the qcow2 image is damaged: an L2 table at {:#x} cannot be read", tableOffset));
        }
        std::vector<std::uint64_t> entries(static_cast<std::size_t>(l2Entries));
        for (std::size_t i = 0; i < entries.size(); ++i) {
            entries[i] = be(bytes, i * 8, 8);
        }
        table = m_l2.emplace(tableOffset, std::move(entries)).first;
    }
    const std::uint64_t entry = table->second[static_cast<std::size_t>(cluster % l2Entries)];
    if ((entry & kCompressed) != 0) {
        return fail(ErrorCode::Invalid, "the qcow2 image holds compressed clusters, which Coney does not read");
    }
    if (m_version == 3 && (entry & kZeroes) != 0) {
        return 0;
    }
    return entry & kOffsetMask;
}

std::expected<void, Error> Qcow2Image::read(std::span<std::byte> destination) {
    if (destination.size() > m_virtualSize - m_position) {
        return fail(ErrorCode::Truncated, "read past the end of the qcow2 image's disk");
    }
    std::size_t done = 0;
    while (done < destination.size()) {
        const std::uint64_t position = m_position + done;
        const std::uint64_t within = position & (m_clusterBytes - 1);
        const std::size_t take =
            static_cast<std::size_t>(std::min<std::uint64_t>(m_clusterBytes - within, destination.size() - done));
        const auto host = hostOffset(position >> m_clusterBits);
        if (!host) {
            return std::unexpected(host.error());
        }
        const std::span<std::byte> out = destination.subspan(done, take);
        if (*host == 0) {
            std::ranges::fill(out, std::byte{0});
        } else if (auto read = readAt(*m_file, *host + within, out); !read) {
            return fail(ErrorCode::Invalid,
                        std::format("the qcow2 image is damaged: a cluster at {:#x} cannot be read", *host));
        }
        done += take;
    }
    m_position += destination.size();
    return {};
}

std::expected<void, Error> Qcow2Image::seek(std::uint64_t position) {
    if (position > m_virtualSize) {
        return fail(ErrorCode::Truncated, "seek past the end of the qcow2 image's disk");
    }
    m_position = position;
    return {};
}

} // namespace coney::io
