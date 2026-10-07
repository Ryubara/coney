// SPDX-License-Identifier: GPL-3.0-or-later

// Checks against the player's own disc that mission 1's play-through script passes `level99`'s combat tutorial up to
// its throw lesson: the level plays as `--play-level level99 --checkpoint 1` plays it, headless, driven by the pad of
// kLessonsScript and then kCourseScript (lessons 7-12), and lessons 7-8's tutorial callbacks must arm in order (the
// snaps, the throws) with no script error (docs/research/scripting.md#level99-lessons). It runs only when the
// environment variable CONEY_DISC names the disc and skips otherwise; it prints counts and callback names only
// (LEGAL.md).
//
// **Known gap**: lessons 9-12 (the bats, the rage moves, the second wave) and checkpoint 2 are not checked. The pad
// script is frame-locked: its bat swings and the second wave land only when every bum stands where an earlier Coney AI
// left it, and the AI now takes turns at its target and turns at the AI's own rates (docs/research/ai.md#coney). The
// later lessons' checks return with an adaptive driver (walk to the nearest standing bum, then attack).

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <expected>
#include <format>
#include <memory>
#include <numbers>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <SDL3/SDL_stdinc.h>
#include <catch2/catch_test_macros.hpp>

#include "ai/ai_config.h"
#include "ai/ai_humans.h"
#include "ai/brain.h"
#include "ai/gangs.h"
#include "characters/character_types.h"
#include "combat/anim_ids.h"
#include "core/error.h"
#include "core/game_random.h"
#include "core/game_timer.h"
#include "core/input_script.h"
#include "core/pads.h"
#include "fileio/disc.h"
#include "fileio/executable.h"
#include "fileio/wad.h"
#include "gamemodes/game_mode_stack.h"
#include "gamemodes/gameplay_mode.h"
#include "gamemodes/level_start.h"
#include "hud/hud.h"
#include "human/human.h"
#include "human/locomotion.h"
#include "human/player.h"
#include "platform/play_level_mode.h"
#include "platform/render_engine.h"
#include "scenes/scene_disc.h"
#include "scenes/scene_list.h"
#include "scenes/scene_player.h"
#include "scripting/config_strings.h"
#include "scripting/lua_value.h"
#include "scripting/lua_vm.h"
#include "scripting/script_system.h"
#include "world/sector_budget.h"

#include "support/level99_lessons.h"

namespace {

using coney::test::kCourseEnd;
using coney::test::kCourseScript;
using coney::test::kLessonsScript;

// How long after the course's last move checkpoint 2 may take, frames (the scene l99_c6 plays first).
constexpr std::uint64_t kCheckpointWait = 1800;

// The disc named by CONEY_DISC, opened; nothing when it is not set.
std::optional<coney::io::Wad> openDisc() {
    const char* discPath = SDL_getenv("CONEY_DISC");
    if (discPath == nullptr || *discPath == '\0') {
        return std::nullopt;
    }
    auto disc = coney::io::Disc::open(discPath);
    REQUIRE(disc.has_value());
    auto wad = coney::io::Wad::open(std::move(*disc));
    REQUIRE(wad.has_value());
    return wad ? std::optional<coney::io::Wad>(std::move(*wad)) : std::nullopt;
}

} // namespace

