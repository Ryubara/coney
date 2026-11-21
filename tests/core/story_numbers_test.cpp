// SPDX-License-Identifier: GPL-3.0-or-later
// The session's level numbers for mod missions (docs/guides/studio/mission-mods.md#graph-natives): Coney's own, with
// no counterpart in the original, which has no mods. Every mod here is synthetic.
#include "core/story_numbers.h"

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "core/story_graph.h"
#include "core/story_manifest.h"
#include "core/story_merge.h"

using coney::story::levelNumberPool;
using coney::story::mergeStory;
using coney::story::NumberedMission;
using coney::story::numberModMissions;
using coney::story::StoryGraph;
using coney::story::StoryLoad;
using coney::story::StoryMerge;
using coney::story::StoryMission;
using coney::story::StoryMod;
using coney::story::StoryStep;

namespace {

// A mission `id` after `anchor`.
StoryMission after(std::string id, std::string anchor) {
    StoryMission mission;
    mission.id = std::move(id);
    mission.level = "levels/a.clevel";
    mission.after = std::move(anchor);
    return mission;
}

// A mission `id` in `target`'s place.
StoryMission replacing(std::string id, std::string target) {
    StoryMission mission;
    mission.id = std::move(id);
    mission.level = "levels/b.clevel";
    mission.replace = std::move(target);
    return mission;
}

// The merge of one mod `name` with `missions` over the original's chain.
StoryMerge merged(std::string name, std::vector<StoryMission> missions) {
    StoryMod mod{.name = std::move(name), .story = {}};
    mod.story.missions = std::move(missions);
    const std::vector<StoryMod> mods{std::move(mod)};
    return mergeStory(StoryGraph::original(), mods);
}

// The ids of `graph`'s steps, in order.
std::vector<std::string> ids(const StoryGraph& graph) {
    std::vector<std::string> out;
    for (const StoryStep& step : graph.steps()) {
        out.push_back(step.id());
    }
    return out;
}

// The level number of `graph`'s step whose id is `id`; -1 when the chain has none.
int levelOf(const StoryGraph& graph, const std::string& id) {
    const auto at = graph.find(id);
    return at ? graph.steps()[at.value()].level : -1;
}

// Mod mission `id` numbered `level`.
StoryStep numberedStep(std::string id, int level) {
    StoryStep step = StoryStep::modMission(std::move(id));
    step.level = level;
    return step;
}

// The NTSC-U level records' numbers from 2 to 59 (docs/references/levels.md): the story missions in that range.
const std::vector<int> kUsedNtscNumbers{2, 3, 5, 9, 11, 14, 20, 31, 34, 51, 52, 54, 55, 60, 61, 62, 63, 64, 80, 95, 99};

} // namespace

TEST_CASE("story numbers: the pool is 2-59 less the numbers the level records use", "[story_numbers]") {
    // Expected: mission-mods.md#graph-natives (the 45 numbers of the NTSC-U disc), inferred from the range checks
    const std::vector<int> pool = levelNumberPool(kUsedNtscNumbers);
    std::vector<int> expected{4, 6, 7, 8, 10, 12, 13, 15, 16, 17, 18, 19};
    for (int n = 21; n <= 30; ++n) {
        expected.push_back(n);
    }
    expected.insert(expected.end(), {32, 33});
    for (int n = 35; n <= 50; ++n) {
        expected.push_back(n);
    }
    expected.insert(expected.end(), {53, 56, 57, 58, 59});
    CHECK(expected.size() == 45);
    CHECK(pool == expected);
    // Nothing outside the range the scripts accept: never 0, 1, 60-69, 95 or 100 and above.
    CHECK(std::ranges::none_of(pool, [](int n) { return n < 2 || n > 59; }));
}

