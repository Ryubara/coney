// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <expected>
#include <string_view>

#include "core/error.h"
#include "fileio/disc.h"
#include "fileio/file_stream.h"
#include "fileio/wad_index.h"

namespace coney::io {

/// The disc file holding the archive's index.
inline constexpr std::string_view kWadDirFile = "WARRIORS.DIR";
/// The disc file holding every game file.
inline constexpr std::string_view kWadFile = "WARRIORS.WAD";

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
    /// @orig 0x00148aa0 PS2StreamFileSys::Open (DS_PS2FileSys.cpp)
    [[nodiscard]] std::expected<FileStream, Error> openEntry(const WadEntry& entry) const;

  private:
    Wad(Disc disc, WadIndex index) : m_disc(std::move(disc)), m_index(std::move(index)) {}

    Disc m_disc;
    WadIndex m_index;
};

} // namespace coney::io
