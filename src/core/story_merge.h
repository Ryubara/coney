// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <span>
#include <string>
#include <vector>

#include "core/story_graph.h"
#include "core/story_manifest.h"

// Merging the story mods' missions into the story graph when the mods start: Coney's own, with no counterpart in the
// original, which has no mods.
//
// Research: docs/guides/studio/mission-mods.md#graph-merge

namespace coney::story {

/// An enabled story mod, as the merge takes it: its name and its manifest's `[story]` section.
struct StoryMod {
    std::string name;   ///< The mod's `[project] name`; its missions' ids are `<name>.<id>`.
    StorySection story; ///< Its missions, in manifest order.
};

/// A mission the merge placed in the chain.
struct PlacedMission {
    std::string id;       ///< Its story id, `<mod>.<id>`.
    std::string mod;      ///< The mod it comes from.
    StoryMission mission; ///< Its manifest entry: the level, scripts, title, area and label the later readers need.

    friend bool operator==(const PlacedMission&, const PlacedMission&) = default;
};

/// Missions the merge refused, with the message a player and the Mods screen are shown. One refusal per message: a
/// cycle's is every mission in it, any other one mission.
struct StoryRefusal {
    std::vector<std::string> missions; ///< The story ids refused.
    std::string message;               ///< What went wrong, naming the mission (`yard-brawl.alley: ...`) or the cycle.

    friend bool operator==(const StoryRefusal&, const StoryRefusal&) = default;
};

/// The merge's outcome.
///
/// **Ownership (networking checklist):** built once by the story game mode's session when its mods start and fixed
/// until they stop. The same seed and the same mods in the same load order give the same result on every machine, so a
/// co-op session compares `graph.hash()` with the mod list. Plain data, ids and no pointers.
struct StoryMerge {
    StoryGraph graph;                   ///< The merged chain.
    std::vector<PlacedMission> placed;  ///< The missions placed, in the order the merge placed them.
    std::vector<StoryRefusal> refusals; ///< The missions refused, in the order they were refused.
    bool modded = false;                ///< Any story mod was given: every story game is then a modded session.
};

/// Merges the missions of `storyMods` (the enabled mods with a `[story]` section, in load order) into `seed` (the
/// original's chain): each mod's missions in manifest order, mods in load order. A mission goes right after its `after`
/// anchor, after any missions already placed there and what follows them, so missions on one anchor chain in load
/// order; after a hub chapter `hub:<n>` it is followed by a new hub chapter `hub:<n>.<id>` at chapter n's checkpoint. A
/// `replace` takes the mission's place, side step included, and an anchor on the replaced mission means its
/// replacement. A mission anchored on one not yet placed waits for it, so a mod may anchor on a later mod's mission.
/// Refused, each with a message, and the rest placed: a missing anchor, an anchor on a refused mission, a `before`
/// guard that does not name the step after the anchor at the mission's turn, every mission of a cycle (one message),
/// the second replacement of a mission, a replacement of a hub chapter and an id already in the story. With no story
/// mod the graph is `seed`, its hash unchanged.
/// @coney-only the story graph for mods (docs/guides/studio/mission-mods.md#story-graph); the original has no mods
[[nodiscard]] StoryMerge mergeStory(const StoryGraph& seed, std::span<const StoryMod> storyMods);

} // namespace coney::story
