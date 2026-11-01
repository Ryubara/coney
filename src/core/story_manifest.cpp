// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/story_manifest.h"

namespace coney::story {

// Stub for the tests-first commit: no id is valid yet.
bool validMissionId(std::string_view /*id*/) { return false; }

// Stub for the tests-first commit: no manifest has a story section yet.
std::expected<std::optional<StorySection>, Error> parseStorySection(std::string_view /*manifestText*/) {
    return std::optional<StorySection>{};
}

} // namespace coney::story
