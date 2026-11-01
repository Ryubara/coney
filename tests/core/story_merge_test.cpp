// SPDX-License-Identifier: GPL-3.0-or-later
// The story graph merge (docs/guides/studio/mission-mods.md#graph-merge): Coney's own, with no counterpart in the
// original. Every mod here is synthetic.
#include "core/story_graph.h"
#include "core/story_manifest.h"
#include "core/story_merge.h"

#include <algorithm>
#include <iterator>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

using coney::story::mergeStory;
using coney::story::StoryGraph;
using coney::story::StoryMerge;
using coney::story::StoryMission;
using coney::story::StoryMod;
using coney::story::StoryRefusal;
using coney::story::StoryStep;

namespace {

// A mission of `id` with only its level set.
StoryMission missionOf(std::string id) {
    StoryMission mission;
    mission.id = std::move(id);
    mission.level = "levels/a.clevel";
    return mission;
}

// A mission placed after `anchor`, with an optional before guard.
StoryMission after(std::string id, std::string anchor, std::string before = {}) {
    StoryMission mission = missionOf(std::move(id));
    mission.after = std::move(anchor);
    mission.before = std::move(before);
    return mission;
}

// A mission that replaces `target`.
StoryMission replacing(std::string id, std::string target) {
    StoryMission mission = missionOf(std::move(id));
    mission.replace = std::move(target);
    return mission;
}

// A story mod named `name` with `missions`, in manifest order.
StoryMod mod(std::string name, std::vector<StoryMission> missions) {
    StoryMod made{.name = std::move(name), .story = {}};
    made.story.missions = std::move(missions);
    return made;
}

// The merged chain's step ids from `from` to before `to` (both ids in the chain).
std::vector<std::string> idsBetween(const StoryGraph& graph, std::string_view from, std::string_view to) {
    std::vector<std::string> ids;
    bool inside = false;
    for (const StoryStep& step : graph.steps()) {
        const std::string id = step.id();
        if (id == from) {
            inside = true;
        }
        if (id == to) {
            break;
        }
        if (inside) {
            ids.push_back(id);
        }
    }
    return ids;
}

// The original's steps by id, to compare a chain with.
std::vector<std::string> idsOf(const StoryGraph& graph) {
    std::vector<std::string> ids;
    std::ranges::transform(graph.steps(), std::back_inserter(ids), [](const StoryStep& step) { return step.id(); });
    return ids;
}

// The merge of `mods` over the original's chain.
StoryMerge merged(std::vector<StoryMod> mods) { return mergeStory(StoryGraph::original(), mods); }

} // namespace

TEST_CASE("story merge: without a story mod the graph is the seed, with the seed's hash", "[story_merge]") {
    // Expected: ../guides/studio/mission-mods.md#graph-merge, Coney's own design (no counterpart in the original)
    const StoryMerge merge = merged({});
    CHECK(merge.graph.hash() == StoryGraph::original().hash());
    CHECK(idsOf(merge.graph) == idsOf(StoryGraph::original()));
    CHECK(merge.refusals.empty());
    CHECK(merge.placed.empty());
    CHECK_FALSE(merge.modded);
}

TEST_CASE("story merge: a story mod makes the story a modded session, even with nothing placed", "[story_merge]") {
    // Expected: ../guides/studio/mission-mods.md#story-manifest, Coney's own design (no counterpart in the original)
    const StoryMerge empty = merged({mod("quiet", {})});
    CHECK(empty.modded);
    CHECK(empty.graph.hash() == StoryGraph::original().hash());
    const StoryMerge refusedOnly = merged({mod("lost", {after("alley", "level70")})});
    CHECK(refusedOnly.modded);
}

