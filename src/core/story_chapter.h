// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <optional>
#include <string>
#include <string_view>

#include "core/story_graph.h"
#include "core/story_merge.h"

// How the hub plays a hub chapter of the merged story: chapter n itself, or a new chapter that a mod's mission after
// `hub:<n>` opened. Coney's own, with no counterpart in the original, which has no mods.
//
// Research: docs/guides/studio/hub-chapters.md, docs/research/story-progress.md#hub

namespace coney::story {

/// What Coney sets when the hub loads for one hub chapter step.
///
/// **Ownership (networking checklist):** derived from the session's fixed StoryMerge and the story's progress at the
/// hub load, so every machine of a session that agrees on both gets the same plan. Plain data, ids and no pointers.
struct HubChapterPlan {
    std::string id;       ///< The chapter step's id: `hub:<n>`, or `hub:<n>.<mod>.<id>` for a new chapter.
    int checkpoint = 0;   ///< The hub's checkpoint, chapter n's: the hub's scripts read the story's time from it.
    bool original = true; ///< Chapter n itself, whose own script's actions run unchanged.
    std::string mod;      ///< The mod a new chapter comes from; empty for an original chapter.
    /// A new chapter's `chapter` script from the mod's manifest (the package's path); empty for Coney's default
    /// chapter, and for an original chapter.
    std::string script;
    /// The mission the chapter's last action loads (`fRunMission[checkpoint].level`); nothing when it offers none.
    std::optional<StoryStep> offer;

    friend bool operator==(const HubChapterPlan&, const HubChapterPlan&) = default;
};

/// The plan for the hub entered at `checkpoint` with `lastMissionId` the latest story mission complete (nothing: none):
/// the chapter StoryGraph::hubChapterAt() picks in `merge.graph`, its offer, and for a new chapter its mod and that
/// mission's `chapter` script from `merge.placed`. Nothing when the graph has no chapter at that checkpoint.
/// @coney-only the hub chapters that mod missions open (docs/guides/studio/hub-chapters.md)
[[nodiscard]] std::optional<HubChapterPlan> planHubChapter(const StoryMerge& merge, int checkpoint,
                                                           std::optional<std::string_view> lastMissionId);

} // namespace coney::story
