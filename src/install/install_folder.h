// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <span>
#include <string_view>

#include "core/error.h"
#include "fileio/stream.h"

namespace coney::install {

/// A file being written in the install folder.
class OutputFile {
  public:
    virtual ~OutputFile() = default;
    OutputFile() = default;
    OutputFile(const OutputFile&) = delete;
    OutputFile& operator=(const OutputFile&) = delete;
    OutputFile(OutputFile&&) = delete;
    OutputFile& operator=(OutputFile&&) = delete;

    /// Appends `bytes`. Fails with ErrorCode::Io when the system cannot write them (a full disk).
    [[nodiscard]] virtual std::expected<void, Error> write(std::span<const std::byte> bytes) = 0;
    /// Flushes and closes the file. Fails with ErrorCode::Io when the last bytes cannot be written.
    [[nodiscard]] virtual std::expected<void, Error> close() = 0;
};

/// The folder an install writes into, by paths relative to it with `/` between folders (`game/ps2/IOP/BFW.SND`).
///
/// The installer only ever goes through this, so the core holds no host file-system code: the platform supplies the
/// real folder (platform/install_folder_fs.h) and the tests one in memory.
class InstallFolder {
  public:
    virtual ~InstallFolder() = default;
    InstallFolder() = default;
    InstallFolder(const InstallFolder&) = delete;
    InstallFolder& operator=(const InstallFolder&) = delete;
    InstallFolder(InstallFolder&&) = delete;
    InstallFolder& operator=(InstallFolder&&) = delete;

    /// Makes the folder `relative` and every folder above it; an existing one is fine.
    [[nodiscard]] virtual std::expected<void, Error> makeFolders(std::string_view relative) = 0;
    /// The size of the file `relative`, or nullopt when there is none.
    [[nodiscard]] virtual std::optional<std::uint64_t> fileSize(std::string_view relative) const = 0;
    /// Creates (or empties) the file `relative`; its folder must exist.
    [[nodiscard]] virtual std::expected<std::unique_ptr<OutputFile>, Error> create(std::string_view relative) = 0;
    /// Opens the file `relative` for reading.
    [[nodiscard]] virtual std::expected<std::unique_ptr<io::Stream>, Error> open(std::string_view relative) const = 0;
    /// Renames `from` to `to`, replacing `to`: the step that makes a finished file appear whole or not at all.
    [[nodiscard]] virtual std::expected<void, Error> rename(std::string_view from, std::string_view to) = 0;
    /// Removes the file `relative`; a missing one is fine.
    [[nodiscard]] virtual std::expected<void, Error> remove(std::string_view relative) = 0;
    /// Bytes free on the drive the folder is on, or nullopt when the system cannot tell.
    [[nodiscard]] virtual std::optional<std::uint64_t> freeBytes() const = 0;
};

} // namespace coney::install
