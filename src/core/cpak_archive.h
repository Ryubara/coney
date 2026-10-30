// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <expected>
#include <span>
#include <string_view>
#include <utility>

#include "core/cpak_index.h"
#include "core/error.h"

namespace coney::cpak {

/// A whole archive held in memory (a file read or mapped by the caller), whose entries are read in place: a view of
/// an entry is a span into the archive's own bytes, never a copy. The format: docs/guides/cpak-format.md.
class Archive {
  public:
    /// Reads the index of `bytes`, the whole archive, which must outlive the Archive. Fails as Index::parse() does,
    /// with ErrorCode::Truncated also when `bytes` is shorter than the header's archive size and ErrorCode::Invalid
    /// when it is longer.
    [[nodiscard]] static std::expected<Archive, Error> open(std::span<const std::byte> bytes);

    /// The archive's index.
    [[nodiscard]] const Index& index() const { return m_index; }

    /// The stored bytes of `entry`, in place, without checking their hash: for a reader that verified the archive
    /// once (Verify) and reads it many times. `entry` must be one of index().entries(). Fails with
    /// ErrorCode::Invalid for a compressed entry, which cannot be read in place.
    [[nodiscard]] std::expected<std::span<const std::byte>, Error> view(const Entry& entry) const;

    /// view(), then the entry's size and SHA-256 checked (verifyEntry()). Fails as both do.
    [[nodiscard]] std::expected<std::span<const std::byte>, Error> verified(const Entry& entry) const;

    /// verified() of the entry called `name`. Fails with ErrorCode::NotFound when there is none.
    [[nodiscard]] std::expected<std::span<const std::byte>, Error> read(std::string_view name) const;

  private:
    Archive(Index index, std::span<const std::byte> bytes) : m_index(std::move(index)), m_bytes(bytes) {}

    Index m_index;
    std::span<const std::byte> m_bytes;
};

} // namespace coney::cpak
