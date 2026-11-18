// SPDX-License-Identifier: GPL-3.0-or-later
// The hub chapters that mod missions open (docs/guides/studio/mission-mods.md#hub-chapters): Coney's own, with no
// counterpart in the original. Every mod here is synthetic.
#include "core/story_chapter.h"
#include "core/story_graph.h"
#include "core/story_manifest.h"
#include "core/story_merge.h"

#include <cstddef>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

using coney::story::HubChapterPlan;
using coney::story::kHubLevel;
using coney::story::mergeStory;
using coney::story::planHubChapter;
using coney::story::StoryGraph;
using coney::story::StoryMerge;
using coney::story::StoryMission;
using coney::story::StoryMod;
using coney::story::StoryStep;

namespace {

// A mission `id` after `anchor`, with an optional chapter script.
StoryMission after(std::string id, std::string anchor, std::string chapter = {}) {
    StoryMission mission;
    mission.id = std::move(id);
    mission.level = "levels/a.clevel";
    mission.after = std::move(anchor);
    mission.chapter = std::move(chapter);
    return mission;
}

// The id of the latest mission of `graph` in `complete` (side steps included, hub chapters never asked): what the
// caller of planHubChapter gets from latestMission (src/warriors/mod_story_progress.h) over the session's progress,
// kept here because core does not link the warriors code.
std::optional<std::string> latestComplete(const StoryGraph& graph, const std::set<std::string>& complete) {
    const auto steps = graph.steps();
    for (auto step = steps.rbegin(); step != steps.rend(); ++step) {
        if (step->kind == StoryStep::Kind::Mission && complete.contains(step->id())) {
            return step->id();
        }
    }
    return std::nullopt;
}

// The merge of one mod `name` with `missions` over the original's chain.
StoryMerge merged(std::string name, std::vector<StoryMission> missions) {
    StoryMod mod{.name = std::move(name), .story = {}};
    mod.story.missions = std::move(missions);
    const std::vector<StoryMod> mods{std::move(mod)};
    return mergeStory(StoryGraph::original(), mods);
}

} // namespace

TEST_CASE("hub chapter: an original chapter plays its own actions and offers the chain's next mission",
          "[story_chapter]") {
    // Expected: story-progress.md#hub, confirmed (code)
    const StoryMerge merge = merged("quiet", {});
    const std::optional<HubChapterPlan> plan = planHubChapter(merge, 4, "level3");
    REQUIRE(plan.has_value());
    if (plan) {
        CHECK(plan->id == "hub:4");
        CHECK(plan->checkpoint == 4);
        CHECK(plan->original);
        CHECK(plan->mod.empty());
        CHECK(plan->script.empty());
        CHECK(plan->offer == StoryStep::mission(5));
    }
    const std::optional<HubChapterPlan> last = planHubChapter(merge, 11, "level84");
    REQUIRE(last.has_value());
    if (last) {
        CHECK_FALSE(last->offer.has_value());
    }
    CHECK_FALSE(planHubChapter(merge, 13, std::nullopt).has_value());
}

TEST_CASE("hub chapter: a new chapter without a script is Coney's default at chapter n's checkpoint",
          "[story_chapter]") {
    // Expected: ../guides/studio/mission-mods.md#graph-questions (the default chapter, decided 2026-11-01), Coney's
    // own design (no counterpart in the original)
    const StoryMerge merge = merged("yard-brawl", {after("docks", "hub:6")});
    REQUIRE(merge.refusals.empty());
    // Chapter 6 offers the mod's mission.
    const std::optional<HubChapterPlan> before = planHubChapter(merge, 6, "level81");
    REQUIRE(before.has_value());
    if (before) {
        CHECK(before->id == "hub:6");
        CHECK(before->original);
        REQUIRE(before->offer.has_value());
        if (before->offer) {
            CHECK(before->offer->id() == "yard-brawl.docks");
        }
    }
    // Once it is complete the hub at checkpoint 6 is the new chapter, offering chapter 6's mission.
    const std::optional<HubChapterPlan> plan = planHubChapter(merge, 6, "yard-brawl.docks");
    REQUIRE(plan.has_value());
    if (plan) {
        CHECK(plan->id == "hub:6.yard-brawl.docks");
        CHECK(plan->checkpoint == 6);
        CHECK_FALSE(plan->original);
        CHECK(plan->mod == "yard-brawl");
        CHECK(plan->script.empty());
        CHECK(plan->offer == StoryStep::mission(86));
    }
}

