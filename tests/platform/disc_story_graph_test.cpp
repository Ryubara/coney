// SPDX-License-Identifier: GPL-3.0-or-later

// Checks Coney's story graph against the player's own disc: the disc's `global.lua` runs in a level's script state with
// a profile whose completion records are set by hand, and for every prefix of the story complete (nothing, each
// mission in turn up to `level64`, and `level64` alone) the level and checkpoint its own `runNextMission` asks for,
// and the level its `findLastMission` returns, are compared with the graph's. It runs only when the environment
// variable CONEY_DISC names the disc and skips otherwise; it prints counts only (LEGAL.md). Coney's natives over the
// graph (scripting/story_natives.h) are run beside the disc's own functions the same way, and a mod mission's pool
// number is run through the disc's own `global.lua` main chunk and `UnlockAndLoad`.

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <SDL3/SDL_stdinc.h>
#include <catch2/catch_test_macros.hpp>

#include "core/story_graph.h"
#include "core/story_manifest.h"
#include "core/story_merge.h"
#include "core/story_numbers.h"
#include "fileio/disc.h"
#include "fileio/wad.h"
#include "gamemodes/level_start.h"
#include "scripting/config_strings.h"
#include "scripting/lua_value.h"
#include "scripting/script_system.h"
#include "scripting/story_natives.h"
#include "warriors/game_state.h"
#include "warriors/level_table.h"
#include "warriors/profile_record.h"

namespace {

// The level number of a `MenuLoadLevel` name (`level95` gives 95); nothing for any other name.
std::optional<int> levelNumber(const std::string& name) {
    constexpr std::string_view kPrefix = "level";
    if (!name.starts_with(kPrefix) || name.size() == kPrefix.size()) {
        return std::nullopt;
    }
    int number = 0;
    for (const char digit : std::string_view(name).substr(kPrefix.size())) {
        if (digit < '0' || digit > '9') {
            return std::nullopt;
        }
        number = number * 10 + (digit - '0');
    }
    return number;
}

// The player's disc as a WAD; nothing (and the test skipped) when CONEY_DISC is not set.
std::optional<coney::io::Wad> openDisc() {
    const char* discPath = SDL_getenv("CONEY_DISC");
    if (discPath == nullptr || *discPath == '\0') {
        return std::nullopt;
    }
    auto disc = coney::io::Disc::open(discPath);
    REQUIRE(disc.has_value());
    auto wad = coney::io::Wad::open(std::move(*disc));
    REQUIRE(wad.has_value());
    return std::move(*wad);
}

// A profile with exactly `complete` levels complete: every record locked, then their completion records open.
void setComplete(coney::GameState& state, const std::vector<int>& complete) {
    state.saved.lockedBits = coney::SavedProgress{}.lockedBits;
    for (const int level : complete) {
        REQUIRE(state.player.unlocks.unlock(state.saved, static_cast<std::uint8_t>(level), 0, 0) >= 1);
    }
}

// What a runNextMission(0) call asked for: the level's number and the checkpoint; nothing when no level was asked for.
std::optional<std::pair<int, int>> runNext(coney::LevelScripts& scripts) {
    scripts.host().clearNextLevel();
    scripts.state().checkPoint = 0;
    const std::vector<coney::script::Value> args{coney::script::Value(0.0)};
    REQUIRE(scripts.scripts().call("runNextMission", args));
    const std::optional<std::string>& next = scripts.host().nextLevel();
    const std::optional<int> level = next ? levelNumber(*next) : std::nullopt;
    if (!level) {
        return std::nullopt;
    }
    return std::pair{*level, static_cast<int>(scripts.state().checkPoint)};
}

// What findLastMission() returned; nothing when not a number.
std::optional<double> findLast(coney::LevelScripts& scripts) {
    const auto found = scripts.scripts().callResults("findLastMission");
    return found && !found->empty() ? (*found)[0].number() : std::nullopt;
}

// The cases: the first k missions of `graph` complete for k = 0 to all, then level64 alone.
std::vector<std::vector<int>> storyPrefixes(const coney::story::StoryGraph& graph) {
    const std::vector<int> missions = graph.findLastMissionLevels();
    const std::vector<int> inOrder(missions.rbegin(), missions.rend());
    std::vector<std::vector<int>> cases;
    for (std::size_t count = 0; count <= inOrder.size(); ++count) {
        cases.emplace_back(inOrder.begin(), inOrder.begin() + static_cast<std::ptrdiff_t>(count));
    }
    cases.push_back({64});
    return cases;
}

} // namespace

