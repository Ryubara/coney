// SPDX-License-Identifier: GPL-3.0-or-later
// Coney's findLastMission and runNextMission natives over the story graph, the hub's offer and the session's numbers
// for mod missions (docs/guides/studio/mission-mods.md#graph-natives). The expected loads of the original's story are
// the rows of `global.lua`'s runNextMission and findLastMission as docs/research/story-progress.md#global-functions
// lists them; the disc test (tests/platform/disc_story_graph_test.cpp) runs the disc's own Lua beside the natives. The
// scripts and mods here are synthetic, compiled by Coney's own compiler.
#include "scripting/story_natives.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <format>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "core/error.h"
#include "core/story_graph.h"
#include "core/story_manifest.h"
#include "core/story_merge.h"
#include "core/story_numbers.h"
#include "scripting/lua_chunk.h"
#include "scripting/lua_compiler.h"
#include "scripting/lua_value.h"
#include "scripting/lua_vm.h"
#include "scripting/script_system.h"
#include "warriors/game_state.h"
#include "warriors/level_table.h"

using coney::Error;
using coney::ErrorCode;
using coney::GameState;
using coney::LevelRecord;
using coney::script::LuaVm;
using coney::script::ScriptSystem;
using coney::script::StoryHooks;
using coney::script::Value;
using coney::story::mergeStory;
using coney::story::StoryGraph;
using coney::story::StoryMerge;
using coney::story::StoryMission;
using coney::story::StoryMod;
using coney::story::StoryStep;

namespace {

// global.lua's two functions as a stand-in of our own that loads a level no story row names, so a call that still
// reaches them shows; and the hub: fRunMission with a level of our own in chapter 1's row and chapter 6's.
constexpr std::string_view kGlobal = R"(
LastLevel = 0
function findLastMission() return 77 end
function runNextMission(arg) MenuLoadLevel("level77") end
)";
constexpr std::string_view kHub = R"(
fRunMission = {[1] = {flag = 101, level = 77}, [6] = {flag = 606, level = 77}, [11] = {flag = 1111}}
)";

// `source` compiled into a chunk in the game's layout.
std::vector<std::byte> chunkOf(std::string_view source) {
    auto compiled = coney::script::compileLua(source, "=test");
    REQUIRE(compiled.has_value());
    return compiled ? coney::script::writeLuaChunk(*compiled) : std::vector<std::byte>{};
}

// A number argument, 0 when absent or not a number.
double numberArg(std::span<const Value> args, std::size_t index) {
    return index < args.size() ? args[index].number().value_or(0.0) : 0.0;
}

// A script system over synthetic scripts with the bindings the natives call: SetCheckPoint and GetCheckPoint on the
// game state, SoundStopMusicTrack, MenuLoadLevel and HUDLaunchMissionComplete noted in `calls`, and
// UM_IsLevelComplete answering from `complete` (a mod level's number from the game state's story progress, as the
// game's binding does).
struct Story {
    GameState state;
    std::set<int> complete;
    std::vector<std::string> calls;
    std::vector<std::string> log;
    std::map<std::string, std::vector<std::byte>, std::less<>> files;
    ScriptSystem scripts;

    Story()
        : scripts(
              [this](std::string_view name) -> std::expected<std::vector<std::byte>, Error> {
                  const auto found = files.find(name);
                  if (found == files.end()) {
                      return coney::fail(ErrorCode::NotFound, std::string(name) + ": no such script");
                  }
                  return found->second;
              },
              [this](ScriptSystem& /*system*/, LuaVm& vm) { install(vm); },
              [this](std::string_view line) { log.emplace_back(line); }) {
        files["global.lua"] = chunkOf(kGlobal);
        files["level95.lua"] = chunkOf(kHub);
        files["level99.lua"] = chunkOf("");
    }

