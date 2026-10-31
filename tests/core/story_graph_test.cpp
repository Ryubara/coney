// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/story_graph.h"

#include <algorithm>
#include <cstddef>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

using coney::story::HubOffer;
using coney::story::kHubLevel;
using coney::story::NextMissionRow;
using coney::story::StoryGraph;
using coney::story::StoryLoad;
using coney::story::StoryStep;

namespace {

// The original's chain as the design writes it, by step id (docs/guides/studio/mission-mods.md#graph).
const std::vector<std::string> kChain{"level99", "level80", "level87", "hub:1",   "level34", "hub:2",   "level2",
                                      "hub:3",   "level3",  "hub:4",   "level5",  "hub:5",   "level81", "hub:6",
                                      "level86", "level93", "hub:7",   "level31", "hub:8",   "level14", "hub:9",
                                      "level9",  "hub:10",  "level51", "level52", "level54", "level55", "level84",
                                      "hub:11",  "level64", "hub:12"};

// The step id of what a load starts: the hub at a checkpoint is that chapter, any other level its mission.
std::string idOf(const StoryLoad& load) {
    return load.level == kHubLevel ? "hub:" + std::to_string(load.checkpoint) : "level" + std::to_string(load.level);
}

// A profile of completed levels, as UM_IsLevelComplete answers for it.
struct Profile {
    std::set<int> complete;
    [[nodiscard]] bool isComplete(int level) const { return complete.contains(level); }
};

// What the natives will load next for `profile`: findLastMission, then its runNextMission row.
std::optional<StoryLoad> nextFor(const StoryGraph& graph, const Profile& profile) {
    return graph.nextLoad(graph.lastMission([&profile](int level) { return profile.isComplete(level); }));
}

} // namespace

TEST_CASE("story graph: the original's chain is the 31 steps of the story's order", "[story_graph]") {
    // Expected: story-progress.md#order, confirmed (code)
    const StoryGraph graph = StoryGraph::original();
    std::vector<std::string> ids;
    for (const StoryStep& step : graph.steps()) {
        ids.push_back(step.id());
    }
    CHECK(ids == kChain);
    // level64 is the one side step: reached through the Armies stages, never from hub:11.
    const auto sideSteps =
        std::count_if(graph.steps().begin(), graph.steps().end(), [](const StoryStep& step) { return step.sideStep; });
    CHECK(sideSteps == 1);
    REQUIRE(graph.steps().size() == 31);
    CHECK(graph.steps()[29] == StoryStep::mission(64, true));
}

TEST_CASE("story graph: its runNextMission view is the original's 20 rows", "[story_graph]") {
    // Expected: story-progress.md#global-functions, confirmed (code)
    const std::vector<NextMissionRow> expected{
        {0, {99, 1}},  {99, {80, 1}}, {80, {87, 1}}, {87, {95, 1}}, {34, {95, 2}},  {2, {95, 3}},  {3, {95, 4}},
        {5, {95, 5}},  {81, {95, 6}}, {86, {93, 1}}, {93, {95, 7}}, {31, {95, 8}},  {14, {95, 9}}, {9, {95, 10}},
        {51, {52, 1}}, {52, {54, 1}}, {54, {55, 1}}, {55, {84, 1}}, {84, {95, 11}}, {64, {95, 12}}};
    const StoryGraph graph = StoryGraph::original();
    CHECK(graph.runNextMissionRows() == expected);
    for (const NextMissionRow& row : expected) {
        CHECK(graph.nextLoad(row.from) == row.next);
    }
    // A level with no row loads nothing, as the original's missing row does.
    CHECK_FALSE(graph.nextLoad(95).has_value());
    CHECK_FALSE(graph.nextLoad(60).has_value());
}

TEST_CASE("story graph: its findLastMission view is the original's 19 levels, latest first", "[story_graph]") {
    // Expected: story-progress.md#global-functions, confirmed (code)
    const std::vector<int> expected{64, 84, 55, 54, 52, 51, 9, 14, 31, 93, 86, 81, 5, 3, 2, 34, 87, 80, 99};
    const StoryGraph graph = StoryGraph::original();
    CHECK(graph.findLastMissionLevels() == expected);
    // Nothing complete gives 0; the latest complete wins whatever else is.
    CHECK(graph.lastMission([](int) { return false; }) == 0);
    CHECK(graph.lastMission([](int level) { return level == 99 || level == 5 || level == 34; }) == 5);
    CHECK(graph.lastMission([](int level) { return level == 64 || level == 99; }) == 64);
}

