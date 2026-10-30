// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "core/error.h"
#include "core/sha256.h"
#include "fileio/stream.h"

// The Coney Pack (.cpak): Coney's own archive of the game's data, one file per archive, with a header and an index at
// the front and every entry aligned so that it can be read in place. The layout, and why each field is there, is
// docs/guides/cpak-format.md; this header is its reader's half. The format has no counterpart in the original.
namespace coney::cpak {

/// The four bytes every archive starts with.
inline constexpr std::array<std::byte, 4> kMagic{std::byte{'C'}, std::byte{'P'}, std::byte{'A'}, std::byte{'K'}};
/// The only format version this reader knows; it refuses any other.
inline constexpr std::uint16_t kVersion = 1;
/// The header's size: the index starts right after it.
inline constexpr std::size_t kHeaderSize = 64;
/// The size of one entry's record in the index; the names follow the records.
inline constexpr std::size_t kRecordSize = 64;
/// The bounds of the alignment a header may give (both powers of two).
inline constexpr std::uint32_t kMinAlignment = 16;
inline constexpr std::uint32_t kMaxAlignment = 1U << 20;
/// The longest entry name, in bytes of UTF-8.
inline constexpr std::size_t kMaxNameBytes = 1024;

/// How an entry's bytes are stored. The values are the format's; a reader that meets one it cannot decode says so.
enum class Compression : std::uint8_t {
    None = 0, ///< The bytes as they are; the only kind readable in place.
    Lz4 = 1,  ///< LZ4 frame format; reserved: installs write None until compression is measured (#798 step 6).
    Zstd = 2, ///< Zstandard frame format; reserved, as Lz4.
};

/// The name the format and the tools give a compression ("none", "lz4", "zstd"), or "unknown".
[[nodiscard]] std::string_view compressionName(Compression compression);

/// One entry of an archive, as its index records it.
struct Entry {
    std::string name;             ///< Its path in the archive, `textures/hud/hud_main`; see checkName().
    std::uint64_t offset = 0;     ///< Where its stored bytes start, from the start of the archive.
    std::uint64_t storedSize = 0; ///< How many bytes it takes in the archive.
    std::uint64_t size = 0;       ///< How many bytes it holds once decompressed (storedSize for None).
    Compression compression = Compression::None;
    Sha256Digest sha256{}; ///< SHA-256 of the decompressed bytes: the original's bytes.
};

/// Checks an entry name against the format's rules: 1 to kMaxNameBytes bytes, segments separated by `/`, none empty,
/// `.` or `..`, and no `\`, `:` or control character (NUL included). Fails with ErrorCode::Invalid, naming the rule.
/// The rules keep a name usable as a relative path on every platform when a tool exports an entry.
[[nodiscard]] std::expected<void, Error> checkName(std::string_view name);

/// The header and index of an archive: every entry's record, sorted by name, checked against the format's rules.
class Index {
  public:
    /// Reads the header and the index from the first bytes of an archive of `archiveSize` bytes. `front` must hold at
    /// least the header and the index (more is ignored). Fails with ErrorCode::Truncated when it does not or when the
    /// archive is shorter than its header says, and with ErrorCode::Invalid for a wrong magic, an unknown version or
    /// flag, an index whose SHA-256 does not match (a damaged index), or records that break the rules: names out of
    /// order, repeated or ill-formed, an entry outside the archive, unaligned or overlapping another.
    [[nodiscard]] static std::expected<Index, Error> parse(std::span<const std::byte> front, std::uint64_t archiveSize);

    /// Reads the header and index from the start of `stream`, whose size is the archive's. Fails as parse() does, or
    /// with the stream's own error.
    [[nodiscard]] static std::expected<Index, Error> read(io::Stream& stream);

    /// The entries, sorted by name (byte order).
    [[nodiscard]] const std::vector<Entry>& entries() const { return m_entries; }
    /// The entry called `name` (exact bytes), or nullptr. The pointer lives as long as this index.
    [[nodiscard]] const Entry* find(std::string_view name) const;
    /// The alignment every entry's offset is a multiple of.
    [[nodiscard]] std::uint32_t alignment() const { return m_alignment; }
    /// The archive's size in bytes, as the header gives it.
    [[nodiscard]] std::uint64_t archiveSize() const { return m_archiveSize; }
    /// The bytes the header and the index take; parse() needs this many of the front.
    [[nodiscard]] std::uint64_t frontSize() const { return kHeaderSize + m_indexSize; }

  private:
    std::vector<Entry> m_entries;
    std::uint32_t m_alignment = kMinAlignment;
    std::uint64_t m_archiveSize = 0;
    std::uint32_t m_indexSize = 0;
};

/// Checks that `stored` (an entry's bytes as the archive holds them) has the size and SHA-256 its record gives. Fails
/// with ErrorCode::Invalid for a damaged entry, and for a compressed entry, which this reader cannot decode.
[[nodiscard]] std::expected<void, Error> verifyEntry(const Entry& entry, std::span<const std::byte> stored);

/// Reads one entry from `stream` (positioned anywhere; the archive's own stream) and verifies it. Fails as
/// verifyEntry() does, or with the stream's error.
[[nodiscard]] std::expected<std::vector<std::byte>, Error> readEntry(io::Stream& stream, const Entry& entry);

} // namespace coney::cpak
