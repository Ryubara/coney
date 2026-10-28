// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core/cpak_index.h"
#include "core/error.h"
#include "core/sha256.h"

// Synthetic Coney Packs built byte by byte from the layout on docs/guides/cpak-format.md, independently of
// cpak::Writer, so the reader is tested against the page and not only against the writer. No game data.
namespace coney::test::cpak {

/// The code of the error `result` holds, or nullopt when it holds a value: a check that never reads error() of a
/// success.
template <typename T> [[nodiscard]] std::optional<ErrorCode> failure(const std::expected<T, Error>& result) {
    if (result.has_value()) {
        return std::nullopt;
    }
    return result.error().code;
}

/// The bytes of `text`.
[[nodiscard]] inline std::vector<std::byte> bytesOf(std::string_view text) {
    std::vector<std::byte> bytes(text.size());
    std::ranges::transform(text, bytes.begin(), [](char c) { return static_cast<std::byte>(c); });
    return bytes;
}

/// The SHA-256 of `bytes`.
[[nodiscard]] inline Sha256Digest sha256Of(std::span<const std::byte> bytes) {
    Sha256 sha;
    sha.update(bytes);
    return sha.finish();
}

/// Writes `value` little-endian into `out` at `at`, `width` bytes of it.
inline void put(std::vector<std::byte>& out, std::size_t at, std::uint64_t value, std::size_t width) {
    for (std::size_t i = 0; i < width; ++i) {
        out[at + i] = static_cast<std::byte>((value >> (8 * i)) & 0xffU);
    }
}

/// One entry as a hand-built index records it, and the bytes placed at its offset.
struct RawEntry {
    std::string name;
    std::uint64_t offset = 0;
    std::uint64_t storedSize = 0;
    std::uint64_t size = 0;
    std::uint8_t compression = 0;
    Sha256Digest sha256{};
    std::vector<std::byte> stored; ///< Written at `offset`; may be shorter than storedSize to build a bad archive.
};

/// The values of a hand-built header; the defaults are a well-formed one.
struct RawHeader {
    std::uint16_t version = coney::cpak::kVersion;
    std::uint16_t flags = 0;
    std::uint32_t alignment = 16;
    std::uint64_t archiveSize = 0; ///< 0: the size of the bytes built.
};

/// A well-formed entry: `name` holding `text`, stored as it is at `offset`.
[[nodiscard]] inline RawEntry rawEntry(std::string name, std::uint64_t offset, std::string_view text) {
    RawEntry entry;
    entry.name = std::move(name);
    entry.offset = offset;
    entry.stored = bytesOf(text);
    entry.storedSize = entry.stored.size();
    entry.size = entry.stored.size();
    entry.sha256 = sha256Of(entry.stored);
    return entry;
}

/// An archive of `entries` in the order given (names are not sorted here), its index hashed as the page says: the
/// SHA-256 of the header's first 32 bytes, then the index. The archive ends after the last entry's stored bytes, or
/// at the end of the index.
[[nodiscard]] inline std::vector<std::byte> buildArchive(const std::vector<RawEntry>& entries, RawHeader header = {}) {
    constexpr std::size_t kHeader = coney::cpak::kHeaderSize;
    constexpr std::size_t kRecord = coney::cpak::kRecordSize;
    std::size_t namesSize = 0;
    for (const RawEntry& entry : entries) {
        namesSize += entry.name.size();
    }
    const std::size_t indexSize = entries.size() * kRecord + namesSize;
    std::size_t end = kHeader + indexSize;
    for (const RawEntry& entry : entries) {
        end = std::max<std::size_t>(end, static_cast<std::size_t>(entry.offset) + entry.stored.size());
    }
    std::vector<std::byte> out(end);
    // The header: magic, version, flags, alignment, count, index size, reserved, archive size; hash below.
    for (std::size_t i = 0; i < 4; ++i) {
        out[i] = coney::cpak::kMagic[i];
    }
    put(out, 0x04, header.version, 2);
    put(out, 0x06, header.flags, 2);
    put(out, 0x08, header.alignment, 4);
    put(out, 0x0c, entries.size(), 4);
    put(out, 0x10, indexSize, 4);
    put(out, 0x18, header.archiveSize != 0 ? header.archiveSize : end, 8);
    // The records, then the names in the same order.
    std::size_t nameAt = 0;
    for (std::size_t i = 0; i < entries.size(); ++i) {
        const RawEntry& entry = entries[i];
        const std::size_t at = kHeader + i * kRecord;
        put(out, at + 0x00, entry.offset, 8);
        put(out, at + 0x08, entry.storedSize, 8);
        put(out, at + 0x10, entry.size, 8);
        put(out, at + 0x18, nameAt, 4);
        put(out, at + 0x1c, entry.name.size(), 2);
        out[at + 0x1e] = static_cast<std::byte>(entry.compression);
        std::ranges::transform(entry.sha256, out.begin() + static_cast<std::ptrdiff_t>(at + 0x20),
                               [](std::uint8_t b) { return static_cast<std::byte>(b); });
        std::ranges::transform(entry.name,
                               out.begin() + static_cast<std::ptrdiff_t>(kHeader + entries.size() * kRecord + nameAt),
                               [](char c) { return static_cast<std::byte>(c); });
        nameAt += entry.name.size();
    }
    for (const RawEntry& entry : entries) {
        std::ranges::copy(entry.stored, out.begin() + static_cast<std::ptrdiff_t>(entry.offset));
    }
    // The index hash covers the header's first 32 bytes and the whole index.
    Sha256 sha;
    sha.update(std::span<const std::byte>(out).first(32));
    sha.update(std::span<const std::byte>(out).subspan(kHeader, indexSize));
    const Sha256Digest digest = sha.finish();
    std::ranges::transform(digest, out.begin() + 0x20, [](std::uint8_t b) { return static_cast<std::byte>(b); });
    return out;
}

} // namespace coney::test::cpak
