// SPDX-License-Identifier: GPL-3.0-or-later

// Checks against the player's own disc that player 1 climbs a parked car through its shell in the level's collision
// mesh, as the original does (docs/research/characters.md#car-shells, #climb): `level99` checkpoint 2 played as
// `--play-level level99 --checkpoint 2` plays it, headless, its scene skipped; player 1 is put north of the sedan the
// script parks at (57.5, 37.9), walks south into its side with the left stick at 60 % and presses triangle, and ends
// standing on its roof. It runs only when the environment variable CONEY_DISC names the disc and skips otherwise; it
// prints counts and positions only (LEGAL.md).
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <expected>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <SDL3/SDL_stdinc.h>
#include <catch2/catch_test_macros.hpp>

#include "ai/ai_config.h"
#include "characters/character_types.h"
#include "core/error.h"
#include "core/game_random.h"
#include "core/game_timer.h"
#include "core/input_script.h"
#include "debug/play_controls.h"
#include "fileio/disc.h"
#include "fileio/executable.h"
#include "fileio/wad.h"
#include "gamemodes/game_mode_stack.h"
#include "gamemodes/gameplay_mode.h"
#include "gamemodes/level_start.h"
#include "human/human.h"
#include "human/player.h"
#include "platform/play_level_mode.h"
#include "platform/render_engine.h"
#include "scenes/scene_disc.h"
#include "scenes/scene_list.h"
#include "scenes/scene_player.h"
#include "scripting/config_strings.h"
#include "scripting/script_system.h"
#include "world/sector_budget.h"

namespace {

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

TEST_CASE("the disc's level99 street: player 1 climbs onto a parked car through its shell",
          "[disc][story][traversal]") {
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

    // The scripts as --play-level runs them at checkpoint 2, with the game's random table.
    coney::LevelScriptOptions options;
    std::vector<std::uint32_t> table;
    if (auto words = coney::io::readExecutableWords(wad->disc(), coney::GameRandom::kExecutableName,
                                                    coney::GameRandom::kTableAddress, coney::GameRandom::kTableSize)) {
        table = std::move(*words);
        options.randomTable = table;
    }
    coney::LevelScripts scripts(coney::script::wadScriptSource(*wad), "level99", 2, print, options);
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

    // The checkpoint's scene skipped (START, then cross on the skip prompt); then the left stick at 60 % up from 520,
    // and triangle once he stands at the car's side.
    auto input = coney::parseInputScript("300 tap start\n310 tap cross\n520 stick left 0 60\n600 tap triangle\n"
                                         "640 stick left 0 0\n");
    REQUIRE(input.has_value());
    coney::ScriptedInput pad(std::move(*input));
    coney::GameModeStack stack;
    stack.setInput(&pad);
    stack.push(gameplay);
    coney::GameTimer timer;
    timer.setFixedStep(true);
    stack.runUntilEmpty(timer, {}, 500);
    auto* play = dynamic_cast<coney::platform::PlayLevelMode*>(gameplay.level());
    REQUIRE(play != nullptr);

    // The sedan the script parks east of the start, from gameplay's car lines.
    const bool parked = std::ranges::any_of(
        log, [](const std::string& line) { return line.starts_with("gameplay: car car_osedan at (57.51, 37.86"); });
    REQUIRE(parked);

    // Player 1 1.5 m north of the car's side (y 38.96), facing south (the camera turns behind him), then walking south.
    play->teleport(coney::debug::Place{.name = "car", .feet = {57.5F, 40.5F, 0.3F}, .headingDegrees = 180.0F});
    bool climbed = false;
    float highest = 0.0F;
    for (int step = 500; step < 700; ++step) {
        stack.runUntilEmpty(timer, {}, 1);
        const coney::human::Human& human = play->player().human();
        climbed = climbed || human.traversal() == coney::human::Traversal::Climbing;
        highest = std::max(highest, human.position().z);
    }
    const coney::human::Human& human = play->player().human();
    std::printf("  level99 car: climbed %d, highest feet %.2f m, last at (%.2f, %.2f, %.2f) %s\n", climbed ? 1 : 0,
                highest, human.position().x, human.position().y, human.position().z,
                coney::human::traversalName(human.traversal()));
    CHECK(scripts.scripts().errors() == 0);
    CHECK(climbed);
    // On the roof (1.71 m), standing.
    CHECK(human.position().z > 1.5F);
    CHECK(human.traversal() == coney::human::Traversal::None);
}
