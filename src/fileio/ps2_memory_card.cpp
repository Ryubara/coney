// SPDX-License-Identifier: GPL-3.0-or-later
#include "fileio/ps2_memory_card.h"

#include <algorithm>
#include <cstring>
#include <format>
#include <utility>

#include "fileio/reader.h"

namespace coney::io {

namespace {

// The superblock's fields this reader needs, by offset (docs/research/save.md#import-containers).
constexpr std::size_t kPageBytesAt = 0x28;
constexpr std::size_t kIndirectListAt = 0x50;
constexpr std::size_t kIndirectListSize = 32;
constexpr std::size_t kSuperblockBytes = 0x154;
// A directory entry's size and its fields' offsets.
constexpr std::size_t kEntryBytes = 512;
constexpr std::size_t kEntryNameAt = 0x40;
constexpr std::size_t kEntryNameBytes = 32;
// The mode bits: the entry exists, it is a folder, it is a file.
constexpr std::uint16_t kExists = 0x8000;
constexpr std::uint16_t kFolder = 0x0020;
constexpr std::uint16_t kFile = 0x0010;
// A FAT entry: the high bit marks a used cluster, the rest is the next one, all ones for the chain's end.
constexpr std::uint32_t kUsed = 0x80000000U;
constexpr std::uint32_t kNextMask = 0x7fffffffU;
constexpr std::uint32_t kChainEnd = 0x7fffffffU;
// The chain of a PSU export's files is padded to whole clusters of 1,024 bytes.
constexpr std::size_t kPsuPadding = 1024;

// A little-endian word read from bytes the caller has checked are there.
std::uint32_t wordAt(std::span<const std::byte> bytes, std::size_t offset) {
    return std::to_integer<std::uint32_t>(bytes[offset]) | (std::to_integer<std::uint32_t>(bytes[offset + 1]) << 8) |
           (std::to_integer<std::uint32_t>(bytes[offset + 2]) << 16) |
           (std::to_integer<std::uint32_t>(bytes[offset + 3]) << 24);
}

// A little-endian half-word read from bytes the caller has checked are there.
std::uint16_t halfAt(std::span<const std::byte> bytes, std::size_t offset) {
    return static_cast<std::uint16_t>(std::to_integer<std::uint16_t>(bytes[offset]) |
                                      (std::to_integer<std::uint16_t>(bytes[offset + 1]) << 8));
}

// The name field of a directory entry: up to its first zero byte.
std::string entryName(std::span<const std::byte> entry) {
    std::string name;
    for (std::size_t i = 0; i < kEntryNameBytes; ++i) {
        const auto c = std::to_integer<char>(entry[kEntryNameAt + i]);
        if (c == '\0') {
            break;
        }
        name.push_back(c);
    }
    return name;
}

} // namespace

const Ps2SaveFile* Ps2SaveFolder::file(std::string_view fileName) const {
    const auto found = std::ranges::find(files, fileName, &Ps2SaveFile::name);
    return found != files.end() ? &*found : nullptr;
}

bool Ps2MemoryCard::looksLikeCard(std::span<const std::byte> image) {
    return image.size() >= kMagic.size() && std::memcmp(image.data(), kMagic.data(), kMagic.size()) == 0;
}

std::expected<Ps2MemoryCard, Error> Ps2MemoryCard::parse(std::span<const std::byte> image) {
    if (image.size() < kSuperblockBytes) {
        return fail(ErrorCode::Truncated, std::format("a memory card image is at least {} bytes; this one is {}",
                                                      kSuperblockBytes, image.size()));
    }
    if (!looksLikeCard(image)) {
        return fail(ErrorCode::Invalid, "not a PS2 memory card image: it does not start with the card's format name");
    }
    Ps2MemoryCard card(image);
    card.m_pageBytes = halfAt(image, kPageBytesAt);
    card.m_pagesPerCluster = halfAt(image, kPageBytesAt + 2);
    card.m_clusterCount = wordAt(image, 0x30);
    card.m_allocOffset = wordAt(image, 0x34);
    card.m_rootCluster = wordAt(image, 0x3c);
    // Every card the console formats has 512-byte pages, two to a cluster; anything else is not one we can read.
    if (card.m_pageBytes != 512 || card.m_pagesPerCluster != 2 || card.m_clusterCount == 0 ||
        card.m_allocOffset >= card.m_clusterCount) {
        return fail(ErrorCode::Invalid,
                    std::format("not a PS2 memory card image: {}-byte pages, {} to a cluster, {} clusters",
                                card.m_pageBytes, card.m_pagesPerCluster, card.m_clusterCount));
    }
    // The size says whether each page carries its error-correction spare (a sixteenth of the page) or not.
    const std::uint64_t pages = std::uint64_t{card.m_clusterCount} * card.m_pagesPerCluster;
    const std::uint32_t spare = card.m_pageBytes / 32;
    if (image.size() == pages * (card.m_pageBytes + spare)) {
        card.m_spareBytes = spare;
    } else if (image.size() != pages * card.m_pageBytes) {
        return fail(ErrorCode::Invalid,
                    std::format("the memory card image's size is {} bytes; its superblock describes {} with the "
                                "pages' spares or {} without",
                                image.size(), pages * (card.m_pageBytes + spare), pages * card.m_pageBytes));
    }
    for (std::size_t i = 0; i < kIndirectListSize; ++i) {
        const std::uint32_t cluster = wordAt(image, kIndirectListAt + i * 4);
        if (cluster == 0) {
            break;
        }
        card.m_indirect.push_back(cluster);
    }
    return card;
}

std::expected<std::vector<std::byte>, Error> Ps2MemoryCard::readCluster(std::uint32_t cluster) const {
    if (cluster >= m_clusterCount) {
        return fail(ErrorCode::Invalid,
                    std::format("the memory card names cluster {}, past its {} clusters", cluster, m_clusterCount));
    }
    std::vector<std::byte> bytes;
    bytes.reserve(std::size_t{m_pageBytes} * m_pagesPerCluster);
    const std::size_t stride = std::size_t{m_pageBytes} + m_spareBytes;
    for (std::uint32_t page = 0; page < m_pagesPerCluster; ++page) {
        // parse() checked the image holds every cluster's pages.
        const std::size_t at = (std::size_t{cluster} * m_pagesPerCluster + page) * stride;
        const auto data = m_image.subspan(at, m_pageBytes);
        bytes.insert(bytes.end(), data.begin(), data.end());
    }
    return bytes;
}

std::expected<std::uint32_t, Error> Ps2MemoryCard::fatEntry(std::uint32_t cluster) const {
    // A cluster of 256 words: the FAT entry is in the FAT cluster that the indirect cluster names.
    const std::uint32_t perCluster = m_pageBytes * m_pagesPerCluster / 4;
    const std::uint32_t fatIndex = cluster / perCluster;
    const std::uint32_t indirectIndex = fatIndex / perCluster;
    if (indirectIndex >= m_indirect.size()) {
        return fail(ErrorCode::Invalid, std::format("the memory card's FAT does not reach cluster {}", cluster));
    }
    const auto indirect = readCluster(m_indirect[indirectIndex]);
    if (!indirect) {
        return std::unexpected(indirect.error());
    }
    const auto fat = readCluster(wordAt(*indirect, (fatIndex % perCluster) * 4));
    if (!fat) {
        return std::unexpected(fat.error());
    }
    return wordAt(*fat, (cluster % perCluster) * 4);
}

std::expected<std::vector<std::byte>, Error> Ps2MemoryCard::readChain(std::uint32_t first, std::size_t length) const {
    const std::size_t clusterBytes = std::size_t{m_pageBytes} * m_pagesPerCluster;
    // A chain longer than the card has clusters must loop: refuse a length the card could not hold.
    if (length > std::size_t{m_clusterCount} * clusterBytes) {
        return fail(ErrorCode::Invalid, std::format("a file on the memory card claims {} bytes, more than the card "
                                                    "holds",
                                                    length));
    }
    std::vector<std::byte> bytes;
    bytes.reserve(length);
    // A cluster met twice means the chain loops: a damaged card, never followed round again.
    std::vector<bool> visited(m_clusterCount, false);
    std::uint32_t cluster = first;
    while (bytes.size() < length) {
        if (cluster >= m_clusterCount || visited[cluster]) {
            return fail(ErrorCode::Invalid, std::format("a file's cluster chain on the memory card loops or leaves "
                                                        "the card at cluster {}",
                                                        cluster));
        }
        visited[cluster] = true;
        const auto data = readCluster(m_allocOffset + cluster);
        if (!data) {
            return std::unexpected(data.error());
        }
        const std::size_t take = std::min(clusterBytes, length - bytes.size());
        bytes.insert(bytes.end(), data->begin(), data->begin() + static_cast<std::ptrdiff_t>(take));
        if (bytes.size() == length) {
            break;
        }
        // Follow the FAT; a free cluster or the chain's end before the length is a damaged card.
        const auto entry = fatEntry(cluster);
        if (!entry) {
            return std::unexpected(entry.error());
        }
        if ((*entry & kUsed) == 0 || (*entry & kNextMask) == kChainEnd) {
            return fail(ErrorCode::Invalid,
                        std::format("a file on the memory card ends after {} of its {} bytes", bytes.size(), length));
        }
        cluster = *entry & kNextMask;
    }
    return bytes;
}

std::expected<std::vector<Ps2MemoryCard::Entry>, Error> Ps2MemoryCard::readDirectory(std::uint32_t first,
                                                                                     std::uint32_t count) const {
    const auto bytes = readChain(first, std::size_t{count} * kEntryBytes);
    if (!bytes) {
        return std::unexpected(bytes.error());
    }
    std::vector<Entry> entries;
    for (std::uint32_t i = 0; i < count; ++i) {
        const auto entry = std::span(*bytes).subspan(i * kEntryBytes, kEntryBytes);
        entries.push_back({halfAt(entry, 0), wordAt(entry, 4), wordAt(entry, 0x10), entryName(entry)});
    }
    return entries;
}

std::expected<std::vector<Ps2MemoryCard::Entry>, Error> Ps2MemoryCard::rootEntries() const {
    // The root's own "." entry, the first of its first cluster, holds how many entries the root has.
    const auto dot = readDirectory(m_rootCluster, 1);
    if (!dot) {
        return std::unexpected(dot.error());
    }
    return readDirectory(m_rootCluster, dot->front().length);
}

std::expected<std::vector<std::string>, Error> Ps2MemoryCard::folders() const {
    const auto entries = rootEntries();
    if (!entries) {
        return std::unexpected(entries.error());
    }
    std::vector<std::string> names;
    for (const Entry& entry : *entries) {
        if ((entry.mode & (kExists | kFolder)) == (kExists | kFolder) && entry.name != "." && entry.name != "..") {
            names.push_back(entry.name);
        }
    }
    return names;
}

std::expected<Ps2SaveFolder, Error> Ps2MemoryCard::folder(std::string_view name) const {
    const auto entries = rootEntries();
    if (!entries) {
        return std::unexpected(entries.error());
    }
    const auto found = std::ranges::find_if(*entries, [name](const Entry& entry) {
        return (entry.mode & (kExists | kFolder)) == (kExists | kFolder) && entry.name == name;
    });
    if (found == entries->end()) {
        return fail(ErrorCode::NotFound, std::format("the memory card has no folder {}", name));
    }
    const auto inside = readDirectory(found->cluster, found->length);
    if (!inside) {
        return std::unexpected(inside.error());
    }
    Ps2SaveFolder folder{std::string(name), {}};
    for (const Entry& entry : *inside) {
        if ((entry.mode & (kExists | kFile)) != (kExists | kFile)) {
            continue; // "." and "..", and deleted entries
        }
        auto data = readChain(entry.cluster, entry.length);
        if (!data) {
            return std::unexpected(
                Error{data.error().code, std::format("{}/{}: {}", name, entry.name, data.error().message)});
        }
        folder.files.push_back({entry.name, std::move(*data)});
    }
    return folder;
}

bool looksLikePsu(std::span<const std::byte> bytes) {
    if (bytes.size() < 2 * kEntryBytes) {
        return false;
    }
    const std::uint16_t mode = halfAt(bytes, 0);
    return (mode & (kExists | kFolder)) == (kExists | kFolder) &&
           entryName(bytes.subspan(kEntryBytes, kEntryBytes)) == ".";
}

std::expected<Ps2SaveFolder, Error> readPsuExport(std::span<const std::byte> bytes) {
    Reader reader(bytes);
    // Reads the next 512-byte entry.
    const auto nextEntry = [&reader]() -> std::expected<std::span<const std::byte>, Error> {
        auto entry = reader.readBytes(kEntryBytes);
        if (!entry) {
            return fail(ErrorCode::Truncated, "the PSU export ends inside a directory entry");
        }
        return *entry;
    };
    const auto head = nextEntry();
    if (!head) {
        return std::unexpected(head.error());
    }
    if (!looksLikePsu(bytes)) {
        return fail(ErrorCode::Invalid, "not a PSU export: it does not start with a save folder's entry and its \".\"");
    }
    const std::uint32_t count = wordAt(*head, 4);
    Ps2SaveFolder folder{entryName(*head), {}};
    for (std::uint32_t i = 0; i < count; ++i) {
        const auto entry = nextEntry();
        if (!entry) {
            return std::unexpected(entry.error());
        }
        if ((halfAt(*entry, 0) & (kExists | kFile)) != (kExists | kFile)) {
            continue; // "." and ".." carry no bytes
        }
        const std::uint32_t length = wordAt(*entry, 4);
        const std::size_t padded = (std::size_t{length} + kPsuPadding - 1) / kPsuPadding * kPsuPadding;
        const auto data = reader.readBytes(length);
        if (!data) {
            return fail(ErrorCode::Truncated, std::format("the PSU export ends inside {}", entryName(*entry)));
        }
        folder.files.push_back({entryName(*entry), std::vector<std::byte>(data->begin(), data->end())});
        // The last file's padding may be left out by some tools.
        if (const std::size_t skip = std::min(padded - length, reader.remaining()); skip > 0) {
            (void)reader.readBytes(skip);
        }
    }
    return folder;
}

} // namespace coney::io
