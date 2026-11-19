// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Synthetic Xbox saves and disk images, built byte by byte for the importer's tests: the Xbox build's profile record,
// its save folder (profile.dat, SaveMeta.xbx), a FATX volume, and xemu's qcow2 disk image, as
// docs/research/save.md#xbox-save and #xbox-containers describe them, holding profiles the tests make from
// ProfileRecord. Nothing here comes from the game, a real save or a real disk (LEGAL.md, "No game data").

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "fileio/stream.h"
#include "support/fixtures.h"
#include "warriors/profile_record.h"

namespace coney::test {

/// The Xbox build's Rumble store (0x22c bytes), written field by field: the 512 owned bits, ten 0x30-byte gangs (an
/// in-use byte, a 10-character name at +1, nine member types at +0x0c), then the current gang, the current member and
/// the "changed" byte (docs/research/save.md#xbox-record).
struct XboxRumble {
    struct Gang {
        bool used = false;
        std::string name;
        std::array<std::uint32_t, 9> members{}; // a fresh store is all zeros
    };
    std::array<std::uint8_t, 0x40> owned{};
    std::array<Gang, 10> gangs{};
    std::uint32_t currentGang = 0;
    std::uint32_t currentMember = 0;
    bool changed = false;

    /// The store's 0x22c bytes.
    [[nodiscard]] std::vector<std::byte> bytes() const {
        Bytes out;
        for (const std::uint8_t b : owned) {
            out.u8(b);
        }
        for (const Gang& gang : gangs) {
            out.u8(gang.used ? 1 : 0)
                .text(gang.name.substr(0, 10))
                .fill(11 - std::min<std::size_t>(gang.name.size(), 10), 0);
            for (const std::uint32_t member : gang.members) {
                out.u32(member);
            }
        }
        out.u32(currentGang).u32(currentMember).u8(changed ? 1 : 0).fill(3, 0);
        return out.data();
    }
};

/// The Xbox build's 1,210-byte profile record of `record`, every field at its Xbox place and size
/// (docs/research/save.md#xbox-record), with `rumble` as its Rumble store. The record's own Rumble bytes (PS2 layout)
/// and its Video setting have no place in it.
inline std::vector<std::byte> xboxRecord(const ProfileRecord& record, const XboxRumble& rumble = {}) {
    const SavedProgress& p = record.progress;
    Bytes out;
    out.u32(ProfileRecord::kVersion)
        .text(record.name.substr(0, 8))
        .fill(8 - std::min<std::size_t>(record.name.size(), 8), 0);
    out.u8(record.hardcoreFinished ? 1 : 0);
    for (const MissionBest& best : p.missionBests) {
        out.u32(best.bestScore).u32(best.total).u8(best.grade).u8(best.percent).u8(best.rest[0]).u8(best.rest[1]);
    }
    for (const std::uint8_t b : p.lockedBits) {
        out.u8(b);
    }
    for (const std::uint8_t b : p.newBits) {
        out.u8(b);
    }
    out.append(rumble.bytes());
    for (const std::uint8_t b : p.rumbleBits) {
        out.u8(b);
    }
    out.u32(p.bankedMoney);
    out.u32(p.optionWord0).u8(record.brightness).fill(3, 0);
    out.u32(std::bit_cast<std::uint32_t>(p.soundFxVolume)).u32(std::bit_cast<std::uint32_t>(p.musicVolume));
    out.u32(p.optionWord10);
    for (const PadOptions& pad : p.pads) {
        out.u8(pad.invertCamera ? 1 : 0).u8(pad.autoAdjustCamera ? 1 : 0).u8(pad.vibration ? 1 : 0);
    }
    out.u8(p.splitScreenMerge ? 1 : 0).u8(record.subtitles ? 1 : 0).u8(p.proLogic2 ? 1 : 0);
    out.u32(record.difficulty).u32(p.stateWord0);
    for (const std::uint8_t b : p.scriptFlags) {
        out.u8(b);
    }
    REQUIRE(out.size() == 0x4ba);
    return out.data();
}

/// An Xbox save's data file: the record and a 20-byte signature after it (a made-up pattern: Coney does not check it).
inline std::vector<std::byte> xboxDataFile(const std::vector<std::byte>& record) {
    Bytes out;
    out.append(record).fill(20, 0xa5);
    return out.data();
}

/// A SaveMeta.xbx naming the save `name`: UTF-16 with its byte-order mark, `Name=<name>` and a line break.
inline std::vector<std::byte> saveMeta(std::string_view name) {
    Bytes out;
    out.u16(0xfeff);
    for (const char c : std::string("Name=") + std::string(name) + "\r\n") {
        out.u16(static_cast<std::uint8_t>(c));
    }
    return out.data();
}

/// A file or folder to put on a synthetic FATX volume: its path from the root (`UDATA/5454009b/ABC/profile.dat`) and,
/// for a file, its bytes. A folder is made for every path's parents.
struct FatxFile {
    std::string path;
    std::vector<std::byte> data;
};

/// A synthetic FATX volume (docs/research/save.md#xbox-containers): the 4 KB superblock (magic, volume id, sectors per
/// cluster, root cluster 1), the FAT at 0x1000 rounded up to 4 KB, then the clusters from 1. Each folder takes one
/// cluster; each file as many as it needs, one after another. Returned as the pieces that are not zero, by their offset
/// from the volume's start, so a volume of a hard disk's size costs only what it holds.
inline std::map<std::uint64_t, std::vector<std::byte>>
buildFatx(std::uint64_t volumeBytes, const std::vector<FatxFile>& files, std::uint32_t sectorsPerCluster = 32) {
    const std::uint64_t clusterBytes = std::uint64_t{sectorsPerCluster} * 512;
    const std::uint64_t clusters = volumeBytes / clusterBytes;
    const std::uint32_t entryBytes = clusters < 0xfff0 ? 2 : 4;
    const std::uint64_t fatBytes = (clusters * entryBytes + 4095) / 4096 * 4096;
    const std::uint64_t dataOffset = 0x1000 + fatBytes;
    struct Node {
        std::string name;
        bool folder = true;
        std::vector<std::byte> data;
        std::vector<std::size_t> children;
        std::uint32_t first = 0;
        std::uint32_t count = 0;
    };
    std::vector<Node> nodes{{"", true, {}, {}, 0, 0}};
    // Finds or makes the child `name` of node `parent`.
    auto child = [&nodes](std::size_t parent, const std::string& name, bool folder) {
        for (const std::size_t c : nodes[parent].children) {
            if (nodes[c].name == name) {
                return c;
            }
        }
        nodes.push_back({name, folder, {}, {}, 0, 0});
        nodes[parent].children.push_back(nodes.size() - 1);
        return nodes.size() - 1;
    };
    for (const FatxFile& file : files) {
        std::size_t at = 0;
        std::string_view rest = file.path;
        while (true) {
            const std::size_t slash = rest.find('/');
            if (slash == std::string_view::npos) {
                nodes[child(at, std::string(rest), false)].data = file.data;
                break;
            }
            at = child(at, std::string(rest.substr(0, slash)), true);
            rest.remove_prefix(slash + 1);
        }
    }
    // Clusters in order from 1: each folder one, each file as many as its bytes need (at least one).
    std::uint32_t nextCluster = 1;
    for (Node& node : nodes) {
        node.first = nextCluster;
        node.count = node.folder ? 1
                                 : static_cast<std::uint32_t>(std::max<std::uint64_t>(
                                       1, (node.data.size() + clusterBytes - 1) / clusterBytes));
        nextCluster += node.count;
    }
    REQUIRE(nextCluster < clusters);
    std::map<std::uint64_t, std::vector<std::byte>> pieces;
    Bytes super;
    super.text("FATX").u32(0x12345678).u32(sectorsPerCluster).u32(1).padTo(0x1000);
    pieces[0] = super.data();
    std::vector<std::byte> fat(std::size_t{nextCluster} * entryBytes, std::byte{0});
    auto setFat = [&fat, entryBytes](std::uint32_t index, std::uint32_t value) {
        for (std::uint32_t i = 0; i < entryBytes; ++i) {
            fat[std::size_t{index} * entryBytes + i] = static_cast<std::byte>((value >> (8 * i)) & 0xFFU);
        }
    };
    setFat(0, entryBytes == 2 ? 0xfff8 : 0xfffffff8);
    for (const Node& node : nodes) {
        for (std::uint32_t i = 0; i < node.count; ++i) {
            const bool last = i + 1 == node.count;
            setFat(node.first + i, last ? (entryBytes == 2 ? 0xffff : 0xffffffff) : node.first + i + 1);
        }
        Bytes body;
        if (node.folder) {
            for (const std::size_t c : node.children) {
                const Node& entry = nodes[c];
                body.u8(static_cast<std::uint8_t>(entry.name.size())).u8(entry.folder ? 0x10 : 0x00).text(entry.name);
                body.fill(42 - entry.name.size(), 0xff).u32(entry.first);
                body.u32(entry.folder ? 0 : static_cast<std::uint32_t>(entry.data.size())).fill(12, 0);
            }
            body.u8(0xff).fill(63, 0xff); // the end of the folder
            body.padTo(static_cast<std::size_t>(clusterBytes));
        } else {
            body.append(node.data);
        }
        if (body.size() != 0) {
            pieces[dataOffset + (std::uint64_t{node.first} - 1) * clusterBytes] = body.data();
        }
    }
    pieces[0x1000] = fat;
    return pieces;
}

/// The pieces laid out in one buffer of `size` bytes, zeros between them.
inline std::vector<std::byte> flatten(const std::map<std::uint64_t, std::vector<std::byte>>& pieces, std::size_t size) {
    std::vector<std::byte> out(size, std::byte{0});
    for (const auto& [offset, bytes] : pieces) {
        REQUIRE(offset + bytes.size() <= size);
        std::ranges::copy(bytes, out.begin() + static_cast<std::ptrdiff_t>(offset));
    }
    return out;
}

/// The pieces moved to `base`: a volume's pieces placed on a disk.
inline std::map<std::uint64_t, std::vector<std::byte>>
shifted(const std::map<std::uint64_t, std::vector<std::byte>>& pieces, std::uint64_t base) {
    std::map<std::uint64_t, std::vector<std::byte>> out;
    for (const auto& [offset, bytes] : pieces) {
        out[base + offset] = bytes;
    }
    return out;
}

/// Big-endian writes for the qcow2 header and tables.
class BigEndian {
  public:
    BigEndian& u32(std::uint32_t v) {
        for (int shift = 24; shift >= 0; shift -= 8) {
            m_bytes.u8(static_cast<std::uint8_t>((v >> shift) & 0xFFU));
        }
        return *this;
    }
    BigEndian& u64(std::uint64_t v) {
        return u32(static_cast<std::uint32_t>(v >> 32)).u32(static_cast<std::uint32_t>(v & 0xFFFFFFFFU));
    }
    Bytes& bytes() { return m_bytes; }

