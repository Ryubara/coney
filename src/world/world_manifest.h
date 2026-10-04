// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <vector>

#include "core/error.h"

// A streamed world's manifest (`<world>_sec.mem`): the sizes of the world stream and of each part file, and the heap
// the game makes for each. Format: docs/research/world.md#manifest.

namespace coney::world {

/// The sizes of one part file (`<world>_ms<i>.sec`).
struct PartSizes {
    std::uint32_t fileSize = 0; ///< Bytes of the part file.
    std::uint32_t heapSize = 0; ///< Bytes of the clump the game makes for the part in the `Sector Pool`.
};

/// What a manifest holds. Research: docs/research/world.md#manifest
struct WorldManifest {
    std::uint32_t worldSize = 0;     ///< Bytes of `<world>_sec.wld`.
    std::uint32_t worldHeapSize = 0; ///< Bytes of the clump the game makes for the world stream.
    std::vector<PartSizes> parts;    ///< Part i (1-based) is parts[i - 1].
};

/// Reads a manifest: `u32 worldSize, u32 worldHeapSize, u32 partCount`, then `partCount` pairs `{fileSize,
/// heapSize}`. Fails with ErrorCode::Truncated when the data is shorter than its count says and ErrorCode::Invalid when
/// bytes follow the last pair (every manifest on the disc is exactly 12 + 8 × partCount bytes).
/// @orig 0x00410e08 World_ReadManifest (WorldPS2.cpp)
[[nodiscard]] std::expected<WorldManifest, Error> readWorldManifest(std::span<const std::byte> data);

/// The world heap the loader asks for when a world has no manifest: 135 % of the stream's size, at least 716,800
/// bytes (docs/research/world.md#manifest; never needed on the retail disc, where every world has one).
[[nodiscard]] std::uint32_t worldHeapWithoutManifest(std::uint32_t fileSize);

/// A part's heap without a manifest: 130 % of the file's size, at least 256 KB (docs/research/world.md#manifest).
[[nodiscard]] std::uint32_t partHeapWithoutManifest(std::uint32_t fileSize);

} // namespace coney::world
