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

namespace coney::io {

/// One file of a PS2 save folder: its name on the card and its bytes.
struct Ps2SaveFile {
    std::string name;            ///< At most 31 characters, as the card's directory entry holds it.
    std::vector<std::byte> data; ///< The file's bytes, exactly its length.
};

/// One save folder of a PS2 memory card (`BASLUS-21215`): its name and its files, in the card's directory order.
///
/// Research: docs/research/save.md#import-containers
struct Ps2SaveFolder {
    std::string name;               ///< The folder's name, `B` + region + product code for a game's save.
    std::vector<Ps2SaveFile> files; ///< Its files ("." and ".." left out).

    /// The file called `fileName` (exact bytes), or null.
    [[nodiscard]] const Ps2SaveFile* file(std::string_view fileName) const;
};

/// A PS2 memory card image as the players have one: the 8 MB card file PCSX2 keeps (`Mcd001.ps2`, the card's raw pages
/// with their 16-byte error-correction spares) or a dump of a real card without the spares (card tools and the
/// MemCard PRO and SD2PSX adapters write those), of any size the superblock describes. It reads Sony's card file
/// system: the superblock, the two-level table of FAT clusters, the cluster chains and the 512-byte directory entries.
/// Only reading: the image is never changed, and the error-correction bytes are not checked.
///
/// Research: docs/research/save.md#import-containers
/// @coney-only reads the console's memory card format for the importer; the game reaches the card through libmc
class Ps2MemoryCard {
  public:
    /// The 28 bytes every formatted card starts with.
    static constexpr std::string_view kMagic = "Sony PS2 Memory Card Format ";

    /// Whether `image` starts with kMagic: a card image, to be read with parse().
    [[nodiscard]] static bool looksLikeCard(std::span<const std::byte> image);

    /// Reads the superblock of `image`, which must outlive the card. Fails with ErrorCode::Invalid when the magic is
    /// missing, the page or cluster sizes are not a card's, or the image's size is neither the card's pages with their
    /// spares nor without them; ErrorCode::Truncated when it is too short for a superblock.
    [[nodiscard]] static std::expected<Ps2MemoryCard, Error> parse(std::span<const std::byte> image);

    /// The names of the folders in the card's root directory, in its order.
    [[nodiscard]] std::expected<std::vector<std::string>, Error> folders() const;
    /// The folder `name` of the root directory with every file in it. Fails with ErrorCode::NotFound when the root has
    /// no such folder, and ErrorCode::Invalid when a cluster chain leaves the card, loops or ends before a file's
    /// length.
    [[nodiscard]] std::expected<Ps2SaveFolder, Error> folder(std::string_view name) const;

    /// Whether the image holds the pages' 16-byte error-correction spares (PCSX2's card files do).
    [[nodiscard]] bool hasSpares() const { return m_spareBytes != 0; }
    /// Clusters on the card, from the superblock.
    [[nodiscard]] std::uint32_t clusterCount() const { return m_clusterCount; }

  private:
    // One 512-byte directory entry: what the importer needs of it.
    struct Entry {
        std::uint16_t mode = 0;
        std::uint32_t length = 0;  // bytes of a file, entries of a folder
        std::uint32_t cluster = 0; // the first cluster, counted from the allocation offset
        std::string name;
    };

    explicit Ps2MemoryCard(std::span<const std::byte> image) : m_image(image) {}

    // The bytes of cluster `cluster` (counted from the card's start), its pages' data without the spares.
    [[nodiscard]] std::expected<std::vector<std::byte>, Error> readCluster(std::uint32_t cluster) const;
    // The FAT entry of allocatable cluster `cluster`, through the indirect table.
    [[nodiscard]] std::expected<std::uint32_t, Error> fatEntry(std::uint32_t cluster) const;
    // The first `length` bytes of the chain that starts at allocatable cluster `first`.
    [[nodiscard]] std::expected<std::vector<std::byte>, Error> readChain(std::uint32_t first, std::size_t length) const;
    // The `count` directory entries of the folder whose chain starts at `first`, in order.
    [[nodiscard]] std::expected<std::vector<Entry>, Error> readDirectory(std::uint32_t first,
                                                                         std::uint32_t count) const;
    // The entries of the root directory, whose count is in its own "." entry.
    [[nodiscard]] std::expected<std::vector<Entry>, Error> rootEntries() const;

    std::span<const std::byte> m_image;
    std::uint32_t m_pageBytes = 0;
    std::uint32_t m_pagesPerCluster = 0;
    std::uint32_t m_spareBytes = 0;
    std::uint32_t m_clusterCount = 0;
    std::uint32_t m_allocOffset = 0;
    std::uint32_t m_rootCluster = 0;
    std::vector<std::uint32_t> m_indirect; // the superblock's list of indirect FAT clusters, up to its first 0
};

/// Reads a single-save export in the PSU format (EMS and uLaunchELF's `.psu`, mymc's export): the folder's directory
/// entry, its "." and ".." entries, then each file's entry followed by its bytes padded to 1,024. Fails with
/// ErrorCode::Invalid when it does not start as looksLikePsu() says or a file's length runs past the export, and
/// ErrorCode::Truncated when it ends early.
///
/// Research: docs/research/save.md#import-containers
/// @coney-only reads a card tool's export format for the importer
[[nodiscard]] std::expected<Ps2SaveFolder, Error> readPsuExport(std::span<const std::byte> bytes);

/// Whether `bytes` could be a PSU export, by the rule readPsuExport() reads by: it starts with a save folder's entry
/// that exists and then that folder's "." entry. The size need not be whole entries: the last file's padding may be
/// left out.
/// @coney-only recognises a card tool's export format for the importer
[[nodiscard]] bool looksLikePsu(std::span<const std::byte> bytes);

} // namespace coney::io