TEST_CASE("the disc's level99 combat tutorial: the play-through script passes lessons 1-8", "[disc][story][combat]") {
    std::optional<coney::io::Wad> wad = openDisc();
    if (!wad) {
        SKIP("CONEY_DISC is not set: no disc to check");
    }
    auto engine = coney::platform::RenderEngine::start(coney::platform::RenderBackend::Null, {});
    REQUIRE(engine.has_value());
    coney::platform::RenderEngine& renderer = **engine;
    coney::world::SectorBudget budget(coney::world::kSectorPoolSize);
    std::vector<std::string> log;
    const auto print = [&log](std::string_view line) { log.emplace_back(line); };

    // The scripts as --play-level runs them, with the game's random table; the play mode draws the scripts' HUD.
    coney::LevelScriptOptions options;
    std::vector<std::uint32_t> table;
    if (auto words = coney::io::readExecutableWords(wad->disc(), coney::GameRandom::kExecutableName,
                                                    coney::GameRandom::kTableAddress, coney::GameRandom::kTableSize)) {
        table = std::move(*words);
        options.randomTable = table;
    }
    coney::LevelScripts scripts(coney::script::wadScriptSource(*wad), "level99", 1, print, options);
    coney::GameplayMode::LevelLoader loader =
        [&renderer, &wad, &budget, &scripts,
         &print](const coney::LevelStart& start,
                 const coney::ScriptedCast& cast) -> std::expected<std::unique_ptr<coney::GameMode>, coney::Error> {
        std::optional<coney::human::PlayerStart> playerStart;
        coney::platform::PlayerSetup setup;
        setup.ai = coney::ai::aiConfigFrom(scripts.recorded());
        setup.types = coney::characters::CharacterTypes::fromRecorded(scripts.recorded());
        if (start.player) {
            const coney::HumanCreation& player = *start.player;
            const std::array<float, 3> p = player.position.value_or(std::array<float, 3>{});
            playerStart = coney::human::PlayerStart{.position = coney::anim::Vec3{p[0], p[1], p[2]},
                                                    .headingDegrees = player.headingDegrees};
            setup.model = player.model.empty() ? std::string(coney::human::kPlayerModel) : player.model;
            setup.type = player.type;
        }
        auto mode = coney::platform::PlayLevelMode::create(renderer, *wad, start.level, budget, print, playerStart,
                                                           setup, &cast);
        if (!mode) {
            return std::unexpected(std::move(mode.error()));
        }
        (*mode)->useHud(scripts.hud());
        return std::unique_ptr<coney::GameMode>(std::move(*mode));
    };
    auto sceneList = coney::scenes::loadSceneList(*wad);
    REQUIRE(sceneList.has_value());
    coney::GameplayMode gameplay(renderer, scripts.scripts(), scripts.context(), scripts.state(), scripts.humans(),
                                 scripts.flags(), scripts.recorded(), std::move(loader), print);
    gameplay.setLevel("level99");
    gameplay.setSceneMaker([&wad, &sceneList] {
        return std::make_unique<coney::scenes::SceneSystem>(*sceneList, coney::scenes::wadSceneSource(*wad),
                                                            coney::scenes::SceneSystem::ScriptCall{});
    });

    const std::string course = std::string(kLessonsScript) + std::string(kCourseScript);
    auto events = coney::parseInputScript(course);
    REQUIRE(events.has_value());
    coney::ScriptedInput pad(std::move(*events));
    coney::GameModeStack stack;
    stack.setInput(&pad);
    stack.push(gameplay);
    coney::GameTimer timer;
    timer.setFixedStep(true);
    coney::hud::Hud& hud = scripts.hud();

    // Each tutorial callback the lessons arm, in order, until checkpoint 2.
    std::vector<std::string> armed;
    std::uint64_t frame = 0;
    for (; frame < kCourseEnd + kCheckpointWait && (armed.empty() || armed.back() != "P1.RageMoves"); ++frame) {
        stack.runUntilEmpty(timer, {}, 1);
        if (const std::string& callback = hud.tutorialCallback();
            !callback.empty() && (armed.empty() || armed.back() != callback)) {
            armed.push_back(callback);
        }
    }
    std::printf("  level99 course: checkpoint %g at frame %llu, %zu callbacks armed\n", scripts.state().checkPoint,
                static_cast<unsigned long long>(frame), armed.size());
    for (const std::string& callback : armed) {
        std::printf("    armed %s\n", callback.c_str());
    }
    if (armed.empty() || armed.back() != "P1.RageMoves") {
        for (const std::string& line : log) {
            UNSCOPED_INFO(line);
        }
    }
    // Lessons 7-8's callbacks, in order (lessons 9-10 are the known gap above).
    const std::array<std::string_view, 2> later{"P1.Snaps", "P1.Throws"};
    auto at = armed.begin();
    for (const std::string_view callback : later) {
        at = std::find(at, armed.end(), callback);
        CHECK(at != armed.end());
    }
    CHECK(scripts.scripts().errors() == 0);
}
