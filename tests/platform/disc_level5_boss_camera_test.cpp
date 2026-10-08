// SPDX-License-Identifier: GPL-3.0-or-later

// Checks against the player's own disc that `level5`'s boss fight (checkpoint 4) puts the boss rail camera on the
// screen (docs/research/scripting.md#level5, CamSetupRail): played as `--play-level level5 --checkpoint 4` plays it,
// headless, the scene l5_c6 skipped, then 40 s with the pad at rest; the camera is the rail camera, on its two points.
// It runs only when the environment variable CONEY_DISC names the disc and skips otherwise; it prints counts and
// positions only (LEGAL.md).
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
#include "camera/cameras.h"
#include "camera/rail_camera.h"
#include "characters/character_types.h"
#include "core/error.h"
#include "core/game_random.h"
#include "core/game_timer.h"
#include "core/input_script.h"
#include "fileio/disc.h"
#include "fileio/executable.h"
#include "fileio/wad.h"
#include "gamemodes/game_mode_stack.h"
#include "gamemodes/gameplay_mode.h"
#include "gamemodes/level_start.h"
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

TEST_CASE("the disc's level5 boss fight shows the boss rail camera", "[disc][story][camera]") {
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

    // The scripts as --play-level runs them at checkpoint 4, with the game's random table.
    coney::LevelScriptOptions options;
    std::vector<std::uint32_t> table;
    if (auto words = coney::io::readExecutableWords(wad->disc(), coney::GameRandom::kExecutableName,
                                                    coney::GameRandom::kTableAddress, coney::GameRandom::kTableSize)) {
        table = std::move(*words);
        options.randomTable = table;
    }
    coney::LevelScripts scripts(coney::script::wadScriptSource(*wad), "level5", 4, print, options);
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
    gameplay.setLevel("level5");
    gameplay.setSceneMaker([&wad, &sceneList] {
        return std::make_unique<coney::scenes::SceneSystem>(*sceneList, coney::scenes::wadSceneSource(*wad),
                                                            coney::scenes::SceneSystem::ScriptCall{});
    });

    // The opening scene skipped (START, then cross on the skip prompt); then the pad at rest.
    auto input = coney::parseInputScript("300 tap start\n310 tap cross\n");
    REQUIRE(input.has_value());
    coney::ScriptedInput pad(std::move(*input));
    coney::GameModeStack stack;
    stack.setInput(&pad);
    stack.push(gameplay);
    coney::GameTimer timer;
    timer.setFixedStep(true);
    stack.runUntilEmpty(timer, {}, 1500);
    REQUIRE(dynamic_cast<coney::platform::PlayLevelMode*>(gameplay.level()) != nullptr);

    const coney::camera::Cameras* cameras = gameplay.cameras();
    REQUIRE(cameras != nullptr);
    const coney::camera::RailCamera* rail = cameras->rail();
    REQUIRE(rail != nullptr);
    std::printf("  level5 boss camera: kind %d, rail points %zu, at (%.2f, %.2f, %.2f)\n",
                static_cast<int>(cameras->current().kind), rail->pointCount(), rail->position().x, rail->position().y,
                rail->position().z);
    CHECK(scripts.scripts().errors() == 0);
    CHECK(cameras->current().kind == coney::camera::CameraKind::Rail);
    CHECK(rail->pointCount() == 2);
}