TEST_CASE("story merge: a mission after a mission goes right after it", "[story_merge]") {
    // Expected: ../guides/studio/mission-mods.md#anchors, Coney's own design (no counterpart in the original)
    const StoryMerge merge = merged({mod("yard-brawl", {after("alley", "level80")})});
    CHECK(merge.refusals.empty());
    CHECK(idsBetween(merge.graph, "level80", "hub:1") ==
          std::vector<std::string>{"level80", "yard-brawl.alley", "level87"});
    CHECK(merge.graph.steps().size() == 32);
    CHECK(merge.graph.hash() != StoryGraph::original().hash());
    REQUIRE(merge.placed.size() == 1);
    CHECK(merge.placed[0].id == "yard-brawl.alley");
    CHECK(merge.placed[0].mod == "yard-brawl");
    CHECK(merge.placed[0].mission.level == "levels/a.clevel");
}

TEST_CASE("story merge: a mission after a hub chapter is its offer and opens a new chapter", "[story_merge]") {
    // Expected: ../guides/studio/mission-mods.md#anchors, Coney's own design (no counterpart in the original)
    const StoryMerge merge = merged({mod("yard-brawl", {after("docks", "hub:6")})});
    CHECK(merge.refusals.empty());
    CHECK(idsBetween(merge.graph, "hub:6", "level93") ==
          std::vector<std::string>{"hub:6", "yard-brawl.docks", "hub:6.yard-brawl.docks", "level86"});
    // The new chapter is the hub at chapter 6's checkpoint: a point in chapter 6's time, not a thirteenth chapter.
    const auto chapter = std::ranges::find_if(
        merge.graph.steps(), [](const StoryStep& step) { return step.id() == "hub:6.yard-brawl.docks"; });
    REQUIRE(chapter != merge.graph.steps().end());
    CHECK(chapter->kind == StoryStep::Kind::HubChapter);
    CHECK(chapter->chapter == 6);
    CHECK(chapter->checkpoint() == 6);
}

TEST_CASE("story merge: a replacement takes the mission's place, side step included", "[story_merge]") {
    // Expected: ../guides/studio/mission-mods.md#anchors, Coney's own design (no counterpart in the original)
    const StoryMerge merge = merged({mod("payin", {replacing("payin", "level86"), replacing("armies", "level64")})});
    CHECK(merge.refusals.empty());
    CHECK(idsBetween(merge.graph, "hub:6", "hub:7") == std::vector<std::string>{"hub:6", "payin.payin", "level93"});
    CHECK(merge.graph.steps().size() == 31);
    const auto armies =
        std::ranges::find_if(merge.graph.steps(), [](const StoryStep& step) { return step.id() == "payin.armies"; });
    REQUIRE(armies != merge.graph.steps().end());
    CHECK(armies->sideStep);
    CHECK(idsBetween(merge.graph, "hub:11", "hub:12") == std::vector<std::string>{"hub:11", "payin.armies"});
}

TEST_CASE("story merge: a mission anchored on a replaced mission goes after its replacement", "[story_merge]") {
    // Expected: ../guides/studio/mission-mods.md#anchors, Coney's own design (no counterpart in the original)
    const StoryMerge merge =
        merged({mod("payin", {replacing("payin", "level86")}), mod("later", {after("next", "level86")})});
    CHECK(merge.refusals.empty());
    CHECK(idsBetween(merge.graph, "hub:6", "hub:7") ==
          std::vector<std::string>{"hub:6", "payin.payin", "later.next", "level93"});
}

TEST_CASE("story merge: two missions on one anchor chain in load order, either way round", "[story_merge]") {
    // Expected: ../guides/studio/mission-mods.md#graph-merge, Coney's own design (no counterpart in the original)
    const StoryMod first = mod("aaa", {after("x", "level80")});
    const StoryMod second = mod("bbb", {after("y", "level80")});
    CHECK(idsBetween(merged({first, second}).graph, "level80", "hub:1") ==
          std::vector<std::string>{"level80", "aaa.x", "bbb.y", "level87"});
    CHECK(idsBetween(merged({second, first}).graph, "level80", "hub:1") ==
          std::vector<std::string>{"level80", "bbb.y", "aaa.x", "level87"});
}

