// SPDX-License-Identifier: GPL-3.0-or-later
#include "warriors/mod_story_progress.h"

#include <algorithm>
#include <format>
#include <iterator>
#include <ranges>
#include <set>

#include "core/log.h"

namespace coney {

namespace {

// The mod a mission id names: the part before its first '.' (`yard-brawl.alley` is `yard-brawl`'s).
std::string_view modOf(std::string_view id) { return id.substr(0, id.find('.')); }

// Whether `graph` holds a mod mission `id`.
bool chainHas(const story::StoryGraph& graph, std::string_view id) {
    return std::ranges::any_of(graph.steps(), [id](const story::StoryStep& step) {
        return step.kind == story::StoryStep::Kind::Mission && step.modId == id;
    });
}

} // namespace

const MissionProgress* ModStoryProgress::find(std::string_view id) const {
    const auto found = std::ranges::find(m_entries, id, &MissionProgress::id);
    return found != m_entries.end() ? &*found : nullptr;
}

bool ModStoryProgress::isComplete(std::string_view id) const {
    const MissionProgress* held = find(id);
    return held != nullptr && held->complete;
}

void ModStoryProgress::assign(std::uint8_t level, std::string id) { m_numbers[level] = std::move(id); }

const std::string* ModStoryProgress::missionAt(std::uint8_t level) const {
    const auto found = m_numbers.find(level);
    return found != m_numbers.end() ? &found->second : nullptr;
}

bool ModStoryProgress::unlock(std::uint8_t level, std::uint8_t group, std::uint8_t item) {
    const std::string* id = missionAt(level);
    if (id == nullptr) {
        return false;
    }
    // Only group 0 holds a mission's records: item 0 its completion, items 1-32 its ranks.
    if (group != 0 || item > kRanks) {
        return true;
    }
    MissionProgress& held = entry(*id);
    if (item == 0) {
        held.complete = true;
    } else {
        held.ranks |= 1U << (item - 1U);
    }
    return true;
}

std::optional<bool> ModStoryProgress::isLevelComplete(std::uint8_t level) const {
    const std::string* id = missionAt(level);
    if (id == nullptr) {
        return std::nullopt;
    }
    return isComplete(*id);
}

bool ModStoryProgress::setBest(std::uint8_t level, const MissionBest& best) {
    const std::string* id = missionAt(level);
    if (id == nullptr) {
        return false;
    }
    MissionProgress& held = entry(*id);
    held.bestScore = best.bestScore;
    held.total = best.total;
    held.grade = best.grade;
    held.percent = best.percent;
    return true;
}

MissionProgress& ModStoryProgress::entry(const std::string& id) {
    const auto found = std::ranges::find(m_entries, id, &MissionProgress::id);
    if (found != m_entries.end()) {
        return *found;
    }
    m_entries.push_back({.id = id});
    return m_entries.back();
}

const story::StoryStep* latestMission(const story::StoryGraph& graph,
                                      const std::function<bool(int level)>& isOriginalComplete,
                                      const ModStoryProgress& progress) {
    // Latest first, as findLastMission's list runs; entries of ids the chain lacks are never asked.
    for (const story::StoryStep& step : std::views::reverse(graph.steps())) {
        if (step.kind != story::StoryStep::Kind::Mission) {
            continue;
        }
        const bool complete = step.modId.empty() ? isOriginalComplete(step.level) : progress.isComplete(step.modId);
        if (complete) {
            return &step;
        }
    }
    return nullptr;
}

std::vector<std::string> warnMissingStoryMods(const ModStoryProgress& progress, const story::StoryGraph& graph) {
    std::set<std::string, std::less<>> missing;
    for (const MissionProgress& held : progress.entries()) {
        if (!chainHas(graph, held.id)) {
            missing.emplace(modOf(held.id));
        }
    }
    for (const std::string& mod : missing) {
        logging::warn(logging::Category::Engine,
                      std::format("save: story mod {} is not running; its missions are kept, and the story goes on "
                                  "from the latest mission present",
                                  mod));
    }
    return {missing.begin(), missing.end()};
}

} // namespace coney
