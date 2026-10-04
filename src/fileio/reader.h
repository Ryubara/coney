// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>

#include "core/error.h"

namespace coney::io {

/// Reads little-endian values from a byte buffer it does not own, with bounds checks.
///
/// Every value is assembled from bytes, never by casting the buffer to a wider type, so alignment, the host's byte
/// order and strict aliasing never matter (docs/guides/conventions.md#memory-and-data). A failed read leaves the
/// position where it was.
class Reader {
  public:
    /// Starts at offset 0 of `data`, which must outlive the reader.
    explicit Reader(std::span<const std::byte> data) : m_data(data) {}

    /// Reads one byte. Fails with ErrorCode::Truncated at the end of the data.
    [[nodiscard]] std::expected<std::uint8_t, Error> readU8();
    /// Reads a little-endian 16-bit value. Fails with ErrorCode::Truncated when fewer than 2 bytes remain.
    [[nodiscard]] std::expected<std::uint16_t, Error> readU16Le();
    /// Reads a little-endian 32-bit value. Fails with ErrorCode::Truncated when fewer than 4 bytes remain.
    [[nodiscard]] std::expected<std::uint32_t, Error> readU32Le();
    /// Reads a little-endian IEEE 754 single. Fails with ErrorCode::Truncated when fewer than 4 bytes remain.
    [[nodiscard]] std::expected<float, Error> readF32Le();
    /// Returns the next `count` bytes as a view into the buffer and moves past them. Fails with ErrorCode::Truncated
    /// when fewer remain.
    [[nodiscard]] std::expected<std::span<const std::byte>, Error> readBytes(std::size_t count);

    /// Moves to an absolute offset; the end of the data is a valid position. Fails with ErrorCode::Truncated past it.
    [[nodiscard]] std::expected<void, Error> seek(std::size_t offset);

    /// The offset of the next read.
    [[nodiscard]] std::size_t position() const { return m_position; }
    /// Bytes left after the current position.
    [[nodiscard]] std::size_t remaining() const { return m_data.size() - m_position; }

  private:
    std::span<const std::byte> m_data;
    std::size_t m_position = 0;
};

/// The little-endian 32-bit value at `bytes[0..3]`. `bytes` must hold at least 4 bytes (checked by CONEY_ASSERT):
/// callers use it on buffers whose size they have already checked.
[[nodiscard]] std::uint32_t loadU32Le(std::span<const std::byte> bytes);

} // namespace coney::io
