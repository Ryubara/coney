// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Synthetic streamed-world layouts for the streaming tests: worlds made of sectors in a row along x, built as the
// readers' results rather than as bytes. Nothing here comes from the game (LEGAL.md, "No game data").

#include <cstdint>
#include <initializer_list>
#include <utility>
#include <vector>

#include "world/world_manifest.h"
#include "world/world_streams.h"

namespace coney::test {

/// A world layout of sectors in a row: the i-th entry is a box spanning x 10i to 10i + 10 (y and z 0 to 10), with the
/// given streamed index and part (an index of -1 is a sector without an atomic).
inline world::WorldStream rowOfSectors(std::initializer_list<std::pair<std::int32_t, std::uint32_t>> sectors,
                                       std::uint32_t partCount) {
    world::WorldStream layout;
    layout.partCount = partCount;
    float x = 0.0F;
    for (const auto& [index, part] : sectors) {
        world::WorldSector sector;
        sector.box = world::Box{{x, 0.0F, 0.0F}, {x + 10.0F, 10.0F, 10.0F}};
        world::SectorPluginData plugin;
        plugin.streamedIndex = index;
        plugin.part = part;
        plugin.origin = world::Vec3{x + 5.0F, 5.0F, 5.0F};
        sector.plugin = plugin;
        layout.sectors.push_back(sector);
        x += 10.0F;
    }
    return layout;
}

/// A manifest whose part i (1-based) has heap and file size `heaps[i - 1]`.
inline world::WorldManifest rowManifest(std::initializer_list<std::uint32_t> heaps) {
    world::WorldManifest manifest;
    for (const std::uint32_t heap : heaps) {
        manifest.parts.push_back(world::PartSizes{.fileSize = heap, .heapSize = heap});
    }
    return manifest;
}

} // namespace coney::test
