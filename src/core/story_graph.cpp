// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/story_graph.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <format>
#include <string_view>

namespace coney::story {

namespace {

// The original's order as level numbers and hub chapter numbers (docs/research/story-progress.md#order): Coney's own
// table of the story, read from `global.lua`'s `runNextMission` and the hub's `fRunMission`, not a copy of either.
// level64 is the side step after hub chapter 11.
const std::array<StoryStep, 31> kOriginalChain{
    StoryStep::mission(99),    StoryStep::mission(80),       StoryStep::mission(87),    StoryStep::hubChapter(1),
    StoryStep::mission(34),    StoryStep::hubChapter(2),     StoryStep::mission(2),     StoryStep::hubChapter(3),
    StoryStep::mission(3),     StoryStep::hubChapter(4),     StoryStep::mission(5),     StoryStep::hubChapter(5),
    StoryStep::mission(81),    StoryStep::hubChapter(6),     StoryStep::mission(86),    StoryStep::mission(93),
    StoryStep::hubChapter(7),  StoryStep::mission(31),       StoryStep::hubChapter(8),  StoryStep::mission(14),
    StoryStep::hubChapter(9),  StoryStep::mission(9),        StoryStep::hubChapter(10), StoryStep::mission(51),
    StoryStep::mission(52),    StoryStep::mission(54),       StoryStep::mission(55),    StoryStep::mission(84),
    StoryStep::hubChapter(11), StoryStep::mission(64, true), StoryStep::hubChapter(12)};

// The first line of the hashed text form; a change of the form changes it.
constexpr std::string_view kHashVersion = "coney.story-graph 1\n";

// What loads to enter `step`.
StoryLoad loadOf(const StoryStep& step) { return {.level = step.level, .checkpoint = step.checkpoint()}; }

} // namespace

std::string StoryStep::id() const {
    return kind == Kind::HubChapter ? std::format("hub:{}", chapter) : std::format("level{}", level);
}

StoryGraph StoryGraph::original() { return StoryGraph({kOriginalChain.begin(), kOriginalChain.end()}); }

std::vector<NextMissionRow> StoryGraph::runNextMissionRows() const {
    std::vector<NextMissionRow> rows;
    if (m_steps.empty()) {
        return rows;
    }
    // A new game: nothing complete loads the first step.
    rows.push_back({.from = 0, .next = loadOf(m_steps.front())});
    // Each mission loads the step after it, whether that is a mission or a hub chapter.
    for (std::size_t index = 0; index + 1 < m_steps.size(); ++index) {
        if (m_steps[index].kind == StoryStep::Kind::Mission) {
            rows.push_back({.from = m_steps[index].level, .next = loadOf(m_steps[index + 1])});
        }
    }
    return rows;
}

std::vector<int> StoryGraph::findLastMissionLevels() const {
    std::vector<int> levels;
    for (auto step = m_steps.rbegin(); step != m_steps.rend(); ++step) {
        if (step->kind == StoryStep::Kind::Mission) {
            levels.push_back(step->level);
        }
    }
    return levels;
}

std::vector<HubOffer> StoryGraph::hubOffers() const {
    std::vector<HubOffer> offers;
    for (const StoryStep& step : m_steps) {
        if (step.kind == StoryStep::Kind::HubChapter) {
            offers.push_back({.chapter = step.chapter, .level = offer(step.chapter)});
        }
    }
    std::ranges::sort(offers, {}, &HubOffer::chapter);
    return offers;
}

int StoryGraph::lastMission(const std::function<bool(int level)>& isComplete) const {
    for (const int level : findLastMissionLevels()) {
        if (isComplete(level)) {
            return level;
        }
    }
    return 0;
}

std::optional<StoryLoad> StoryGraph::nextLoad(int lastLevel) const {
    for (const NextMissionRow& row : runNextMissionRows()) {
        if (row.from == lastLevel) {
            return row.next;
        }
    }
    return std::nullopt;
}

std::optional<int> StoryGraph::offer(int chapter) const {
    for (std::size_t index = 0; index < m_steps.size(); ++index) {
        const StoryStep& step = m_steps[index];
        if (step.kind != StoryStep::Kind::HubChapter || step.chapter != chapter) {
            continue;
        }
        // The chapter's last action loads the mission after it; a side step is reached by its own route.
        if (index + 1 < m_steps.size() && m_steps[index + 1].kind == StoryStep::Kind::Mission &&
            !m_steps[index + 1].sideStep) {
            return m_steps[index + 1].level;
        }
        return std::nullopt;
    }
    return std::nullopt;
}

Sha256Digest StoryGraph::hash() const {
    Sha256 sha;
    sha.update(kHashVersion);
    for (const StoryStep& step : m_steps) {
        sha.update(std::format("{} {} {} {}\n", step.id(), step.level, step.checkpoint(), step.sideStep ? 1 : 0));
    }
    return sha.finish();
}

} // namespace coney::story