TEST_CASE("story graph: the disc's runNextMission and findLastMission agree with it for every prefix of the story",
          "[disc][story][story_graph]") {
    // Expected: story-progress.md#global-functions, confirmed (code)
    const char* discPath = SDL_getenv("CONEY_DISC");
    if (discPath == nullptr || *discPath == '\0') {
        SKIP("CONEY_DISC is not set: no disc to check");
    }
    auto disc = coney::io::Disc::open(discPath);
    REQUIRE(disc.has_value());
    auto wad = coney::io::Wad::open(std::move(*disc));
    REQUIRE(wad.has_value());

    const coney::story::StoryGraph graph = coney::story::StoryGraph::original();
    // The missions in story order: findLastMission's list, earliest first.
    std::vector<int> missions = graph.findLastMissionLevels();
    REQUIRE(missions.size() == 19);
    std::vector<int> inOrder(missions.rbegin(), missions.rend());

    // The cases: the first k missions complete for k = 0 to 19, then level64 alone.
    std::vector<std::vector<int>> cases;
    for (std::size_t count = 0; count <= inOrder.size(); ++count) {
        cases.emplace_back(inOrder.begin(), inOrder.begin() + static_cast<std::ptrdiff_t>(count));
    }
    cases.push_back({64});

    // One level's script state; the preloads fill the unlock records the completion is read from.
    coney::LevelScripts scripts(coney::script::wadScriptSource(*wad), "level99", 1, [](std::string_view) {});
    coney::GameState& state = scripts.state();
    coney::QuietBindingHost& host = scripts.host();
    std::size_t equal = 0;
    for (const std::vector<int>& complete : cases) {
        INFO(complete.size() << " missions complete");
        // global.lua's main chunk, as at each level load, so LastLevel starts at 0 and the unlock records exist.
        REQUIRE(scripts.scripts().runFile(coney::script::kGlobalScript));
        // A profile with exactly these levels complete (every record locked, then their completion records open; a
        // level may have more than one).
        state.saved.lockedBits = coney::SavedProgress{}.lockedBits;
        for (const int level : complete) {
            REQUIRE(state.player.unlocks.unlock(state.saved, static_cast<std::uint8_t>(level), 0, 0) >= 1);
        }
        host.clearNextLevel();
        state.checkPoint = 0;

        const auto found = scripts.scripts().callResults("findLastMission");
        const std::vector<coney::script::Value> args{coney::script::Value(0.0)};
        REQUIRE(scripts.scripts().call("runNextMission", args));
        const std::optional<std::string> next = host.nextLevel();

        // The graph's answer through the same completion records.
        const int last = graph.lastMission([&](int level) {
            return state.player.unlocks.isLevelComplete(state.saved, static_cast<std::uint8_t>(level));
        });
        const std::optional<coney::story::StoryLoad> load = graph.nextLoad(last);

        const bool sameLast = found.has_value() && !found->empty() && (*found)[0].number() == static_cast<double>(last);
        const bool sameLoad = load.has_value() && next.has_value() && levelNumber(*next) == load->level &&
                              static_cast<int>(state.checkPoint) == load->checkpoint;
        CHECK(sameLast);
        CHECK(sameLoad);
        equal += sameLast && sameLoad ? 1 : 0;
    }
    std::printf("  story graph against the disc's runNextMission: %zu of %zu cases equal\n", equal, cases.size());
    CHECK(equal == cases.size());
}

TEST_CASE("story natives: Coney's runNextMission and findLastMission load what the disc's do for every prefix of the "
          "story",
          "[disc][story][story_natives]") {
    // Expected: the disc's own global.lua:f61 and f62 (story-progress.md#global-functions), confirmed (code)
    std::optional<coney::io::Wad> wad = openDisc();
    if (!wad) {
        SKIP("CONEY_DISC is not set: no disc to check");
    }
    const coney::story::StoryGraph graph = coney::story::StoryGraph::original();
    const std::vector<std::vector<int>> cases = storyPrefixes(graph);
    REQUIRE(cases.size() == 21);

    coney::LevelScripts scripts(coney::script::wadScriptSource(*wad), "level99", 1, [](std::string_view) {});
    std::size_t equal = 0;
    for (const std::vector<int>& complete : cases) {
        INFO(complete.size() << " missions complete");
        // The disc's functions, as global.lua's main chunk leaves them.
        REQUIRE(scripts.scripts().runFile(coney::script::kGlobalScript));
        setComplete(scripts.state(), complete);
        const std::optional<double> luaLast = findLast(scripts);
        const std::optional<std::pair<int, int>> luaLoad = runNext(scripts);
        // The main chunk again (LastLevel back to 0), then the natives set over the functions, as at a level entry.
        REQUIRE(scripts.scripts().runFile(coney::script::kGlobalScript));
        coney::script::installStoryNatives(scripts.scripts(), graph, scripts.state().modStory);
        const auto installed = scripts.scripts().vm().global("runNextMission").function();
        REQUIRE(installed != nullptr);
        REQUIRE(static_cast<bool>(installed->native));
        const std::optional<double> nativeLast = findLast(scripts);
        const std::optional<std::pair<int, int>> nativeLoad = runNext(scripts);

        const bool same = luaLast.has_value() && luaLast == nativeLast && luaLoad.has_value() && luaLoad == nativeLoad;
        CHECK(same);
        equal += same ? 1 : 0;
    }
    std::printf("  Coney's story natives against the disc's runNextMission: %zu of %zu cases equal\n", equal,
                cases.size());
    CHECK(equal == cases.size());
}