  private:
    Bytes m_bytes;
};

/// What to change in a synthetic qcow2 image, to test the refusals.
struct Qcow2Options {
    std::uint32_t version = 3;
    std::uint32_t clusterBits = 16;
    std::uint32_t cryptMethod = 0;
    bool backingFile = false;
    std::uint64_t incompatible = 0;
    bool compressFirst = false; ///< Mark the first data cluster's L2 entry compressed.
};

/// A qcow2 image of a `virtualSize`-byte disk holding `pieces` at their offsets
/// (docs/research/save.md#xbox-containers): the header in cluster 0, the L1 table in cluster 1, an empty refcount table
/// in cluster 2, then for each touched 1 GB-or-so stretch an L2 table and the data clusters. Clusters no piece touches
/// are left unallocated.
inline std::vector<std::byte> buildQcow2(std::uint64_t virtualSize,
                                         const std::map<std::uint64_t, std::vector<std::byte>>& pieces,
                                         const Qcow2Options& options = {}) {
    const std::uint64_t clusterBytes = std::uint64_t{1} << options.clusterBits;
    const std::uint64_t l2Entries = clusterBytes / 8;
    const std::uint64_t virtualClusters = (virtualSize + clusterBytes - 1) / clusterBytes;
    const std::uint64_t l1Size = (virtualClusters + l2Entries - 1) / l2Entries;
    REQUIRE(l1Size * 8 <= clusterBytes);
    // The data of every touched virtual cluster.
    std::map<std::uint64_t, std::vector<std::byte>> clusters;
    for (const auto& [offset, bytes] : pieces) {
        for (std::size_t i = 0; i < bytes.size(); ++i) {
            const std::uint64_t at = offset + i;
            auto& cluster = clusters[at / clusterBytes];
            cluster.resize(static_cast<std::size_t>(clusterBytes), std::byte{0});
            cluster[static_cast<std::size_t>(at % clusterBytes)] = bytes[i];
        }
    }
    std::map<std::uint64_t, std::uint64_t> l2Host;   // L1 index -> host offset of its L2 table
    std::map<std::uint64_t, std::uint64_t> dataHost; // virtual cluster -> host offset
    std::uint64_t nextHost = 3 * clusterBytes;
    for (const auto& [index, data] : clusters) {
        if (!l2Host.contains(index / l2Entries)) {
            l2Host[index / l2Entries] = nextHost;
            nextHost += clusterBytes;
        }
        dataHost[index] = nextHost;
        nextHost += clusterBytes;
    }
    std::vector<std::byte> image(static_cast<std::size_t>(nextHost), std::byte{0});
    auto put = [&image](std::uint64_t at, std::span<const std::byte> bytes) {
        std::ranges::copy(bytes, image.begin() + static_cast<std::ptrdiff_t>(at));
    };
    BigEndian header;
    header.u32(0x514649fb).u32(options.version);
    header.u64(options.backingFile ? 0x200 : 0).u32(options.backingFile ? 4 : 0);
    header.u32(options.clusterBits).u64(virtualSize).u32(options.cryptMethod);
    header.u32(static_cast<std::uint32_t>(l1Size)).u64(clusterBytes).u64(2 * clusterBytes).u32(1);
    header.u32(0).u64(0);
    if (options.version >= 3) {
        header.u64(options.incompatible).u64(0).u64(0).u32(4).u32(104);
    }
    put(0, header.bytes().span());
    if (options.backingFile) {
        put(0x200, std::as_bytes(std::span("base", 4)));
    }
    BigEndian l1;
    for (std::uint64_t i = 0; i < l1Size; ++i) {
        l1.u64(l2Host.contains(i) ? (l2Host[i] | (std::uint64_t{1} << 63)) : 0);
    }
    put(clusterBytes, l1.bytes().span());
    bool first = true;
    for (const auto& [index, host] : dataHost) {
        std::uint64_t entry = host | (std::uint64_t{1} << 63);
        if (first && options.compressFirst) {
            entry = (std::uint64_t{1} << 62) | host;
        }
        first = false;
        BigEndian e;
        e.u64(entry);
        put(l2Host[index / l2Entries] + (index % l2Entries) * 8, e.bytes().span());
        put(host, clusters[index]);
    }
    return image;
}

/// A stream over a sparse disk: `pieces` at their offsets, zeros elsewhere, `size` bytes in all. Stands in for a raw
/// image of a whole hard disk, which would be gigabytes on the test machine's drive.
class SparseStream final : public io::Stream {
  public:
    SparseStream(std::map<std::uint64_t, std::vector<std::byte>> pieces, std::uint64_t size)
        : m_pieces(std::move(pieces)), m_size(size) {}

