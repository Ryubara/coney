// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "core/error.h"

namespace coney::scenes {

/// The chunk type of the Scene List inside `scene_list.cnk`.
inline constexpr std::uint32_t kSceneListChunk = 0x43;
/// The file the boot loads the scene list from.
inline constexpr std::string_view kSceneListFile = "scene_list.cnk";

/// One record of the scene list: the scene id (its index), the record's size in bytes and its name (cut to 16
/// characters).
struct SceneListEntry {
    std::uint32_t id = 0;
    std::uint32_t size = 0;
    std::string name;
};

/// The global scene list (`scene_list.cnk`'s Scene List chunk): every `.scn` record, headers and segments, by id. The
/// scene ids the scripts use are indexes into it, global rather than per level.
///
/// Research: docs/research/scripting.md#scenes-and-movies, docs/research/scenes.md#slots
class SceneList {
  public:
    SceneList() = default;
    /// A list of `entries` (tests).
    explicit SceneList(std::vector<SceneListEntry> entries) : m_entries(std::move(entries)) {}

    /// Decodes the Scene List chunk's data: a u32 count, then 24-byte `{u32 id, u32 size, char name[16]}` records.
    /// Fails with ErrorCode::Truncated when the records do not fit.
    /// @orig 0x00353460 SceneList_Load (SceneCache.cpp)
    [[nodiscard]] static std::expected<SceneList, Error> parse(std::span<const std::byte> chunk);

    /// The id of the first record whose name contains `name` (case-sensitive); nothing when none does. `ScenePreload`
    /// takes 0 then.
    /// @orig 0x00353698 SceneList_FindContaining (SceneCache.cpp)
    [[nodiscard]] std::optional<std::uint32_t> findContaining(std::string_view name) const;
    /// The id of the record named exactly `name`; nothing when none is.
    /// @orig 0x00353778 SceneList_FindExact (SceneCache.cpp)
    [[nodiscard]] std::optional<std::uint32_t> findExact(std::string_view name) const;
    /// The record with id `id`; null when there is none.
    /// @orig 0x00353730 SceneList_FindId (SceneCache.cpp)
    [[nodiscard]] const SceneListEntry* entry(std::uint32_t id) const;

    [[nodiscard]] std::size_t size() const { return m_entries.size(); }
    [[nodiscard]] const std::vector<SceneListEntry>& entries() const { return m_entries; }

  private:
    std::vector<SceneListEntry> m_entries;
};

} // namespace coney::scenes