TEST_CASE("story merge: two missions on one hub chapter: the first chapter offers the second", "[story_merge]") {
    // Expected: ../guides/studio/mission-mods.md#graph-merge, Coney's own design (no counterpart in the original)
    const StoryMerge merge = merged({mod("aaa", {after("x", "hub:6")}), mod("bbb", {after("y", "hub:6")})});
    CHECK(merge.refusals.empty());
    CHECK(idsBetween(merge.graph, "hub:6", "level93") ==
          std::vector<std::string>{"hub:6", "aaa.x", "hub:6.aaa.x", "bbb.y", "hub:6.bbb.y", "level86"});
}

TEST_CASE("story merge: a later mission on the same anchor goes after what follows the first", "[story_merge]") {
    // Expected: ../guides/studio/mission-mods.md#graph-merge, Coney's own design (no counterpart in the original)
    // aaa.x after level80, aaa.z after aaa.x, then bbb.y after level80: bbb.y follows aaa.x and what was placed after
    // it.
    const StoryMerge merge =
        merged({mod("aaa", {after("x", "level80"), after("z", "aaa.x")}), mod("bbb", {after("y", "level80")})});
    CHECK(merge.refusals.empty());
    CHECK(idsBetween(merge.graph, "level80", "hub:1") ==
          std::vector<std::string>{"level80", "aaa.x", "aaa.z", "bbb.y", "level87"});
}

TEST_CASE("story merge: a mission may anchor on a mod later in load order", "[story_merge]") {
    // Expected: ../guides/studio/mission-mods.md#anchors, Coney's own design (no counterpart in the original)
    const StoryMerge merge = merged({mod("aaa", {after("x", "bbb.y")}), mod("bbb", {after("y", "level80")})});
    CHECK(merge.refusals.empty());
    CHECK(idsBetween(merge.graph, "level80", "hub:1") ==
          std::vector<std::string>{"level80", "bbb.y", "aaa.x", "level87"});
}

TEST_CASE("story merge: a missing anchor refuses that mission with a message; the rest plays", "[story_merge]") {
    // Expected: ../guides/studio/mission-mods.md#graph-merge, Coney's own design (no counterpart in the original)
    const StoryMerge merge = merged({mod("yard-brawl", {after("alley", "level70"), after("docks", "hub:13"),
                                                        after("roof", "yard-brawl.alley"), after("ok", "level80")})});
    CHECK(
        merge.refusals ==
        std::vector<StoryRefusal>{
            {.missions = {"yard-brawl.alley"}, .message = "yard-brawl.alley: no story step 'level70'"},
            {.missions = {"yard-brawl.docks"}, .message = "yard-brawl.docks: no story step 'hub:13'"},
            {.missions = {"yard-brawl.roof"}, .message = "yard-brawl.roof: story step 'yard-brawl.alley' was refused"},
        });
    CHECK(idsBetween(merge.graph, "level80", "hub:1") ==
          std::vector<std::string>{"level80", "yard-brawl.ok", "level87"});
    REQUIRE(merge.placed.size() == 1);
    CHECK(merge.placed[0].id == "yard-brawl.ok");
}

TEST_CASE("story merge: a failed before guard refuses the mission; a kept one places it", "[story_merge]") {
    // Expected: ../guides/studio/mission-mods.md#graph-merge, Coney's own design (no counterpart in the original)
    const StoryMerge kept = merged({mod("aaa", {after("x", "level80", "level87")})});
    CHECK(kept.refusals.empty());
    CHECK(idsBetween(kept.graph, "level80", "hub:1") == std::vector<std::string>{"level80", "aaa.x", "level87"});

    const StoryMerge moved =
        merged({mod("aaa", {after("x", "level80")}), mod("bbb", {after("y", "level80", "level87")})});
    CHECK(moved.refusals ==
          std::vector<StoryRefusal>{
              {.missions = {"bbb.y"}, .message = "bbb.y: expected 'level87' after 'level80', found 'aaa.x'"}});
    CHECK(idsBetween(moved.graph, "level80", "hub:1") == std::vector<std::string>{"level80", "aaa.x", "level87"});
}