TEST_CASE("story numbers: mod missions get the pool's numbers in chain order", "[story_numbers]") {
    // Expected: mission-mods.md#graph-natives
    StoryMerge merge =
        merged("yard", {replacing("payin", "level86"), after("docks", "hub:6"), after("alley", "level80")});
    const std::vector<int> pool{4, 6, 7};
    const std::vector<NumberedMission> numbered = numberModMissions(merge, pool);
    // Chain order: alley (after level80), then docks (hub 6's offer), then payin (level86's place).
    CHECK(numbered == std::vector<NumberedMission>{{.id = "yard.alley", .level = 4},
                                                   {.id = "yard.docks", .level = 6},
                                                   {.id = "yard.payin", .level = 7}});
    CHECK(levelOf(merge.graph, "yard.alley") == 4);
    CHECK(levelOf(merge.graph, "yard.docks") == 6);
    CHECK(levelOf(merge.graph, "yard.payin") == 7);
    CHECK(merge.refusals.empty());

    // The views now name the numbers: level80 loads level4, level4 loads level87, hub 6 offers level6 and level6 loads
    // the new chapter at checkpoint 6.
    CHECK(merge.graph.nextLoad(80) == StoryLoad{.level = 4, .checkpoint = 1});
    CHECK(merge.graph.nextLoad(4) == StoryLoad{.level = 87, .checkpoint = 1});
    CHECK(merge.graph.offerOf("hub:6") == numberedStep("yard.docks", 6));
    CHECK(merge.graph.nextLoad(6) == StoryLoad{.level = 95, .checkpoint = 6});
    CHECK(merge.graph.nextLoad(7) == StoryLoad{.level = 93, .checkpoint = 1});
}

TEST_CASE("story numbers: without mod missions nothing changes", "[story_numbers]") {
    // Expected: mission-mods.md#graph-merge (the unmodded graph's hash is the seed's)
    StoryMerge merge = merged("quiet", {});
    const std::vector<int> pool{4};
    CHECK(numberModMissions(merge, pool).empty());
    CHECK(merge.graph.hash() == StoryGraph::original().hash());
    CHECK(merge.refusals.empty());
}

TEST_CASE("story numbers: a mission past the pool is refused with its message and leaves the chain",
          "[story_numbers]") {
    // Expected: mission-mods.md#graph-natives ("a story mission past the pool is refused with a message")
    StoryMerge merge = merged("yard", {after("alley", "level80"), after("docks", "hub:6")});
    const std::vector<int> pool{4};
    const std::vector<NumberedMission> numbered = numberModMissions(merge, pool);
    CHECK(numbered == std::vector<NumberedMission>{{.id = "yard.alley", .level = 4}});

    // docks and the new chapter it opened are gone: hub 6 offers level86 again, as in the original.
    const std::vector<std::string> chain = ids(merge.graph);
    CHECK(std::ranges::find(chain, "yard.docks") == chain.end());
    CHECK(std::ranges::find(chain, "hub:6.yard.docks") == chain.end());
    CHECK(merge.graph.offerOf("hub:6") == StoryStep::mission(86));
    CHECK(std::ranges::none_of(merge.placed, [](const auto& placed) { return placed.id == "yard.docks"; }));
    REQUIRE(merge.refusals.size() == 1);
    CHECK(merge.refusals.front().missions == std::vector<std::string>{"yard.docks"});
    CHECK(merge.refusals.front().message == "yard.docks: no level number is left for it (the session has 1)");
}

TEST_CASE("story numbers: a replacement past the pool gives the original mission its place back", "[story_numbers]") {
    // Expected: mission-mods.md#anchors (a replaced mission plays again once nothing stands in for it)
    StoryMerge merge = merged("yard", {replacing("payin", "level86")});
    CHECK(numberModMissions(merge, {}).empty());
    CHECK(ids(merge.graph) == ids(StoryGraph::original()));
    CHECK(merge.graph.hash() == StoryGraph::original().hash());
    REQUIRE(merge.refusals.size() == 1);
    CHECK(merge.refusals.front().missions == std::vector<std::string>{"yard.payin"});
}
