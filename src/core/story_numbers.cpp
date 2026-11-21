// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/story_numbers.h"

#include <algorithm>
#include <charconv>
#include <cstddef>
#include <format>
#include <optional>
#include <string_view>
#include <system_error>
#include <utility>

namespace coney::story {

namespace {

// The level number of an original mission's id (`level86` gives 86); nothing for any other id.
std::optional<int> originalLevel(std::string_view id) {
    constexpr std::string_view kPrefix = "level";
    if (!id.starts_with(kPrefix) || id.size() == kPrefix.size()) {
        return std::nullopt;
    }
    int level = 0;
    const std::string_view digits = id.substr(kPrefix.size());
    const auto [end, error] = std::from_chars(digits.data(), digits.data() + digits.size(), level);
    if (error != std::errc{} || end != digits.data() + digits.size()) {
        return std::nullopt;
    }
    return level;
}

// Whether `step` is the new hub chapter that mission `missionId` opened (`hub:<n>.<missionId>`, mergeStory()).
bool opensChapter(const StoryStep& step, std::string_view missionId) {
    return step.kind == StoryStep::Kind::HubChapter && step.modId.starts_with("hub:") &&
           step.modId.ends_with(std::format(".{}", missionId));
}

} // namespace

std::vector<int> levelNumberPool(std::span<const int> usedNumbers) {
    std::vector<int> pool;
    for (int number = kFirstPoolNumber; number <= kLastPoolNumber; ++number) {
        if (std::ranges::find(usedNumbers, number) == usedNumbers.end()) {
            pool.push_back(number);
        }
    }
    return pool;
}

std::vector<NumberedMission> numberModMissions(StoryMerge& merge, std::span<const int> pool) {
    std::vector<NumberedMission> numbered;
    std::vector<StoryStep> steps(merge.graph.steps().begin(), merge.graph.steps().end());
    std::vector<StoryStep> kept;
    kept.reserve(steps.size());
    for (std::size_t index = 0; index < steps.size(); ++index) {
        StoryStep step = steps[index];
        if (step.kind != StoryStep::Kind::Mission || step.modId.empty()) {
            kept.push_back(std::move(step));
            continue;
        }
        // In chain order, the next number of the pool.
        if (numbered.size() < pool.size()) {
            step.level = pool[numbered.size()];
            numbered.push_back({.id = step.modId, .level = step.level});
            kept.push_back(std::move(step));
            continue;
        }
        // Past the pool: refused. The new hub chapter it opened goes with it, and an original mission it replaced
        // takes its place again.
        if (index + 1 < steps.size() && opensChapter(steps[index + 1], step.modId)) {
            ++index;
        }
        const auto placed = std::ranges::find(merge.placed, step.modId, &PlacedMission::id);
        if (placed != merge.placed.end()) {
            if (const std::optional<int> level = originalLevel(placed->mission.replace)) {
                kept.push_back(StoryStep::mission(*level, step.sideStep));
            }
            merge.placed.erase(placed);
        }
        merge.refusals.push_back({.missions = {step.modId},
                                  .message = std::format("{}: no level number is left for it (the session has {})",
                                                         step.modId, pool.size())});
    }
    merge.graph = StoryGraph(std::move(kept));
    return numbered;
}

} // namespace coney::story
