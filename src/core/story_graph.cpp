// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/story_graph.h"

namespace coney::story {

// Stub for the tests-first commit: every query answers as an empty chain would.

std::string StoryStep::id() const { return {}; }

StoryGraph StoryGraph::original() { return {}; }

std::vector<NextMissionRow> StoryGraph::runNextMissionRows() const { return {}; }

std::vector<int> StoryGraph::findLastMissionLevels() const { return {}; }

std::vector<HubOffer> StoryGraph::hubOffers() const { return {}; }

int StoryGraph::lastMission(const std::function<bool(int level)>& /*isComplete*/) const { return 0; }

std::optional<StoryLoad> StoryGraph::nextLoad(int /*lastLevel*/) const { return std::nullopt; }

std::optional<int> StoryGraph::offer(int /*chapter*/) const { return std::nullopt; }

Sha256Digest StoryGraph::hash() const { return {}; }

} // namespace coney::story
