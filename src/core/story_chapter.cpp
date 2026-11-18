// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/story_chapter.h"

namespace coney::story {

std::optional<HubChapterPlan> planHubChapter(const StoryMerge& /*merge*/, int /*checkpoint*/,
                                             std::optional<std::string_view> /*lastMissionId*/) {
    return std::nullopt;
}

} // namespace coney::story
