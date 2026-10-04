// SPDX-License-Identifier: GPL-3.0-or-later
#include "world/world_manifest.h"

#include <algorithm>
#include <format>
#include <utility>

#include "fileio/reader.h"

namespace coney::world {

std::expected<WorldManifest, Error> readWorldManifest(std::span<const std::byte> data) {
    io::Reader reader(data);
    WorldManifest manifest;
    auto worldSize = reader.readU32Le();
    auto worldHeap = reader.readU32Le();
    auto partCount = reader.readU32Le();
    if (!worldSize || !worldHeap || !partCount) {
        return fail(ErrorCode::Truncated, "the manifest is shorter than its 12-byte header");
    }
    // Check the size before allocating, so a damaged count cannot reserve gigabytes.
    constexpr std::size_t kPairBytes = 8;
    if (reader.remaining() / kPairBytes < *partCount) {
        return fail(ErrorCode::Truncated, std::format("the manifest names {} parts but holds only {} bytes of sizes",
                                                      *partCount, reader.remaining()));
    }
    if (reader.remaining() != std::size_t{*partCount} * kPairBytes) {
        return fail(ErrorCode::Invalid,
                    std::format("{} bytes follow the manifest's {} part sizes",
                                reader.remaining() - std::size_t{*partCount} * kPairBytes, *partCount));
    }
    manifest.worldSize = *worldSize;
    manifest.worldHeapSize = *worldHeap;
    manifest.parts.reserve(*partCount);
    for (std::uint32_t i = 0; i < *partCount; ++i) {
        // The size check above covers these reads.
        const std::uint32_t fileSize = reader.readU32Le().value();
        const std::uint32_t heapSize = reader.readU32Le().value();
        manifest.parts.push_back(PartSizes{.fileSize = fileSize, .heapSize = heapSize});
    }
    return manifest;
}

std::uint32_t worldHeapWithoutManifest(std::uint32_t fileSize) {
    constexpr std::uint64_t kMinimum = 0xaf000;
    return static_cast<std::uint32_t>(std::max<std::uint64_t>(std::uint64_t{fileSize} * 135 / 100, kMinimum));
}

std::uint32_t partHeapWithoutManifest(std::uint32_t fileSize) {
    constexpr std::uint64_t kMinimum = std::uint64_t{256} * 1024;
    return static_cast<std::uint32_t>(std::max<std::uint64_t>(std::uint64_t{fileSize} * 130 / 100, kMinimum));
}

} // namespace coney::world