TEST_CASE("hub chapter: a new chapter with a chapter script runs the mod's script", "[story_chapter]") {
    // Expected: ../guides/studio/mission-mods.md#story-manifest, Coney's own design (no counterpart in the original)
    const StoryMerge merge = merged("yard-brawl", {after("docks", "hub:6", "hub/docks-return.lua")});
    const std::optional<HubChapterPlan> plan = planHubChapter(merge, 6, "yard-brawl.docks");
    REQUIRE(plan.has_value());
    if (plan) {
        CHECK_FALSE(plan->original);
        CHECK(plan->script == "hub/docks-return.lua");
        CHECK(plan->offer == StoryStep::mission(86));
    }
}

TEST_CASE("hub chapter: two missions on one chapter: the first new chapter offers the second", "[story_chapter]") {
    // Expected: ../guides/studio/mission-mods.md#graph-merge, Coney's own design (no counterpart in the original)
    const StoryMerge merge = merged("yard-brawl", {after("docks", "hub:6"), after("pier", "hub:6", "hub/pier.lua")});
    REQUIRE(merge.refusals.empty());
    const std::optional<HubChapterPlan> first = planHubChapter(merge, 6, "yard-brawl.docks");
    REQUIRE(first.has_value());
    if (first) {
        CHECK(first->id == "hub:6.yard-brawl.docks");
        CHECK(first->script.empty());
        REQUIRE(first->offer.has_value());
        if (first->offer) {
            CHECK(first->offer->id() == "yard-brawl.pier");
        }
    }
    const std::optional<HubChapterPlan> second = planHubChapter(merge, 6, "yard-brawl.pier");
    REQUIRE(second.has_value());
    if (second) {
        CHECK(second->id == "hub:6.yard-brawl.pier");
        CHECK(second->script == "hub/pier.lua");
        CHECK(second->offer == StoryStep::mission(86));
    }
}

TEST_CASE("hub chapter: the walk goes through a new chapter at chapter n's checkpoint and on to the story's end",
          "[story_chapter]") {
    // Expected: story-progress.md#order, confirmed (code), with the design's new chapter
    // (../guides/studio/mission-mods.md#anchors, Coney's own design)
    const StoryMerge merge = merged("yard-brawl", {after("docks", "hub:6")});
    std::set<std::string> complete;
    std::vector<std::string> loaded;
    std::vector<std::pair<int, int>> hubLoads; // (level, checkpoint) of each hub load
    // Each round enters what runNextMission would; a mission is played to the end, a hub chapter "plays" by loading
    // the offer of the chapter the hub picks at that checkpoint. The bound only stops a broken graph looping.
    for (std::size_t round = 0; round < 64; ++round) {
        const std::optional<std::string> last = latestComplete(merge.graph, complete);
        const std::optional<StoryStep> next = merge.graph.nextStep(last);
        REQUIRE(next.has_value());
        if (!next) {
            return;
        }
        if (next->kind == StoryStep::Kind::Mission) {
            loaded.push_back(next->id());
            complete.insert(next->id());
            continue;
        }
        hubLoads.emplace_back(next->level, next->checkpoint());
        const std::optional<HubChapterPlan> plan = planHubChapter(merge, next->checkpoint(), last);
        REQUIRE(plan.has_value());
        if (!plan) {
            return;
        }
        loaded.push_back(plan->id);
        if (!plan->offer) {
            break;
        }
        loaded.push_back(plan->offer->id());
        complete.insert(plan->offer->id());
    }
    const std::vector<std::string> expected{"level99",
                                            "level80",
                                            "level87",
                                            "hub:1",
                                            "level34",
                                            "hub:2",
                                            "level2",
                                            "hub:3",
                                            "level3",
                                            "hub:4",
                                            "level5",
                                            "hub:5",
                                            "level81",
                                            "hub:6",
                                            "yard-brawl.docks",
                                            "hub:6.yard-brawl.docks",
                                            "level86",
                                            "level93",
                                            "hub:7",
                                            "level31",
                                            "hub:8",
                                            "level14",
                                            "hub:9",
                                            "level9",
                                            "hub:10",
                                            "level51",
                                            "level52",
                                            "level54",
                                            "level55",
                                            "level84",
                                            "hub:11"};
    CHECK(loaded == expected);
    // The new chapter loads the hub at chapter 6's checkpoint, as chapter 6 itself does.
    REQUIRE(hubLoads.size() == 12);
    CHECK(hubLoads[5] == std::pair{kHubLevel, 6});
    CHECK(hubLoads[6] == std::pair{kHubLevel, 6});
}
