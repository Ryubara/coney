// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/error.h"

// A mod's `[story]` section of `coney-project.toml`: the missions it places in the story graph, each anchored after a
// step or in a step's place. Coney's own format; the original has no mods.
//
// Research: docs/guides/studio/mission-mods.md#story-manifest

namespace coney::story {

/// One `[[story.missions]]` table: a mission a mod puts into the story. Paths are the package's own, as written.
struct StoryMission {
    std::string id;           ///< The mod-local id; the story's id is `<mod>.<id>`.
    std::string title;        ///< What the menus show.
    std::string level;        ///< The mission's level, in the package.
    std::string script;       ///< Optional: the mission's script; empty when the level carries its own scripts.
    std::string after;        ///< The anchor it follows (a mission, a hub chapter or another mod's mission); or empty.
    std::string replace;      ///< The mission whose place it takes; or empty. Exactly one of after and replace is set.
    std::string before;       ///< Optional guard: refused when the step after the anchor is not this one.
    std::string area;         ///< Optional: the mission menu's area; the anchor's when empty.
    std::optional<int> label; ///< Optional: the number shown and sorted on; the anchor's when unset.
    std::string chapter;      ///< Optional, with `after = "hub:<n>"` only: the new hub chapter's own script.

    friend bool operator==(const StoryMission&, const StoryMission&) = default;
};

/// A manifest's `[story]` section: its missions in manifest order. A manifest that has one is a story mod, missions or
/// none.
struct StorySection {
    std::vector<StoryMission> missions;

    friend bool operator==(const StorySection&, const StorySection&) = default;
};

/// Whether `id` is a valid mod-local mission id: 1 to 64 lower case letters, digits and '-', not starting with '-' (so
/// `<mod>.<id>` never holds a second '.' and never reads as an original id or a hub chapter).
[[nodiscard]] bool validMissionId(std::string_view id);

/// Reads the `[story]` section of a manifest's whole text: `[story]` and each `[[story.missions]]` table, with quoted
/// strings and the whole number `label`; every other table and key is skipped, so the rest of the manifest is read by
/// its own reader. Nothing when the manifest has no story section. Fails with ErrorCode::Invalid on a broken line
/// (`line N: ...`) and on a mission without a valid id, with its id twice, without a level, with both or neither of
/// `after` and `replace`, or with a `chapter` but no hub chapter anchor (`story mission '<id>': ...`).
/// @coney-only Coney's mod manifest; the original has no mods
[[nodiscard]] std::expected<std::optional<StorySection>, Error> parseStorySection(std::string_view manifestText);

} // namespace coney::story
