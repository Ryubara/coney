// SPDX-License-Identifier: GPL-3.0-or-later

// Checks Coney's story graph against the player's own disc: the disc's `global.lua` runs in a level's script state with
// a profile whose completion records are set by hand, and for every prefix of the story complete (nothing, each
// mission in turn up to `level64`, and `level64` alone) the level and checkpoint its own `runNextMission` asks for,
// and the level its `findLastMission` returns, are compared with the graph's. It runs only when the environment
// variable CONEY_DISC names the disc and skips otherwise; it prints counts only (LEGAL.md).

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <SDL3/SDL_stdinc.h>
#include <catch2/catch_test_macros.hpp>

#include "core/story_graph.h"
#include "fileio/disc.h"
#include "fileio/wad.h"
#include "gamemodes/level_start.h"
#include "scripting/config_strings.h"
#include "scripting/lua_value.h"
#include "scripting/script_system.h"
#include "warriors/game_state.h"
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
