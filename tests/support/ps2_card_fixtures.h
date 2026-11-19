// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Synthetic PS2 memory cards and save exports, built byte by byte for the importer's tests: Sony's card file system
// and the PSU export as docs/research/save.md#import-containers describes them, holding saves the tests make from
// ProfileRecord. Nothing here comes from the game or from a real card (LEGAL.md, "No game data").

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "support/fixtures.h"

namespace coney::test {

/// A file to put on a synthetic card or in an export.
struct CardFile {
    std::string name;
    std::vector<std::byte> data;
};

/// A folder of the card's root, with its files.
struct CardFolder {
    std::string name;
    std::vector<CardFile> files;
};

/// How to lay a synthetic card out.
struct CardLayout {
    bool spares = true;           ///< Each 512-byte page followed by 16 spare bytes, as PCSX2's card files are.
    bool fragmented = false;      ///< Leave a free cluster after each one used, so every chain needs its FAT.
    std::uint32_t clusters = 256; ///< Clusters on the card (1 KB each): 256 KB, enough for a few saves.
};

/// The card file system's directory entry flags (docs/research/save.md#import-containers).
inline constexpr std::uint16_t kModeRead = 0x0001;
inline constexpr std::uint16_t kModeWrite = 0x0002;
inline constexpr std::uint16_t kModeExecute = 0x0004;
inline constexpr std::uint16_t kModeFile = 0x0010;
inline constexpr std::uint16_t kModeFolder = 0x0020;
inline constexpr std::uint16_t kModeExists = 0x8000;
/// A folder's and a file's mode as the console writes them.
inline constexpr std::uint16_t kFolderMode = kModeExists | 0x0400 | kModeFolder | kModeExecute | kModeWrite | kModeRead;
inline constexpr std::uint16_t kFileMode = kModeExists | 0x0400 | kModeFile | kModeExecute | kModeWrite | kModeRead;

/// One 512-byte directory entry: mode, length, first cluster and name; the dates and attributes left zero.
inline std::vector<std::byte> cardEntry(std::uint16_t mode, std::uint32_t length, std::uint32_t cluster,
                                        std::string_view name) {
    Bytes entry;
    entry.u16(mode).u16(0).u32(length).fill(8, 0).u32(cluster).u32(0).fill(8, 0).u32(0).fill(28, 0);
    entry.text(name).fill(512 - 0x40 - name.size(), 0);
    return entry.data();
}

/// A formatted card holding `folders` in its root: a superblock in cluster 0, one indirect FAT cluster (1) naming one
/// FAT cluster (2), allocatable clusters from 3, the root directory in the first of them.
inline std::vector<std::byte> buildPs2Card(const std::vector<CardFolder>& folders, const CardLayout& layout = {}) {
    constexpr std::uint32_t kPage = 512;
    constexpr std::uint32_t kCluster = 1024;
    constexpr std::uint32_t kAllocOffset = 3;
    const std::uint32_t usable = layout.clusters - kAllocOffset;
    std::vector<std::vector<std::byte>> clusters(layout.clusters, std::vector<std::byte>(kCluster, std::byte{0}));
    std::vector<std::uint32_t> fat(256, 0x7fffffffU); // free

    // Allocates the clusters for `bytes` and links them; returns the first, counted from the allocation offset.
    std::uint32_t next = 0;
    const auto store = [&](const std::vector<std::byte>& bytes) -> std::uint32_t {
        const std::size_t count = bytes.empty() ? 1 : (bytes.size() + kCluster - 1) / kCluster;
        std::vector<std::uint32_t> chain;
        for (std::size_t i = 0; i < count; ++i) {
            REQUIRE(next < usable);
            chain.push_back(next);
            next += layout.fragmented ? 2 : 1;
        }
        for (std::size_t i = 0; i < chain.size(); ++i) {
            fat.at(chain[i]) = 0x80000000U | (i + 1 < chain.size() ? chain[i + 1] : 0x7fffffffU);
            const std::size_t from = i * kCluster;
            const std::size_t to = std::min(bytes.size(), from + kCluster);
            for (std::size_t b = from; b < to; ++b) {
                clusters.at(kAllocOffset + chain[i]).at(b - from) = bytes[b];
            }
        }
        return chain.front();
    };

    // The root is stored first so that it starts at allocatable cluster 0; its entries are patched in afterwards.
    const std::size_t rootCount = 2 + folders.size();
    const std::uint32_t root = store(std::vector<std::byte>(rootCount * 512, std::byte{0}));
    Bytes rootBytes;
    rootBytes.append(cardEntry(kFolderMode, static_cast<std::uint32_t>(rootCount), 0, "."));
    rootBytes.append(cardEntry(kFolderMode, 0, 0, ".."));
    for (const CardFolder& folder : folders) {
        Bytes dir;
        std::vector<std::pair<std::uint32_t, const CardFile*>> placed;
        placed.reserve(folder.files.size());
        for (const CardFile& file : folder.files) {
            placed.emplace_back(store(file.data), &file);
        }
        dir.append(cardEntry(kFolderMode, static_cast<std::uint32_t>(2 + folder.files.size()), 0, "."));
        dir.append(cardEntry(kFolderMode, 0, 0, ".."));
        for (const auto& [cluster, file] : placed) {
            dir.append(cardEntry(kFileMode, static_cast<std::uint32_t>(file->data.size()), cluster, file->name));
        }
        const std::uint32_t first = store(dir.data());
        rootBytes.append(
            cardEntry(kFolderMode, static_cast<std::uint32_t>(2 + folder.files.size()), first, folder.name));
    }
    // Writes the root's entries over its chain, which store() laid out contiguously or every other cluster.
    const std::vector<std::byte>& rootData = rootBytes.data();
    for (std::size_t b = 0; b < rootData.size(); ++b) {
        const std::size_t index = b / kCluster;
        const std::uint32_t cluster = root + static_cast<std::uint32_t>(index) * (layout.fragmented ? 2 : 1);
        clusters.at(kAllocOffset + cluster).at(b % kCluster) = rootData[b];
    }

    // The superblock, the indirect cluster and the FAT cluster.
    Bytes super;
    super.text("Sony PS2 Memory Card Format ").text("1.2.0.0").fill(5, 0);
    super.u16(kPage).u16(2).u16(16).u16(0xff00).u32(layout.clusters).u32(kAllocOffset).u32(layout.clusters - 1);
    super.u32(root).u32(0).u32(0).fill(8, 0);
    super.u32(1).fill(std::size_t{31} * 4, 0); // the indirect FAT clusters: just cluster 1
    super.fill(std::size_t{32} * 4, 0xff).u8(2).u8(0x52);
    const std::vector<std::byte>& superBytes = super.data();
    std::copy(superBytes.begin(), superBytes.end(), clusters[0].begin());
    Bytes indirect;
    indirect.u32(2);
    std::copy(indirect.data().begin(), indirect.data().end(), clusters[1].begin());
    Bytes fatBytes;
    for (const std::uint32_t entry : fat) {
        fatBytes.u32(entry);
    }
    std::copy(fatBytes.data().begin(), fatBytes.data().end(), clusters[2].begin());

    // The image: every page, with its spare bytes when asked for.
    Bytes image;
    for (const std::vector<std::byte>& cluster : clusters) {
        for (std::uint32_t page = 0; page < 2; ++page) {
            image.append(std::span(cluster).subspan(std::size_t{page} * kPage, kPage));
            if (layout.spares) {
                image.fill(16, 0x5a);
            }
        }
    }
    return image.data();
}

/// A PSU export of `folder`: its entry, "." and "..", then each file's entry and bytes padded to 1,024.
inline std::vector<std::byte> buildPsu(const CardFolder& folder) {
    Bytes psu;
    psu.append(cardEntry(kFolderMode, static_cast<std::uint32_t>(2 + folder.files.size()), 0, folder.name));
    psu.append(cardEntry(kFolderMode, 0, 0, "."));
    psu.append(cardEntry(kFolderMode, 0, 0, ".."));
    for (const CardFile& file : folder.files) {
        psu.append(cardEntry(kFileMode, static_cast<std::uint32_t>(file.data.size()), 0, file.name));
        psu.append(file.data);
        psu.fill((1024 - file.data.size() % 1024) % 1024, 0);
    }
    return psu.data();
}

} // namespace coney::test