TEST_CASE("story merge: a before guard on a replaced mission names its place", "[story_merge]") {
    // Expected: ../guides/studio/mission-mods.md#graph-merge, Coney's own design (no counterpart in the original)
    const StoryMerge merge =
        merged({mod("payin", {replacing("payin", "level86")}), mod("bbb", {after("y", "hub:6", "level86")})});
    CHECK(merge.refusals.empty());
    CHECK(idsBetween(merge.graph, "hub:6", "hub:7") ==
          std::vector<std::string>{"hub:6", "bbb.y", "hub:6.bbb.y", "payin.payin", "level93"});
}

TEST_CASE("story merge: a cycle refuses every mission in it with one message", "[story_merge]") {
    // Expected: ../guides/studio/mission-mods.md#graph-merge, Coney's own design (no counterpart in the original)
    const StoryMerge merge = merged({mod("aaa", {after("x", "bbb.y")}), mod("bbb", {after("y", "aaa.x")}),
                                     mod("ccc", {after("z", "aaa.x"), after("w", "level80")})});
    CHECK(merge.refusals == std::vector<StoryRefusal>{
                                {.missions = {"aaa.x", "bbb.y"}, .message = "story cycle: aaa.x, bbb.y, aaa.x"},
                                {.missions = {"ccc.z"}, .message = "ccc.z: story step 'aaa.x' was refused"},
                            });
    CHECK(idsBetween(merge.graph, "level80", "hub:1") == std::vector<std::string>{"level80", "ccc.w", "level87"});
}

TEST_CASE("story merge: of two replacements of one mission the first in load order is kept", "[story_merge]") {
    // Expected: ../guides/studio/mission-mods.md#graph-merge, Coney's own design (no counterpart in the original)
    const StoryMerge merge = merged({mod("aaa", {replacing("x", "level86")}), mod("bbb", {replacing("y", "level86")})});
    CHECK(merge.refusals == std::vector<StoryRefusal>{
                                {.missions = {"bbb.y"}, .message = "bbb.y: 'level86' is already replaced by 'aaa.x'"}});
    CHECK(idsBetween(merge.graph, "hub:6", "hub:7") == std::vector<std::string>{"hub:6", "aaa.x", "level93"});
}

TEST_CASE("story merge: only a mission can be replaced", "[story_merge]") {
    // Expected: ../guides/studio/mission-mods.md#anchors, Coney's own design (no counterpart in the original)
    const StoryMerge merge = merged({mod("aaa", {replacing("x", "hub:3")})});
    CHECK(merge.refusals == std::vector<StoryRefusal>{
                                {.missions = {"aaa.x"}, .message = "aaa.x: 'hub:3' is a hub chapter, not a mission"}});
    CHECK(merge.graph.hash() == StoryGraph::original().hash());
}

TEST_CASE("story merge: the same mods in the same order give the same graph", "[story_merge]") {
    // Expected: ../guides/studio/mission-mods.md#graph, Coney's own design (no counterpart in the original)
    const std::vector<StoryMod> mods{mod("aaa", {after("x", "hub:2"), replacing("p", "level31")}),
                                     mod("bbb", {after("y", "aaa.x"), after("q", "level99")})};
    // 31 steps, aaa.x and its new chapter, bbb.y and bbb.q; aaa.p takes level31's place.
    CHECK(mergeStory(StoryGraph::original(), mods).graph.steps().size() == 35);
    CHECK(mergeStory(StoryGraph::original(), mods).graph.hash() ==
          mergeStory(StoryGraph::original(), mods).graph.hash());
}
