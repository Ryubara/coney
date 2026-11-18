// SPDX-License-Identifier: GPL-3.0-or-later
// Mod missions' progress by mission id: the route of UM_Unlock and UM_IsLevelComplete on a mod level's number to the
// save's story section, and the story going on from the latest mission present
// (docs/guides/studio/mission-mods.md#graph-progress). Synthetic mods and numbers.
#include "warriors/mod_story_progress.h"

#include <cstdint>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "core/story_graph.h"

using coney::MissionBest;
using coney::MissionProgress;
using coney::ModStoryProgress;
using coney::story::StoryGraph;
using coney::story::StoryStep;

namespace {

// A free pool number (docs/guides/studio/mission-mods.md#graph-natives: 4 is the first on the NTSC-U disc).
constexpr std::uint8_t kModLevel = 4;

// The original's chain with mod mission `id` after level80, as mergeStory() places one.
StoryGraph withModAfterLevel80(const std::string& id) {
    const StoryGraph original = StoryGraph::original();
    std::vector<StoryStep> steps(original.steps().begin(), original.steps().end());
    steps.insert(steps.begin() + 2, StoryStep::modMission(id));
    return StoryGraph(std::move(steps));
}

} // namespace

TEST_CASE("a mod level's completion and rank records go to its mission's story entry", "[mod_story]") {
    // Expected: story-progress.md#progress, confirmed (code): completion is (level, 0, 0), ranks (level, 0, k) from
    // MissionCompleteUnlocks; mission-mods.md#graph-progress, unverified (Coney's design): kept by id
    ModStoryProgress progress;
    progress.assign(kModLevel, "yard-brawl.alley");
    REQUIRE(progress.missionAt(kModLevel) != nullptr);
    CHECK(*progress.missionAt(kModLevel) == "yard-brawl.alley");
    CHECK(progress.missionAt(5) == nullptr);

    CHECK(progress.isLevelComplete(kModLevel) == false);
    CHECK_FALSE(progress.isLevelComplete(5).has_value()); // an original level: the caller's records answer
    // MissionCompleteUnlocks: the completion, then ranks 3, 2 and 1.
    CHECK(progress.unlock(kModLevel, 0, 0));
    for (std::uint8_t rank = 3; rank > 0; --rank) {
        CHECK(progress.unlock(kModLevel, 0, rank));
    }
    // A group-1 objective and a rank past 32 match no record of a mod level: handled, nothing kept.
    CHECK(progress.unlock(kModLevel, 1, 2));
    CHECK(progress.unlock(kModLevel, 0, 33));
    CHECK_FALSE(progress.unlock(5, 0, 0));

    CHECK(progress.isLevelComplete(kModLevel) == true);
    CHECK(progress.isComplete("yard-brawl.alley"));
    REQUIRE(progress.entries().size() == 1);
    const MissionProgress& entry = progress.entries().front();
    CHECK(entry.id == "yard-brawl.alley");
    CHECK(entry.complete);
    CHECK(entry.ranks == 0b111U);

    // The best record keeps the original's four described fields.
    CHECK(
        progress.setBest(kModLevel, MissionBest{.bestScore = 900, .total = 75, .grade = 3, .percent = 88, .rest = {}}));
    CHECK_FALSE(progress.setBest(5, MissionBest{}));
    REQUIRE(progress.find("yard-brawl.alley") != nullptr);
    CHECK(progress.find("yard-brawl.alley")->bestScore == 900);
    CHECK(progress.find("yard-brawl.alley")->total == 75);
    CHECK(progress.find("yard-brawl.alley")->grade == 3);
    CHECK(progress.find("yard-brawl.alley")->percent == 88);
    CHECK(progress.entries().size() == 1);

    // The numbers are the session's: the entry stays under its id when they go.
    progress.clearNumbers();
    CHECK_FALSE(progress.isLevelComplete(kModLevel).has_value());
    CHECK(progress.isComplete("yard-brawl.alley"));
}

TEST_CASE("the story goes on from the latest mission present, skipping a missing mod's", "[mod_story]") {
    // Expected: mission-mods.md#graph-progress, unverified (Coney's design): findLastMission skips ids the chain lacks;
    // story-progress.md#global-functions, confirmed (code): latest first, by (level, 0, 0)
    const StoryGraph graph = withModAfterLevel80("yard-brawl.alley");
    ModStoryProgress progress;
    progress.setEntries({{.id = "yard-brawl.alley", .complete = true},
                         {.id = "gone.docks", .complete = true},
                         {.id = "yard-brawl.roof", .complete = false}});
    const std::set<int> complete{99, 80};
    const auto isComplete = [&complete](int level) { return complete.contains(level); };

    // yard-brawl.alley, after level80, is the latest complete mission present; gone.docks is not in the chain.
    const StoryStep* latest = coney::latestMission(graph, isComplete, progress);
    REQUIRE(latest != nullptr);
    CHECK(latest->id() == "yard-brawl.alley");

    // Without the mod in the chain the story goes on from level80.
    latest = coney::latestMission(StoryGraph::original(), isComplete, progress);
    REQUIRE(latest != nullptr);
    CHECK(latest->id() == "level80");

    // An original mission later in the chain wins over the mod's.
    const std::set<int> later{99, 80, 87};
    latest = coney::latestMission(graph, [&later](int level) { return later.contains(level); }, progress);
    REQUIRE(latest != nullptr);
    CHECK(latest->id() == "level87");

    CHECK(coney::latestMission(StoryGraph::original(), [](int) { return false; }, ModStoryProgress{}) == nullptr);
}

TEST_CASE("a save's story mods the session lacks are named, and their entries stay", "[mod_story]") {
    // Expected: mission-mods.md#graph-progress, unverified (Coney's design): the warning names the missing mod
    const StoryGraph graph = withModAfterLevel80("yard-brawl.alley");
    ModStoryProgress progress;
    const std::vector<MissionProgress> loaded{{.id = "gone.docks", .complete = true},
                                              {.id = "yard-brawl.alley", .complete = true},
                                              {.id = "gone.pier"},
                                              {.id = "also-gone.roof", .ranks = 1}};
    progress.setEntries(loaded);
    CHECK(coney::warnMissingStoryMods(progress, graph) == std::vector<std::string>{"also-gone", "gone"});
    CHECK(progress.entries() == loaded);
    CHECK(coney::warnMissingStoryMods(ModStoryProgress{}, graph).empty());
}
