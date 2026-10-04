// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "core/error.h"
#include "fileio/file_stream.h"
#include "fileio/stream.h"

namespace coney::io {

/// Bytes per sector of an ISO 9660 image of the game's disc; the only block size supported.
inline constexpr std::uint64_t kIsoSectorSize = 2048;

/// A file in the root directory of an ISO 9660 image.
struct IsoFile {
    std::string name;     ///< Upper case, without the `;1` version suffix or a trailing dot.
    std::uint32_t extent; ///< First sector.
    std::uint32_t size;   ///< Size in bytes.
};

/// Reads the primary volume descriptor (sector 16) and the root directory of an ISO 9660 image. Subdirectories are
/// listed by neither name nor content: the game's files all sit in the root (docs/research/formats/wad-dir.md).
///
/// Fails with ErrorCode::Invalid when `image` is not an ISO 9660 image with 2048-byte sectors or a directory record
/// is damaged, and ErrorCode::Truncated when the root directory runs past the end of the image.
[[nodiscard]] std::expected<std::vector<IsoFile>, Error> readIsoRoot(Stream& image);

/// The player's disc: a folder (a mounted disc such as `H:\`, or a copy of its files) or an ISO 9660 image. Files are
/// found in its root by name, ignoring letter case, as `coney-tools` does (python/src/coney_tools/disc.py).
///
/// It replaces the original's `cdrom0:` device: the game opens `cdrom0:\WARRIORS.DIR;1` and `cdrom0:\WARRIORS.WAD;1`
/// at boot (docs/research/file-io.md#opening-the-wad-at-boot).
class Disc {
  public:
    /// Opens a folder or an ISO image. Fails with ErrorCode::NotFound when `path` is neither, and as readIsoRoot()
    /// does for a damaged image.
    [[nodiscard]] static std::expected<Disc, Error> open(const std::filesystem::path& path);

    /// Whether the root holds a file called `name` (any letter case, `;1` suffix optional).
    [[nodiscard]] bool has(std::string_view name) const;
    /// Size of the root file `name`. Fails with ErrorCode::NotFound when there is none.
    [[nodiscard]] std::expected<std::uint64_t, Error> fileSize(std::string_view name) const;
    /// Opens the root file `name` as a stream that reads on demand. Fails with ErrorCode::NotFound when there is
    /// none, and ErrorCode::Truncated when an image's directory places the file past the end of the image.
    [[nodiscard]] std::expected<FileStream, Error> openFile(std::string_view name) const;
    /// Opens `length` bytes of the root file `name` from byte `offset`, such as one entry of WARRIORS.WAD. Fails as
    /// openFile() does, and with ErrorCode::Truncated when the range runs past the end of the file.
    [[nodiscard]] std::expected<FileStream, Error> openFileRange(std::string_view name, std::uint64_t offset,
                                                                 std::uint64_t length) const;
    /// Reads the whole root file `name` into memory; meant for small files such as WARRIORS.DIR. Fails as
    /// openFile() and Stream::read() do.
    [[nodiscard]] std::expected<std::vector<std::byte>, Error> readFile(std::string_view name) const;

    /// The folder or image this disc was opened from.
    [[nodiscard]] const std::filesystem::path& path() const { return m_path; }
    /// Whether the disc is an ISO image rather than a folder.
    [[nodiscard]] bool isImage() const { return m_isImage; }

  private:
    /// Where a root file's bytes are on the host.
    struct Location {
        std::filesystem::path hostFile; ///< The file itself in a folder, or the whole image.
        std::uint64_t offset = 0;       ///< Where the file starts in hostFile: 0 in a folder.
        std::uint64_t size = 0;
    };

    /// The location of the root file `name`, matched by cleanDiscName(); nullptr when there is none.
    [[nodiscard]] const Location* find(std::string_view name) const;

    std::filesystem::path m_path;
    bool m_isImage = false;
    std::map<std::string, Location, std::less<>> m_files; ///< Keyed by cleanDiscName().
};

/// The key a root file is found by: upper case, without an ISO `;1` version suffix or a trailing dot.
[[nodiscard]] std::string cleanDiscName(std::string_view name);

} // namespace coney::io
