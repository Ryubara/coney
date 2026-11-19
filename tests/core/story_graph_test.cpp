// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/sha256.h"
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

TEST_CASE("story graph: the step queries find a step, the step after a mission and each chapter's offer",
          "[story_graph]") {
    // Expected: story-progress.md#global-functions and story-progress.md#hub, confirmed (code); asked by step id, as
    // the design's progress has it (../guides/studio/mission-mods.md#graph-progress)
    const StoryGraph graph = StoryGraph::original();
    CHECK(graph.find("level99") == std::optional<std::size_t>{0});
    CHECK(graph.find("hub:12") == std::optional<std::size_t>{30});
    CHECK_FALSE(graph.find("level70").has_value());

    // runNextMission by step: none complete enters the first step; a mission enters the step after it.
    CHECK(graph.nextStep(std::nullopt) == StoryStep::mission(99));
    CHECK(graph.nextStep("level87") == StoryStep::hubChapter(1));
    CHECK(graph.nextStep("level86") == StoryStep::mission(93));
    CHECK(graph.nextStep("level64") == StoryStep::hubChapter(12));
    // A hub chapter is not completed, an unknown id has no row.
    CHECK_FALSE(graph.nextStep("hub:3").has_value());
    CHECK_FALSE(graph.nextStep("level70").has_value());

    // Each chapter's offer: chapter 6 offers level86; chapters 11 and 12 offer nothing (level64 is a side step).
    CHECK(graph.offerOf("hub:6") == StoryStep::mission(86));
    CHECK_FALSE(graph.offerOf("hub:11").has_value());
    CHECK_FALSE(graph.offerOf("hub:12").has_value());
    CHECK_FALSE(graph.offerOf("level80").has_value());

    // The hub entered at a checkpoint plays the chapter the story is at; a jump elsewhere plays the original chapter.
    CHECK(graph.hubChapterAt(6, "level81") == StoryStep::hubChapter(6));
    CHECK(graph.hubChapterAt(3, "level81") == StoryStep::hubChapter(3));
    CHECK(graph.hubChapterAt(1, std::nullopt) == StoryStep::hubChapter(1));
    CHECK_FALSE(graph.hubChapterAt(13, "level81").has_value());
    CHECK_FALSE(graph.hubChapterAt(0, std::nullopt).has_value());
}

TEST_CASE("story graph: a new hub chapter is the hub at chapter n's checkpoint and offers chapter n's mission",
          "[story_graph]") {
    // Expected: ../guides/studio/mission-mods.md#anchors, Coney's own design (no counterpart in the original); the
    // checkpoint is story-progress.md#hub, confirmed (code)
    const StoryGraph original = StoryGraph::original();
    std::vector<StoryStep> steps(original.steps().begin(), original.steps().end());
    const auto hub6 = std::ranges::find(steps, StoryStep::hubChapter(6));
    REQUIRE(hub6 != steps.end());
    // hub:6, the mod's mission, its new chapter, then level86 as before (the merge's placing, story_merge_test.cpp).
    steps.insert(hub6 + 1,
                 {StoryStep::modMission("yard-brawl.docks"), StoryStep::modHubChapter(6, "hub:6.yard-brawl.docks")});
    const StoryGraph graph(steps);

    // Chapter 6 offers the mod's mission; its completion enters the new chapter, at checkpoint 6.
    CHECK(graph.offerOf("hub:6") == StoryStep::modMission("yard-brawl.docks"));
    const std::optional<StoryStep> next = graph.nextStep("yard-brawl.docks");
    REQUIRE(next.has_value());
    if (next) {
        CHECK(next->id() == "hub:6.yard-brawl.docks");
        CHECK(next->level == kHubLevel);
        CHECK(next->checkpoint() == 6);
    }
    // The new chapter offers what chapter 6 offered, and the walk goes on from there.
    CHECK(graph.offerOf("hub:6.yard-brawl.docks") == StoryStep::mission(86));
    CHECK(graph.nextStep("level86") == StoryStep::mission(93));

    // The hub at checkpoint 6 plays the new chapter once the mod's mission is the latest complete, chapter 6 before.
    const std::optional<StoryStep> after = graph.hubChapterAt(6, "yard-brawl.docks");
    REQUIRE(after.has_value());
    if (after) {
        CHECK(after->id() == "hub:6.yard-brawl.docks");
    }
    CHECK(graph.hubChapterAt(6, "level81") == StoryStep::hubChapter(6));
    // level86 complete: the story is past both; a jump to checkpoint 6 plays chapter 6 itself.
    CHECK(graph.hubChapterAt(6, "level86") == StoryStep::hubChapter(6));

    // fRunMission keeps its 12 rows, one per original chapter; the new chapter is no row of its own.
    const std::vector<HubOffer> offers = graph.hubOffers();
    REQUIRE(offers.size() == 12);
    for (std::size_t index = 0; index < offers.size(); ++index) {
        CHECK(offers[index].chapter == static_cast<int>(index) + 1);
    }
    CHECK(graph.offer(7) == 31);
}