TEST_CASE("story graph: its hub view is the original's 12 offers", "[story_graph]") {
    // Expected: story-progress.md#hub, confirmed (code)
    const std::vector<HubOffer> expected{{1, 34},
                                         {2, 2},
                                         {3, 3},
                                         {4, 5},
                                         {5, 81},
                                         {6, 86},
                                         {7, 31},
                                         {8, 14},
                                         {9, 9},
                                         {10, 51},
                                         {11, std::nullopt},
                                         {12, std::nullopt}};
    const StoryGraph graph = StoryGraph::original();
    CHECK(graph.hubOffers() == expected);
    for (const HubOffer& row : expected) {
        CHECK(graph.offer(row.chapter) == row.level);
    }
    CHECK_FALSE(graph.offer(0).has_value());
    CHECK_FALSE(graph.offer(13).has_value());
}

TEST_CASE("story graph: a walk from an empty profile loads the 29 steps up to hub:11, then level64 leads to hub:12",
          "[story_graph]") {
    // Expected: story-progress.md#global-functions, confirmed (code); the walk is the design's
    // (docs/guides/studio/mission-mods.md#graph-test)
    const StoryGraph graph = StoryGraph::original();
    Profile profile;
    std::vector<std::string> loaded;
    // Each round loads what the natives choose; a mission loaded is played to the end, a hub chapter "plays" by
    // loading its offer. The walk ends at a chapter that offers nothing. The bound only stops a broken graph looping.
    for (std::size_t round = 0; round < 64; ++round) {
        const std::optional<StoryLoad> load = nextFor(graph, profile);
        REQUIRE(load.has_value());
        if (!load) {
            return;
        }
        loaded.push_back(idOf(*load));
        int played = load->level;
        if (load->level == kHubLevel) {
            const std::optional<int> offered = graph.offer(load->checkpoint);
            if (!offered) {
                break;
            }
            loaded.push_back(idOf({*offered, 1}));
            played = *offered;
        }
        profile.complete.insert(played);
    }
    CHECK(loaded == std::vector<std::string>(kChain.begin(), kChain.begin() + 29));

    // level64 complete (the last Armies stage) makes hub chapter 12 the story's place from then on, whatever else is
    // completed afterwards (a replay, a flashback, a Rumble arena's return).
    profile.complete.insert(64);
    CHECK(nextFor(graph, profile) == StoryLoad{kHubLevel, 12});
    for (const int replayed : {99, 34, 84, 82}) {
        profile.complete.insert(replayed);
        CHECK(nextFor(graph, profile) == StoryLoad{kHubLevel, 12});
    }
}

TEST_CASE("story graph: the hash is fixed for the original's chain and changes with any step", "[story_graph]") {
    // Expected: ../guides/studio/mission-mods.md#graph, Coney's own form (the design asks only that every machine of a
    // session get the same hash)
    const StoryGraph graph = StoryGraph::original();
    CHECK(coney::toHex(graph.hash()) == "edd929267edd3e95646e69e0aa4ddba286df22ea862ad76f1b9c6e75b0267bf4");
    CHECK(StoryGraph::original().hash() == graph.hash());

    std::vector<StoryStep> steps(graph.steps().begin(), graph.steps().end());
    REQUIRE(steps.size() == 31);
    // level64 no longer a side step.
    std::vector<StoryStep> notSide = steps;
    notSide[29].sideStep = false;
    CHECK(StoryGraph(notSide).hash() != graph.hash());
    // Two missions swapped.
    std::vector<StoryStep> swapped = steps;
    std::swap(swapped[0], swapped[1]);
    CHECK(StoryGraph(swapped).hash() != graph.hash());
    // One step fewer.
    steps.pop_back();
    CHECK(StoryGraph(steps).hash() != graph.hash());
}
