// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/story_numbers.h"

namespace coney::story {

// Stub for the test commit: no pool and no numbers yet.
std::vector<int> levelNumberPool(std::span<const int> /*usedNumbers*/) { return {}; }

// Stub for the test commit: nothing is numbered yet.
std::vector<NumberedMission> numberModMissions(StoryMerge& /*merge*/, std::span<const int> /*pool*/) { return {}; }

} // namespace coney::story