    // Registers the bindings described above.
    void install(LuaVm& vm) {
        using Results = std::expected<std::vector<Value>, Error>;
        vm.registerFunction("SetCheckPoint", [this](std::span<const Value> args) -> Results {
            state.checkPoint = numberArg(args, 0);
            calls.push_back(std::format("SetCheckPoint {}", numberArg(args, 0)));
            return std::vector<Value>{};
        });
        vm.registerFunction("GetCheckPoint", [this](std::span<const Value>) -> Results {
            return std::vector<Value>{Value(state.checkPoint)};
        });
        vm.registerFunction("SoundStopMusicTrack", [this](std::span<const Value>) -> Results {
            calls.emplace_back("SoundStopMusicTrack");
            return std::vector<Value>{};
        });
        vm.registerFunction("MenuLoadLevel", [this](std::span<const Value> args) -> Results {
            const auto name = !args.empty() ? args[0].string() : std::nullopt;
            calls.push_back(std::format("MenuLoadLevel {}", name.value_or("?")));
            return std::vector<Value>{};
        });
        vm.registerFunction("HUDLaunchMissionComplete", [this](std::span<const Value> args) -> Results {
            calls.push_back(std::format("HUDLaunchMissionComplete {}", numberArg(args, 0)));
            return std::vector<Value>{};
        });
        vm.registerFunction("UM_IsLevelComplete", [this](std::span<const Value> args) -> Results {
            const int level = static_cast<int>(numberArg(args, 0));
            const std::optional<bool> mod = state.modStory.isLevelComplete(static_cast<std::uint8_t>(level));
            const bool done = mod.value_or(complete.contains(level));
            return std::vector<Value>{done ? Value(1.0) : Value()};
        });
    }

    // runNextMission(arg) with `LastLevel` set to `lastLevel` first (nil when nothing), the calls noted afresh.
    void runNext(double arg, std::optional<double> lastLevel = 0.0) {
        calls.clear();
        scripts.vm().setGlobal("LastLevel", lastLevel ? Value(*lastLevel) : Value());
        const std::vector<Value> args{Value(arg)};
        REQUIRE(scripts.call("runNextMission", args));
    }

    // What findLastMission() returns; nothing when it returns no number.
    std::optional<double> findLast() {
        const auto results = scripts.callResults("findLastMission");
        REQUIRE(results.has_value());
        return results && !results->empty() ? (*results)[0].number() : std::nullopt;
    }

    // fRunMission[row].level; nothing when nil.
    std::optional<double> runMissionLevel(int row) {
        const Value rows = scripts.vm().global("fRunMission");
        REQUIRE(rows.table() != nullptr);
        const Value entry = rows.table()->get(Value(static_cast<double>(row)));
        REQUIRE(entry.table() != nullptr);
        return entry.table()->field("level").number();
    }

    // Fills the level table with `numbers`, at indices 0, 1, 2...
    void levels(const std::vector<int>& numbers) {
        for (std::size_t index = 0; index < numbers.size(); ++index) {
            LevelRecord record;
            record.id = static_cast<double>(index);
            record.number = numbers[index];
            record.name = std::format("level{}", numbers[index]);
            REQUIRE(state.levels.set(record));
        }
    }
};

// The calls of a load of `level` at `checkpoint`, as the Lua's runNextMission makes them.
std::vector<std::string> loadCalls(int level, int checkpoint) {
    return {std::format("SetCheckPoint {}", checkpoint), "SoundStopMusicTrack",
            std::format("MenuLoadLevel level{}", level)};
}

// A mission `id` after `anchor`.
StoryMission after(std::string id, std::string anchor) {
    StoryMission mission;
    mission.id = std::move(id);
    mission.title = "A Title";
    mission.level = "levels/a.clevel";
    mission.after = std::move(anchor);
    return mission;
}

// The merge of mod `yard` with `missions` over the original's chain.
StoryMerge merged(std::vector<StoryMission> missions) {
    StoryMod mod{.name = "yard", .story = {}};
    mod.story.missions = std::move(missions);
    const std::vector<StoryMod> mods{std::move(mod)};
    return mergeStory(StoryGraph::original(), mods);
}

// One row of the walk: the latest mission complete (0: none) and the level and checkpoint loaded after it.
struct Expected {
    int last;
    int level;
    int checkpoint;
};

// runNextMission's 20 rows, story-progress.md#global-functions, in story order (level64 last, a side step).
constexpr std::array<Expected, 20> kRows{{{0, 99, 1},  {99, 80, 1}, {80, 87, 1}, {87, 95, 1},  {34, 95, 2},
                                          {2, 95, 3},  {3, 95, 4},  {5, 95, 5},  {81, 95, 6},  {86, 93, 1},
                                          {93, 95, 7}, {31, 95, 8}, {14, 95, 9}, {9, 95, 10},  {51, 52, 1},
                                          {52, 54, 1}, {54, 55, 1}, {55, 84, 1}, {84, 95, 11}, {64, 95, 12}}};

} // namespace

