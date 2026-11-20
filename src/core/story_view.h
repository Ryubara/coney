// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "core/story_choices.h"
#include "core/story_merge.h"

// The rows of the Mods screen's story view: the merged chain with each mod's missions under its title, the marks for a
// shared anchor, a replacement and a refusal, and what the player can do on each row. The screen draws them. Coney's
// own; the original has no mods.
//
// Research: docs/guides/studio/mission-mods.md#graph-mods-screen

namespace coney::story {

/// One row of the story list.
struct StoryViewRow {
    /// What the row is.
    enum class Kind : std::uint8_t {
        ModTitle,      ///< The title of the mod whose missions follow; not selectable.
        Original,      ///< A step of the original's story, drawn in the game's colours.
        ModMission,    ///< A mod's mission, placed in the chain.
        RefusedHeader, ///< The heading above the refusals; not selectable.
        Refusal,       ///< A refused mission with the merge's message.
    };
    /// What accept does on the row.
    enum class Action : std::uint8_t {
        None,   ///< Nothing.
        Swap,   ///< Swaps the mission with the next one placed on its anchor (the last, with the one before it).
        Choose, ///< Makes this refused mission the one that takes the replaced mission's place.
    };

    Kind kind = Kind::Original;
    std::string id{};   ///< The step's or the refused mission's story id; empty for a heading.
    std::string text{}; ///< What the row says: a step's id, a mod's or a mission's title, a refusal's message.
    std::string mod{};  ///< The mod a mod row or a refusal belongs to; empty for an original step.
    Action action = Action::None;
    std::string anchor{};   ///< Shared anchor: the `after` the group's missions share; empty when not shared.
    std::string replaces{}; ///< The mission this one stands in for, or lost the place of; empty if none.
    std::vector<std::string> rivals{}; ///< For a contested replacement: the other missions that replace the same one.
    bool chosen = false;               ///< The group's order is the player's, not the load order's.

    /// Whether the cursor can stop on the row.
    [[nodiscard]] bool selectable() const { return kind != Kind::ModTitle && kind != Kind::RefusedHeader; }

    friend bool operator==(const StoryViewRow&, const StoryViewRow&) = default;
};

/// The rows of the story list for `merge` (the outcome of mergeStory() over `storyMods` with the player's choices): the
/// chain in order, each run of one mod's missions under a ModTitle row, then, when the merge refused anything, a
/// RefusedHeader and one Refusal row per refused mission (a cycle gives one row for each mission in it, all with its
/// message). A mission placed on a shared anchor has the Swap action, and a refused mission that lost a replacement to
/// another has the Choose action.
/// @coney-only the Mods screen's story view (docs/guides/studio/mission-mods.md#graph-mods-screen); no original
[[nodiscard]] std::vector<StoryViewRow> buildStoryView(const StoryMerge& merge, std::span<const StoryMod> storyMods);

} // namespace coney::story