    // The Stream interface over the sparse disk.
    [[nodiscard]] std::expected<void, Error> read(std::span<std::byte> destination) override {
        if (destination.size() > m_size - m_position) {
            return fail(ErrorCode::Truncated, "past the end");
        }
        std::ranges::fill(destination, std::byte{0});
        const std::uint64_t start = m_position;
        const std::uint64_t end = start + destination.size();
        for (const auto& [offset, bytes] : m_pieces) {
            const std::uint64_t from = std::max(start, offset);
            const std::uint64_t to = std::min(end, offset + bytes.size());
            for (std::uint64_t at = from; at < to; ++at) {
                destination[static_cast<std::size_t>(at - start)] = bytes[static_cast<std::size_t>(at - offset)];
            }
        }
        m_position = end;
        return {};
    }
    [[nodiscard]] std::expected<void, Error> seek(std::uint64_t position) override {
        if (position > m_size) {
            return fail(ErrorCode::Truncated, "past the end");
        }
        m_position = position;
        return {};
    }
    [[nodiscard]] std::uint64_t tell() const override { return m_position; }
    [[nodiscard]] std::uint64_t size() const override { return m_size; }

  private:
    std::map<std::uint64_t, std::vector<std::byte>> m_pieces;
    std::uint64_t m_size;
    std::uint64_t m_position = 0;
};

} // namespace coney::test
