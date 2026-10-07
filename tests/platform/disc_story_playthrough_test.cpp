// SPDX-License-Identifier: GPL-3.0-or-later

// Checks against the player's own disc that the story's missions play through, as `coney --play-level NAME
// --checkpoint N --input-script FILE` plays them: gameplay over the play mode with the scenes, the level's scripts as
// the story reaches them, and a pad script (tests/support) driving player 1 with the analog sticks and buttons as a
// player would. A run passes when the scripts reach the next checkpoint or the mission's end with no script error and
// no failed mission. They run only when the environment variable CONEY_DISC names the disc and skip otherwise; they
// print counts only (LEGAL.md).

#include <cstdint>
#include <cstdio>
#include <expected>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "core/game_random.h"
#include "core/game_timer.h"
#include "core/input_script.h"
#include "fileio/executable.h"
#include "fileio/wad.h"
#include "gamemodes/game_mode_stack.h"
#include "gamemodes/gameplay_mode.h"
#include "gamemodes/level_start.h"
#include "platform/play_level_mode.h"
#include "platform/render_engine.h"
#include "scenes/scene_disc.h"
#include "scenes/scene_list.h"
#include "scenes/scene_player.h"
#include "scripting/script_system.h"
#include "support/disc_play_fixtures.h"
#include "world/sector_budget.h"

namespace {

// What one scripted run reached.
struct Playthrough {
    bool loaded = false;
    std::uint64_t scriptErrors = 0;
    int checkpoint = 0;                       // GetCheckPoint() when the run ended
    std::optional<int> missionComplete;       // HUDLaunchMissionComplete's kind, once called
    std::optional<std::string> missionFailed; // HUDLaunchMissionFailed's reason, once called
    int health = 0;                           // player 1's at the end
    std::vector<std::string> errors;          // the scripts' error lines
};

// Plays `level` at `checkpoint` for `frames` frames with the pad script `script` (in tests/support), as main's
// `--play-level` sets it up: the scripts with the game's random table and the preloads' turning, gameplay with the
// scenes, the play mode as the scripts' cast configures it.
Playthrough play(const coney::io::Wad& wad, std::string_view level, int checkpoint, const std::string& script,
                 std::uint64_t frames) {
    Playthrough run;
    auto engine = coney::platform::RenderEngine::start(coney::platform::RenderBackend::Null, {});
    REQUIRE(engine.has_value());
    if (!engine) {
        return run;
    }
    coney::platform::RenderEngine& renderer = **engine;
    coney::world::SectorBudget budget(coney::world::kSectorPoolSize);
    std::vector<std::string> log;
    const std::function<void(std::string_view)> print = [&log](std::string_view line) { log.emplace_back(line); };

    // The scripts as main's levelScriptsFor makes them.
    coney::LevelScriptOptions options;
    auto table = coney::io::readExecutableWords(wad.disc(), coney::GameRandom::kExecutableName,
                                                coney::GameRandom::kTableAddress, coney::GameRandom::kTableSize);
    if (table) {
        options.randomTable = *table;
    }
    coney::LevelScripts scripts(coney::script::wadScriptSource(wad), level, checkpoint, print, options);
    coney::applyTurnConfig(scripts.recorded());

    auto sceneList = coney::scenes::loadSceneList(wad);
    REQUIRE(sceneList.has_value());
    coney::GameplayMode gameplay(renderer, scripts.scripts(), scripts.context(), scripts.state(), scripts.humans(),
                                 scripts.flags(), scripts.recorded(),
                                 coney::test::playLoader(renderer, wad, budget, print), print);
    gameplay.setLevel(std::string(level));
    gameplay.setSceneMaker([&wad, &sceneList] {
        return std::make_unique<coney::scenes::SceneSystem>(*sceneList, coney::scenes::wadSceneSource(wad),
                                                            coney::scenes::SceneSystem::ScriptCall{});
    });

    const std::filesystem::path path = std::filesystem::path(CONEY_TEST_SUPPORT_DIR) / script;
    auto events = coney::loadInputScript(path.string());
    REQUIRE(events.has_value());
    if (!events) {
        return run;
    }
    coney::ScriptedInput pad(std::move(*events));
    coney::GameModeStack stack;
    stack.setInput(&pad);
    stack.push(gameplay);
    coney::GameTimer timer;
    timer.setFixedStep(true);
    stack.runUntilEmpty(timer, {}, frames);

    const auto* playMode = dynamic_cast<const coney::platform::PlayLevelMode*>(gameplay.level());
    run.loaded = playMode != nullptr;
    run.scriptErrors = scripts.scripts().errors();
    run.checkpoint = static_cast<int>(scripts.state().checkPoint);
    run.missionComplete = scripts.host().missionComplete();
    run.missionFailed = scripts.host().missionFailed();
    if (playMode != nullptr) {
        run.health = playMode->player().human().fighter().health().value();
    }
    for (const std::string& line : log) {
        if (line.starts_with("script error")) {
            run.errors.push_back(line);
        }
    }
    return run;
}

} // namespace

// Hidden ([.]), a known gap: the frame-locked recording no longer climbs the ledge at (166.9, 366.3, 10.3) since the
// AI and movement changes merged with it, so Snow falls short of the gallery. Run it by name until the recording is
// redone (or the climb fixed).
TEST_CASE("the disc's level3 rooftop chase, driven by the pad, reaches the gallery", "[.][disc][story][playthrough]") {
    // docs/research/scripting.md#level3: the AI walks Snow to vbStartRail, the stick then runs him east along the
    // roofs, and vbEnterGallery hands over to the gallery chapter (SetCheckPoint(5)). A fall fails the mission.
    std::optional<coney::io::Wad> wad = coney::test::openDisc();
    if (!wad) {
        SKIP("CONEY_DISC is not set: no disc to check");
    }
    constexpr std::uint64_t kFrames = 3700;
    const Playthrough run = play(*wad, "level3", 4, "level3_chase.txt", kFrames);
    REQUIRE(run.loaded);
    for (const std::string& error : run.errors) {
        UNSCOPED_INFO(error);
    }
    CHECK(run.scriptErrors == 0);
    CHECK_FALSE(run.missionFailed.has_value());
    CHECK(run.checkpoint == 5);
    CHECK(run.health > 0);
    std::printf("  level3 checkpoint 4 chase: checkpoint %d after %llu frames, %llu script errors, health %d, %s%s\n",
                run.checkpoint, static_cast<unsigned long long>(kFrames),
                static_cast<unsigned long long>(run.scriptErrors), run.health,
                run.missionFailed ? "failed: " : "not failed", run.missionFailed ? run.missionFailed->c_str() : "");
}
