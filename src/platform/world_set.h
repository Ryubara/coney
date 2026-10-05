// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "core/error.h"
#include "fileio/wad.h"
#include "platform/texture_dictionary.h"
#include "platform/texture_lookup.h"
#include "platform/world_atomic.h"
#include "world/sector_budget.h"
#include "world/streamed_world.h"
#include "world/world_streamer.h"

namespace rw {
struct Atomic;
} // namespace rw

namespace coney::platform {

/// The streamed worlds a name stands for, in load order: `<name>s` and `<name>d` when `<name>s_sec.wld` exists,
/// otherwise `<name>` alone when `<name>_sec.wld` does (docs/research/level-loading.md#worldmanager-loadlevel). So
/// `level2` gives both of level 2's worlds, `level2s` its `s` world only, `objarena` its single world. Fails with
/// ErrorCode::NotFound when neither exists.
[[nodiscard]] std::expected<std::vector<std::string>, Error> worldNamesFor(const io::Wad& wad, std::string_view name);

/// The levels `levelN` (N below 200, past the highest on the disc) that have a streamed world on the disc, in number
/// order: the names worldNamesFor() resolves, so each can be played. For the debug menu's Levels page.
[[nodiscard]] std::vector<std::string> playableLevelNames(const io::Wad& wad);

/// A level's streamed worlds, loaded: each world's layout and texture dictionary, resident, and its parts' atomics and
/// dictionaries as the streamer asks for them. It is the PartStore the streamer reads through, and it charges the
/// worlds' and parts' heap sizes to a SectorBudget. Every dictionary it holds is in the global texture lookup while it
/// is loaded, so materials find their textures by name across all of them.
///
/// Needs a running RenderEngine (either backend) and must be destroyed before it stops.
///
/// Research: docs/research/world.md#loading-a-world, docs/research/world.md#part-file
class WorldSet final : public world::PartStore {
  public:
    /// Loads the worlds of `names` in order, as LoadLevel does: for each, the manifest, then room for its heap, then
    /// the world stream with its texture dictionary. No part is loaded. With `forDrawing` every dictionary is
    /// converted for the OpenGL renderer as it is read. Fails when a file is missing or damaged, or with
    /// ErrorCode::Invalid when a world's heap does not fit in `budget`, which must outlive the set.
    [[nodiscard]] static std::expected<std::unique_ptr<WorldSet>, Error>
    load(const io::Wad& wad, std::span<const std::string> names, world::SectorBudget& budget, bool forDrawing);

    /// Frees every loaded part, then the worlds and their dictionaries, and gives their heaps back to the budget.
    /// @orig 0x00410b70 World_Unload (WorldPS2.cpp)
    ~WorldSet() override;
    WorldSet(const WorldSet&) = delete;
    WorldSet& operator=(const WorldSet&) = delete;
    WorldSet(WorldSet&&) = delete;
    WorldSet& operator=(WorldSet&&) = delete;

    /// The worlds' streaming state, in load order (the `s` world first), for the streamer.
    [[nodiscard]] std::span<world::StreamedWorld* const> worlds() const { return m_states; }

    /// The atomic of sector `sector` of world `world`, or null while its part is not loaded.
    [[nodiscard]] rw::Atomic* atomic(std::size_t world, std::uint32_t sector) const;

    /// Atomics resident now, over every world.
    [[nodiscard]] std::size_t residentAtomics() const;

    /// Reads part `part` of world `world`: its texture dictionary (into the lookup), then every `{index, atomic}`
    /// record, each atomic read with librw, placed at its sector's origin, unpacked to plain geometry and given
    /// geometry flag 0x40 (modulate by material colour, which the fade-in uses).
    /// @orig 0x004110c0 World_ReadPart (WorldPS2.cpp)
    [[nodiscard]] std::expected<void, Error> loadPart(std::size_t world, std::uint32_t part) override;

    /// Frees part `part` of world `world`: its atomics first, then its dictionary.
    /// @orig 0x004115d0 World_UnloadPart (WorldPS2.cpp)
    void unloadPart(std::size_t world, std::uint32_t part) override;

  private:
    // One loaded part. Members are destroyed in reverse order: the atomics before the lookup entry and dictionary
    // their materials took textures from.
    struct Part {
        std::optional<TextureDictionary> dictionary;
        std::optional<TextureLookupEntry> lookup;
        std::vector<WorldAtomic> atomics;
    };
    // One world: its streaming state, its resident dictionary and its parts.
    struct Loaded {
        world::StreamedWorld state;
        TextureDictionary dictionary;
        TextureLookupEntry lookup;
        std::vector<Part> parts;           // part i at [i - 1]
        std::vector<rw::Atomic*> bySector; // by streamed index; null while not loaded
        std::uint64_t heap = 0;            // charged to the budget while the world is loaded
    };

    WorldSet(const io::Wad& wad, world::SectorBudget& budget) : m_wad(wad), m_budget(budget) {}

    /// Loads one world and adds it to the set.
    /// @orig 0x00410648 World_LoadStream (WorldPS2.cpp)
    /// @orig 0x00410a50 World_ReadStream (WorldPS2.cpp)
    [[nodiscard]] std::expected<void, Error> loadWorld(const std::string& name, bool forDrawing);

    const io::Wad& m_wad;
    world::SectorBudget& m_budget;
    bool m_forDrawing = false;
    std::vector<std::unique_ptr<Loaded>> m_worlds;
    std::vector<world::StreamedWorld*> m_states;
};

} // namespace coney::platform
