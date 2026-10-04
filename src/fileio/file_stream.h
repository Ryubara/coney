// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <expected>
#include <filesystem>
#include <fstream>
#include <span>
#include <utility>

#include "core/error.h"
#include "fileio/stream.h"

namespace coney::io {

/// A stream over a byte range of a file on the host: a whole file in a disc folder, or a file's extent inside an ISO
/// image. It reads on demand, so the 1.4 GB WARRIORS.WAD costs no memory.
///
/// Standard C++ file I/O only, which every supported platform provides; nothing here is OS-specific.
class FileStream final : public Stream {
  public:
    /// Opens `length` bytes of `path` starting at byte `offset`. Fails with ErrorCode::NotFound when the file cannot
    /// be opened and ErrorCode::Truncated when the file is shorter than `offset + length`.
    [[nodiscard]] static std::expected<FileStream, Error> open(const std::filesystem::path& path, std::uint64_t offset,
                                                               std::uint64_t length);

    FileStream(FileStream&&) noexcept = default;
    FileStream& operator=(FileStream&&) noexcept = default;
    ~FileStream() override = default;

    [[nodiscard]] std::expected<void, Error> read(std::span<std::byte> destination) override;
    [[nodiscard]] std::expected<void, Error> seek(std::uint64_t position) override;
    [[nodiscard]] std::uint64_t tell() const override { return m_position; }
    [[nodiscard]] std::uint64_t size() const override { return m_length; }

  private:
    FileStream(std::ifstream file, std::uint64_t offset, std::uint64_t length)
        : m_file(std::move(file)), m_offset(offset), m_length(length) {}

    std::ifstream m_file;
    std::uint64_t m_offset;
    std::uint64_t m_length;
    std::uint64_t m_position = 0;
};

} // namespace coney::io
