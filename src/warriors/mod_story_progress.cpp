// SPDX-License-Identifier: GPL-3.0-or-later
#include "warriors/mod_story_progress.h"

namespace coney {

// Stubs: the routes are not built yet.

const MissionProgress* ModStoryProgress::find(std::string_view /*id*/) const { return nullptr; }

bool ModStoryProgress::isComplete(std::string_view /*id*/) const { return false; }

void ModStoryProgress::assign(std::uint8_t /*level*/, std::string /*id*/) {}

const std::string* ModStoryProgress::missionAt(std::uint8_t /*level*/) const { return nullptr; }

bool ModStoryProgress::unlock(std::uint8_t /*level*/, std::uint8_t /*group*/, std::uint8_t /*item*/) { return false; }

std::optional<bool> ModStoryProgress::isLevelComplete(std::uint8_t /*level*/) const { return std::nullopt; }

bool ModStoryProgress::setBest(std::uint8_t /*level*/, const MissionBest& /*best*/) { return false; }

MissionProgress& ModStoryProgress::entry(const std::string& id) {
    m_entries.push_back({.id = id});
    return m_entries.back();
}

const story::StoryStep* latestMission(const story::StoryGraph& /*graph*/,
                                      const std::function<bool(int level)>& /*isOriginalComplete*/,
                                      const ModStoryProgress& /*progress*/) {
    return nullptr;
}

std::vector<std::string> warnMissingStoryMods(const ModStoryProgress& /*progress*/,
                                              const story::StoryGraph& /*graph*/) {
    return {};
}

} // namespace coney