TEST_CASE("story natives: findLastMission and runNextMission load the original's next step for every prefix of the "
          "story",
          "[story_natives]") {
    // Expected: story-progress.md#global-functions, confirmed (code) at global.lua:f61 and f62
    Story story;
    const StoryGraph graph = StoryGraph::original();
    story.scripts.create();
    REQUIRE(story.scripts.runFile("global.lua"));
    coney::script::installStoryNatives(story.scripts, graph, story.state.modStory);
    for (const Expected& row : kRows) {
        INFO("latest complete: " << row.last);
        if (row.last != 0) {
            story.complete.insert(row.last);
        }
        CHECK(story.findLast() == static_cast<double>(row.last));
        story.runNext(0);
        CHECK(story.calls == loadCalls(row.level, row.checkpoint));
        // LastLevel was 0, so runNextMission set it from findLastMission.
        CHECK(story.scripts.vm().global("LastLevel").number() == static_cast<double>(row.last));
    }
    // level64 alone: the hub at chapter 12.
    story.complete = {64};
    story.runNext(0, std::nullopt);
    CHECK(story.calls == loadCalls(95, 12));
}

TEST_CASE("story natives: a level with no step after it loads nothing", "[story_natives]") {
    // Expected: story-progress.md#global-functions (no row: nothing loads), confirmed (code) at global.lua:f61
    Story story;
    const StoryGraph graph = StoryGraph::original();
    story.scripts.create();
    REQUIRE(story.scripts.runFile("global.lua"));
    coney::script::installStoryNatives(story.scripts, graph, story.state.modStory);
    story.state.checkPoint = 3;
    for (const double last : {95.0, 60.0, 100.0, 34.5}) {
        INFO("LastLevel " << last);
        story.runNext(1, last);
        CHECK(story.calls.empty());
        CHECK(story.state.checkPoint == 3.0);
    }
    CHECK(story.scripts.errors() == 0);
}

TEST_CASE("story natives: runNextMission(1) launches the mission complete with kind 4 after the load, (0) does not",
          "[story_natives]") {
    // Expected: story-progress.md#global-functions, confirmed (code) at global.lua:f61
    Story story;
    const StoryGraph graph = StoryGraph::original();
    story.scripts.create();
    REQUIRE(story.scripts.runFile("global.lua"));
    coney::script::installStoryNatives(story.scripts, graph, story.state.modStory);
    story.runNext(1, 34.0);
    std::vector<std::string> expected = loadCalls(95, 2);
    expected.emplace_back("HUDLaunchMissionComplete 4");
    CHECK(story.calls == expected);
    story.runNext(0, 34.0);
    CHECK(story.calls == loadCalls(95, 2));
}

TEST_CASE("story natives: a LastLevel already set is used as it is", "[story_natives]") {
    // Expected: story-progress.md#global-functions (LastLevel is set from findLastMission only when 0 or nil)
    Story story;
    const StoryGraph graph = StoryGraph::original();
    story.scripts.create();
    REQUIRE(story.scripts.runFile("global.lua"));
    coney::script::installStoryNatives(story.scripts, graph, story.state.modStory);
    story.complete = {99, 80, 87, 34, 2, 3};
    story.runNext(0, 87.0);
    CHECK(story.calls == loadCalls(95, 1));
    CHECK(story.scripts.vm().global("LastLevel").number() == 87.0);
}

TEST_CASE("story natives: a mod mission's number loads its level and its completion leads on", "[story_natives]") {
    // Expected: mission-mods.md#graph-natives and #graph-progress, Coney's own design (no counterpart in the original)
    Story story;
    StoryMerge merge = merged({after("alley", "level80")});
    const std::vector<int> pool{4};
    REQUIRE(coney::story::numberModMissions(merge, pool).size() == 1);
    story.state.modStory.assign(4, "yard.alley");
    story.scripts.create();
    REQUIRE(story.scripts.runFile("global.lua"));
    coney::script::installStoryNatives(story.scripts, merge.graph, story.state.modStory);

    // After level80 the mod's mission, by its number.
    story.complete = {99, 80};
    story.runNext(1);
    std::vector<std::string> expected = loadCalls(4, 1);
    expected.emplace_back("HUDLaunchMissionComplete 4");
    CHECK(story.calls == expected);

    // MissionCompleteUnlocks' UM_Unlock(4, 0, 0) completes it by its id; then level87 follows.
    REQUIRE(story.state.modStory.unlock(4, 0, 0));
    CHECK(story.findLast() == 4.0);
    story.runNext(0);
    CHECK(story.calls == loadCalls(87, 1));
}

