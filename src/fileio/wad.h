// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "core/error.h"
#include "fileio/disc.h"
#include "fileio/file_stream.h"
#include "fileio/wad_index.h"

namespace coney::io {

/// The disc file holding the archive's index.
inline constexpr std::string_view kWadDirFile = "WARRIORS.DIR";
/// The disc file holding every game file.
inline constexpr std::string_view kWadFile = "WARRIORS.WAD";

/// What Wad::useOverrides() found in an overrides folder.
struct WadOverrideReport {
    std::size_t replaced = 0;            ///< Entries now read from a file of the folder.
    std::vector<std::string> unmatched;  ///< File names that name no entry, ignored.
    std::vector<std::string> duplicates; ///< File names of an entry an earlier name (in sorted order) already replaced.
};

/// The game's archive on the player's disc: WARRIORS.DIR parsed into an index, and WARRIORS.WAD read entry by entry.
///
/// It takes the place of the original's WAD object, which reads WARRIORS.DIR at boot and has the IOP keep
/// WARRIORS.WAD open, and of its stream file system, which serves entries by name
/// (docs/research/file-io.md#opening-the-wad-at-boot). Every read here is a plain synchronous read of the disc.
class Wad {
  public:
    /// Reads and validates the index of `disc`. Fails with ErrorCode::NotFound when the disc lacks WARRIORS.DIR or
    /// WARRIORS.WAD, and as WadIndex::parse() does when the index is damaged.
    [[nodiscard]] static std::expected<Wad, Error> open(Disc disc);

    /// The parsed WARRIORS.DIR.
    [[nodiscard]] const WadIndex& index() const { return m_index; }
    /// The disc the archive is on.
    [[nodiscard]] const Disc& disc() const { return m_disc; }

    /// Finds an entry by a file name (`level1.lev`, any case) or by its name hash written `0x` and 1 to 8 hex
    /// digits (`0x7e23a6f2`). Fails with ErrorCode::NotFound when no entry matches and ErrorCode::InvalidArgument for
    /// an empty name or a malformed hash.
    [[nodiscard]] std::expected<const WadEntry*, Error> lookup(std::string_view nameOrHash) const;

    /// Opens one entry's bytes as a stream. Fails as Disc::openFile() does.
    /// The original's stream file system prefixes the name and asks the IOP to read the entry's range of the WAD.
    /// An entry replaced by useOverrides() opens its file whole instead, at the file's own size.
    /// @orig 0x00148aa0 PS2StreamFileSys::Open (DS_PS2FileSys.cpp)
    [[nodiscard]] std::expected<FileStream, Error> openEntry(const WadEntry& entry) const;

    /// Replaces whole entries with the files of `folder`, an install's `overrides/files/`
    /// (docs/guides/asset-and-script-architecture.md#file-overrides): each file named as lookup() takes a name
    /// (`level99.lua`, any case, or `0x` and its name hash) is read by openEntry() in place of the entry's bytes in
    /// WARRIORS.WAD. The names are taken in sorted order, so the first of two names for one entry wins. A missing
    /// folder replaces nothing and keeps what an earlier call replaced. Fails with ErrorCode::NotFound when the folder
    /// exists but cannot be listed.
    /// @coney-only the original reads every entry from the disc; an install's overrides are Coney's modding path
    [[nodiscard]] std::expected<WadOverrideReport, Error> useOverrides(const std::filesystem::path& folder);

    /// Whether any entry is replaced: the session plays the game's simulation from files that are not the disc's (a
    /// modded session).
    [[nodiscard]] bool modded() const { return !m_overrides.empty(); }

  private:
    Wad(Disc disc, WadIndex index) : m_disc(std::move(disc)), m_index(std::move(index)) {}

    Disc m_disc;
    WadIndex m_index;
    std::unordered_map<std::uint32_t, std::filesystem::path> m_overrides; ///< name hash -> the file replacing it
};

} // namespace coney::io
