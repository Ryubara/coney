// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/story_chapter.h"

#include <algorithm>
#include <format>
#include <string>

namespace coney::story {

std::optional<HubChapterPlan> planHubChapter(const StoryMerge& merge, int checkpoint,
                                             std::optional<std::string_view> lastMissionId) {
    const std::optional<StoryStep> chapter = merge.graph.hubChapterAt(checkpoint, lastMissionId);
    if (!chapter) {
        return std::nullopt;
    }
    HubChapterPlan plan{.id = chapter->id(),
                        .checkpoint = chapter->checkpoint(),
                        .original = chapter->modId.empty(),
                        .mod = {},
                        .script = {},
                        .offer = merge.graph.offerOf(chapter->id())};
    if (plan.original) {
        return plan;
    }
    // A new chapter's id is `hub:<n>.<mission>` (mergeStory()): the mission that opened it names its mod and script.
    const std::string prefix = std::format("hub:{}.", chapter->chapter);
    const std::string_view missionId = std::string_view(plan.id).substr(std::min(prefix.size(), plan.id.size()));
    const auto placed = std::ranges::find_if(
        merge.placed, [missionId](const PlacedMission& mission) { return mission.id == missionId; });
    if (placed != merge.placed.end()) {
        plan.mod = placed->mod;
        plan.script = placed->mission.chapter;
    }
    return plan;
}

} // namespace coney::story
