// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>

#include "core/error.h"

namespace coney::io {

/// A readable, seekable sequence of bytes of known size: a WAD entry, a file on the disc, a buffer in memory.
///
/// It plays the part of the original's file interface (`Read`, `Seek`, `Tell`, `GetSize`; docs/research/file-io.md,
/// "File interface"), with two differences: a read either delivers every byte asked for or fails, and nothing reads
/// past the end. The original checks neither.
///
/// Invariant: tell() <= size().
class Stream {
  public:
    virtual ~Stream() = default;
    Stream() = default;
    Stream(const Stream&) = delete;
    Stream& operator=(const Stream&) = delete;

    /// Reads exactly `destination.size()` bytes. Fails with ErrorCode::Truncated when fewer remain (and then reads
    /// nothing), or ErrorCode::Io when the operating system fails; the position is unspecified after an Io error.
    [[nodiscard]] virtual std::expected<void, Error> read(std::span<std::byte> destination) = 0;
    /// Moves to an absolute position; size() is a valid position. Fails with ErrorCode::Truncated past it.
    [[nodiscard]] virtual std::expected<void, Error> seek(std::uint64_t position) = 0;
    /// The position of the next read.
    [[nodiscard]] virtual std::uint64_t tell() const = 0;
    /// The total size in bytes.
    [[nodiscard]] virtual std::uint64_t size() const = 0;

    /// Bytes left after the current position.
    [[nodiscard]] std::uint64_t remaining() const { return size() - tell(); }

    /// Moves `count` bytes forward without reading them. Fails with ErrorCode::Truncated, without moving, when fewer
    /// remain. The original reads and discards 256 bytes at a time because its streams cannot seek forward; every
    /// Coney stream can, so this is a seek.
    ///
    /// Research: docs/research/chunk-system.md
    /// @orig 0x00154440 Stream_SkipBytes (unknown)
    [[nodiscard]] std::expected<void, Error> skip(std::uint64_t count);

    /// Reads a little-endian 32-bit value. Fails as read() does.
    [[nodiscard]] std::expected<std::uint32_t, Error> readU32Le();

  protected:
    Stream(Stream&&) = default;
    Stream& operator=(Stream&&) = default;
};

/// A stream over a byte buffer it does not own (the original's `FS_MemoryFile` wrapping an existing buffer,
/// docs/research/file-io.md#fs_memoryfile, read-only here).
class MemoryStream final : public Stream {
  public:
    /// Reads `data`, which must outlive the stream.
    explicit MemoryStream(std::span<const std::byte> data) : m_data(data) {}

    [[nodiscard]] std::expected<void, Error> read(std::span<std::byte> destination) override;
    [[nodiscard]] std::expected<void, Error> seek(std::uint64_t position) override;
    [[nodiscard]] std::uint64_t tell() const override { return m_position; }
    [[nodiscard]] std::uint64_t size() const override { return m_data.size(); }

  private:
    std::span<const std::byte> m_data;
    std::size_t m_position = 0;
};

/// A window of `length` bytes of another stream, starting at that stream's position when the window is made.
///
/// The chunk system hands one to a chunk's stream reader, so the reader cannot run past its own chunk into the next
/// one. The parent must outlive the window and must not be moved by anyone else while the window is in use.
class SubStream final : public Stream {
  public:
    /// Fails with ErrorCode::Truncated when the parent has fewer than `length` bytes left.
    [[nodiscard]] static std::expected<SubStream, Error> open(Stream& parent, std::uint64_t length);

    SubStream(SubStream&&) = default;
    SubStream& operator=(SubStream&&) = delete;
    ~SubStream() override = default;

    [[nodiscard]] std::expected<void, Error> read(std::span<std::byte> destination) override;
    [[nodiscard]] std::expected<void, Error> seek(std::uint64_t position) override;
    [[nodiscard]] std::uint64_t tell() const override { return m_position; }
    [[nodiscard]] std::uint64_t size() const override { return m_length; }

  private:
    SubStream(Stream& parent, std::uint64_t base, std::uint64_t length)
        : m_parent(&parent), m_base(base), m_length(length) {}

    Stream* m_parent;
    std::uint64_t m_base;
    std::uint64_t m_length;
    std::uint64_t m_position = 0;
};

} // namespace coney::io
