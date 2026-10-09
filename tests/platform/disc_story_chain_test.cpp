// SPDX-License-Identifier: GPL-3.0-or-later

// Checks against the player's own disc that the story chains from one level to the next as `--play-level` does when a
// mission ends: the end of the mission (`HUDLaunchMissionComplete`) is kept by the quiet bindings host, the scripts'
// `UnlockAndLoad` (the unlocks, then `runNextMission(1)`) chooses the next level and checkpoint, and the saved
// progress carries over to the next level's scripts, so the order is the story's (docs/research/scripting.md#run-next-
// mission). It runs only when the environment variable CONEY_DISC names the disc and skips otherwise; it prints
// counts only (LEGAL.md).

#include <cstddef>
#include <cstdio>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include <SDL3/SDL_stdinc.h>
#include <catch2/catch_test_macros.hpp>

#include "fileio/disc.h"
#include "fileio/wad.h"
#include "gamemodes/level_start.h"
#include "scripting/config_strings.h"
#include "scripting/script_system.h"

namespace {

// The scripts of `level` at `checkpoint`, run as gameplay's start runs them (the level script, so global.lua is in).
std::unique_ptr<coney::LevelScripts> startLevel(const coney::io::Wad& wad, const std::string& level, int checkpoint) {
    auto scripts = std::make_unique<coney::LevelScripts>(coney::script::wadScriptSource(wad), level, checkpoint,
                                                         [](std::string_view) {});
    (void)coney::runLevelScript(scripts->scripts(), scripts->state(), scripts->humans(), scripts->flags(), level,
                                &scripts->spawnRecords());
    return scripts;
}

} // namespace

TEST_CASE("a mission's end chooses the next mission and carries the progress on", "[disc][story][chain]") {
    const char* discPath = SDL_getenv("CONEY_DISC");
    if (discPath == nullptr || *discPath == '\0') {
        SKIP("CONEY_DISC is not set: no disc to check");
    }
    auto disc = coney::io::Disc::open(discPath);
    REQUIRE(disc.has_value());
    auto wad = coney::io::Wad::open(std::move(*disc));
    REQUIRE(wad.has_value());

    // The story's first links: 99 -> 80 -> 87 -> the hub at checkpoint 1.
    struct Step {
        const char* level;
        const char* next;
        int checkpoint;
    };
    const Step steps[] = {{"level99", "level80", 1}, {"level80", "level87", 1}, {"level87", "level95", 1}};
    std::unique_ptr<coney::LevelScripts> scripts = startLevel(*wad, "level99", 3);
    for (const Step& step : steps) {
        INFO(step.level);
        coney::QuietBindingHost& host = scripts->host();
        CHECK_FALSE(host.missionCompleteRequested());
        // The script ends the mission with HUDLaunchMissionComplete.
        REQUIRE(scripts->scripts().call("HUDLaunchMissionComplete"));
        REQUIRE(host.missionCompleteRequested());
        REQUIRE(scripts->completeMission());
        CHECK_FALSE(host.missionCompleteRequested()); // the launch runNextMission makes itself is not a new end
        // A local: clang-tidy cannot follow a check through the host's accessor.
        const std::optional<std::string> nextLevel = host.nextLevel();
        REQUIRE(nextLevel.has_value());
        if (!nextLevel) {
            return;
        }
        CHECK(*nextLevel == step.next);
        CHECK(static_cast<int>(scripts->state().checkPoint) == step.checkpoint);
        std::printf("  story chain: %s -> %s at checkpoint %d\n", step.level, nextLevel->c_str(),
                    static_cast<int>(scripts->state().checkPoint));
        // The next level's scripts, with the progress carried over.
        auto next = std::make_unique<coney::LevelScripts>(coney::script::wadScriptSource(*wad), *nextLevel,
                                                          step.checkpoint, [](std::string_view) {});
        scripts->carryProgressTo(*next);
        (void)coney::runLevelScript(next->scripts(), next->state(), next->humans(), next->flags(), *nextLevel,
                                    &next->spawnRecords());
        scripts = std::move(next);
    }
}