TEST_CASE("story hooks: every level entry sets the natives over global.lua's and the hub load writes the offer",
          "[story_natives]") {
    // Expected: mission-mods.md#graph-natives (natives after global.lua's main chunk; the offer after level95.lua's);
    // story-progress.md#hub (chapter 1 offers level34), confirmed (code)
    Story story;
    story.levels({100, 99, 80, 95});
    StoryHooks hooks(story.scripts, story.state, merged({}));
    hooks.attach();
    story.scripts.create();
    story.scripts.enterLevel("level99");
    story.runNext(0);
    CHECK(story.calls == loadCalls(99, 1));

    // The hub at chapter 1 with level87 complete: fRunMission[1] offers level34, whatever the script wrote.
    story.complete = {99, 80, 87};
    story.state.checkPoint = 1;
    story.scripts.create();
    story.scripts.enterLevel("level95");
    CHECK(story.runMissionLevel(1) == 34.0);
    CHECK(story.runMissionLevel(6) == 77.0);
    CHECK(story.scripts.errors() == 0);
}

TEST_CASE("story hooks: mod missions get the pool's numbers, level records and their ids", "[story_natives]") {
    // Expected: mission-mods.md#graph-natives (the pool of free numbers 2-59, a record `level<n>` per mission)
    Story story;
    story.levels({100, 99, 80, 2, 3, 4, 95});
    StoryHooks hooks(story.scripts, story.state, merged({after("alley", "level80"), after("docks", "hub:6")}));
    REQUIRE(hooks.numberMissions());
    CHECK(hooks.numbers() == std::vector<coney::story::NumberedMission>{{.id = "yard.alley", .level = 5},
                                                                        {.id = "yard.docks", .level = 6}});
    for (const auto& [number, id] : {std::pair{5, "yard.alley"}, std::pair{6, "yard.docks"}}) {
        INFO(id);
        const std::optional<std::size_t> index = story.state.levels.find(std::format("level{}", number));
        REQUIRE(index.has_value());
        const LevelRecord* record = story.state.levels.at(index.value_or(0));
        REQUIRE(record != nullptr);
        CHECK(record->number == static_cast<double>(number));
        CHECK(record->id == static_cast<double>(index.value_or(0)));
        const std::string* mission = story.state.modStory.missionAt(static_cast<std::uint8_t>(number));
        REQUIRE(mission != nullptr);
        CHECK(*mission == id);
    }
    // Once only: a second call changes nothing.
    REQUIRE(hooks.numberMissions());
    CHECK(story.state.levels.count() == 9);

    // The hub at chapter 6 with level81 complete offers the mod's mission by its number.
    hooks.attach();
    story.complete = {99, 80, 87, 34, 2, 3, 5, 81};
    story.state.modStory.unlock(5, 0, 0);
    story.state.checkPoint = 6;
    story.scripts.create();
    story.scripts.enterLevel("level95");
    CHECK(story.runMissionLevel(6) == 6.0);
}

TEST_CASE("story hooks: numbering waits for the level table and refuses a mission no free record is left for",
          "[story_natives]") {
    // Expected: mission-mods.md#graph-natives (a mission past the pool is refused with a message)
    Story story;
    StoryHooks hooks(story.scripts, story.state, merged({after("alley", "level80"), after("docks", "hub:6")}));
    CHECK_FALSE(hooks.numberMissions());
    CHECK(hooks.numbers().empty());

    // Every index but one taken, by numbers outside the pool: one record left, so one mission.
    std::vector<int> numbers;
    for (int index = 0; index + 1 < static_cast<int>(coney::LevelTable::kCapacity); ++index) {
        numbers.push_back(100 + index);
    }
    story.levels(numbers);
    REQUIRE(hooks.numberMissions());
    CHECK(hooks.numbers() == std::vector<coney::story::NumberedMission>{{.id = "yard.alley", .level = 2}});
    REQUIRE(hooks.merge().refusals.size() == 1);
    CHECK(hooks.merge().refusals.front().missions == std::vector<std::string>{"yard.docks"});
    CHECK_FALSE(hooks.merge().graph.find("yard.docks").has_value());
}
