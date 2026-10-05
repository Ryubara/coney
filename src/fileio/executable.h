// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string_view>
#include <vector>

#include "core/error.h"
#include "fileio/disc.h"

namespace coney::io {

/// Reads `count` little-endian 32-bit words at virtual address `address` of `elf`, a 32-bit little-endian ELF
/// executable (the PS2's), through its loadable program headers: the file bytes of the segment that holds the whole
/// range. This is how Coney uses data the original keeps in its executable (the random table) without carrying it:
/// it reads them from the player's own disc at run time. Fails with ErrorCode::Invalid when `elf` is not such a file
/// or no segment's file bytes hold the range, and ErrorCode::Truncated when a header runs past the end.
[[nodiscard]] std::expected<std::vector<std::uint32_t>, Error>
readExecutableWords(std::span<const std::byte> elf, std::uint32_t address, std::size_t count);

/// readExecutableWords() on the root file `name` of `disc`. Fails as Disc::readFile() does too.
[[nodiscard]] std::expected<std::vector<std::uint32_t>, Error>
readExecutableWords(const Disc& disc, std::string_view name, std::uint32_t address, std::size_t count);

} // namespace coney::io
