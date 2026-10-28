// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <span>
#include <string>
#include <vector>

#include "core/cpak_index.h"
#include "core/error.h"
#include "core/sha256.h"

namespace coney::cpak {

/// Where the writer puts the archive's bytes, in order from the first: a file, a buffer. Returns its own error.
using Sink = std::function<std::expected<void, Error>(std::span<const std::byte>)>;

/// Hands one entry's bytes to the sink it is given, in as many pieces as it likes: how a source too large to hold in
/// memory (an entry of the 1.4 GB WARRIORS.WAD) reaches the writer.
using Fetch = std::function<std::expected<void, Error>(const Sink&)>;

/// Writes a Coney Pack: entries are added, then the whole archive is written in one pass, header and index first,
/// entries sorted by name, each at a multiple of the alignment, with zero bytes between. Every entry is written with
/// Compression::None. The format: docs/guides/cpak-format.md.
class Writer {
  public:
    /// A writer whose entries start at multiples of `alignment`, a power of two from kMinAlignment to kMaxAlignment
    /// (checked by add() and write()).
    explicit Writer(std::uint32_t alignment = kDefaultAlignment) : m_alignment(alignment) {}

    /// The alignment a writer uses unless told otherwise: a memory page on Windows and x86-64 Linux, so that one entry
    /// can be mapped on its own (docs/guides/cpak-format.md#alignment).
    static constexpr std::uint32_t kDefaultAlignment = 4096;

    /// Adds an entry whose bytes are in memory; they are hashed now and must stay valid until write() is done. Fails
    /// with ErrorCode::Invalid for a name that breaks checkName() or that another entry has, ignoring ASCII case.
    [[nodiscard]] std::expected<void, Error> add(std::string name, std::span<const std::byte> bytes);

    /// Adds an entry of `size` bytes and SHA-256 `sha256`, which `fetch` hands over during write(). write() fails
    /// with ErrorCode::Invalid when the bytes fetched differ in size or hash. Fails as the other add() does.
    [[nodiscard]] std::expected<void, Error> add(std::string name, std::uint64_t size, const Sha256Digest& sha256,
                                                 Fetch fetch);

    /// Writes the archive to `sink`. Fails with ErrorCode::InvalidArgument for a bad alignment, with
    /// ErrorCode::Invalid when a fetched entry is not what was added, and with the sink's or a fetch's own error.
    [[nodiscard]] std::expected<void, Error> write(const Sink& sink) const;

    /// write() into a buffer: the whole archive.
    [[nodiscard]] std::expected<std::vector<std::byte>, Error> toBytes() const;

  private:
    // One entry waiting for write(): its record's values and how to get its bytes.
    struct Pending {
        std::string name;
        std::uint64_t size = 0;
        Sha256Digest sha256{};
        Fetch fetch;
    };

    std::uint32_t m_alignment;
    std::vector<Pending> m_pending;
};

} // namespace coney::cpak