TEST_CASE("story natives: a mod mission's pool number passes the disc's GetLevel and its UnlockAndLoad leads on",
          "[disc][story][story_natives]") {
    // Expected: mission-mods.md#graph-natives (the 45 free numbers of the NTSC-U disc pass GetLevel, global.lua:f64,
    // and UnlockAndLoad's level check, f59), inferred from the code
    std::optional<coney::io::Wad> wad = openDisc();
    if (!wad) {
        SKIP("CONEY_DISC is not set: no disc to check");
    }
    coney::LevelScripts scripts(coney::script::wadScriptSource(*wad), "level99", 1, [](std::string_view) {});
    coney::GameState& state = scripts.state();
    std::vector<int> used;
    std::optional<std::size_t> freeIndex;
    for (std::size_t index = 0; index < coney::LevelTable::kCapacity; ++index) {
        if (const coney::LevelRecord* record = state.levels.at(index)) {
            used.push_back(static_cast<int>(record->number));
        } else if (!freeIndex) {
            freeIndex = index;
        }
    }
    REQUIRE(freeIndex.has_value());
    const std::vector<int> pool = coney::story::levelNumberPool(used);
    CHECK(pool.size() == 45);

    std::size_t passed = 0;
    for (const int number : pool) {
        INFO("pool number " << number);
        // One mod mission after level80, numbered `number`, with its record and its number in the story progress.
        coney::story::StoryMod mod{.name = "test", .story = {}};
        coney::story::StoryMission mission;
        mission.id = "alley";
        mission.level = "levels/alley.clevel";
        mission.after = "level80";
        mod.story.missions.push_back(mission);
        const std::vector<coney::story::StoryMod> mods{mod};
        coney::story::StoryMerge merge = coney::story::mergeStory(coney::story::StoryGraph::original(), mods);
        const std::vector<int> one{number};
        REQUIRE(coney::story::numberModMissions(merge, one).size() == 1);
        coney::LevelRecord record;
        record.id = static_cast<double>(freeIndex.value_or(0));
        record.name = std::format("level{}", number);
        record.worldName = record.name;
        record.number = number;
        REQUIRE(state.levels.set(record));
        state.modStory.clearNumbers();
        state.modStory.setEntries({});
        state.modStory.assign(static_cast<std::uint8_t>(number), "test.alley");
        state.currentLevel = freeIndex.value_or(0);
        state.checkPoint = 1;

        // The disc's main chunk as at the level's entry: GetLevel keeps the number, and the level counts as below 100.
        REQUIRE(scripts.scripts().runFile(coney::script::kGlobalScript));
        const bool keeps = scripts.scripts().vm().global("Level").number() == static_cast<double>(number);
        const bool belowHundred = scripts.scripts().vm().global("p2Inventory").table() != nullptr;
        CHECK(keeps);
        CHECK(belowHundred);

        // The mission's end: the disc's UnlockAndLoad completes it by its id and the natives load level87.
        coney::script::installStoryNatives(scripts.scripts(), merge.graph, state.modStory);
        setComplete(state, {99, 80});
        scripts.host().clearNextLevel();
        REQUIRE(scripts.scripts().call("UnlockAndLoad"));
        const bool completed = state.modStory.isComplete("test.alley");
        const bool next =
            scripts.host().nextLevel() == std::optional<std::string>("level87") && state.checkPoint == 1.0;
        CHECK(completed);
        CHECK(next);
        passed += keeps && belowHundred && completed && next ? 1 : 0;
    }
    std::printf("  mod mission pool numbers through the disc's global.lua: %zu of %zu pass\n", passed, pool.size());
    CHECK(passed == pool.size());
}
