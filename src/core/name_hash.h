// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace coney {

/// Standard CRC-32 (reflected polynomial 0xEDB88320, initial value and final XOR 0xFFFFFFFF; the same result as
/// zlib's crc32) of a byte sequence.
///
/// Research: docs/research/name-hash.md
[[nodiscard]] std::uint32_t crc32(std::span<const std::byte> bytes);

/// CRC-32 of the characters of `text`, exactly as given (no case folding). The game hashes many kinds of names this
/// way; the chunk containers' resource hashes, for example, keep the tools' letter case
/// (docs/research/formats/wad-contents.md#hashes).
///
/// Research: docs/research/name-hash.md
/// @orig 0x00143f68 Crc32_Hash (unknown)
[[nodiscard]] std::uint32_t crc32(std::string_view text);

/// Returns `text` with the ASCII letters A-Z turned into a-z and every other byte unchanged. The original lowercases
/// in place through its ctype table; a copy suits callers that keep the name they were given.
///
/// Research: docs/research/name-hash.md
/// @orig 0x00143fd8 Crc32_Lowercase (unknown)
[[nodiscard]] std::string lowercaseAscii(std::string_view text);

/// The hash WARRIORS.DIR stores for a path: the CRC-32 of the lowercased path, so letter case never matters.
/// `nameHash("./ee_files/global.lua") == 0x7e23a6f2`.
///
/// Research: docs/research/name-hash.md, docs/research/formats/wad-dir.md#name-lookup
[[nodiscard]] std::uint32_t nameHash(std::string_view path);

} // namespace coney
