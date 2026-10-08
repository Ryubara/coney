// SPDX-License-Identifier: GPL-3.0-or-later

// Checks against the player's own disc that an object attack breaks a pane when the attack reaches it, not as it
// starts: `level99` checkpoint 2 played as `--play-level level99 --checkpoint 2` plays it, headless, its scene
// skipped; player 1 is put in front of one of the store's cabinet fronts and presses square, which plays the object
// attack at the pane (docs/research/combat.md#breakables); the pane breaks when the clip's strike shapes reach it
// (`Strike_Contact`, docs/research/objects.md#coneys-implementation), several updates after the press. It runs only
// when the environment variable CONEY_DISC names the disc and skips otherwise; it prints counts only (LEGAL.md).
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <expected>
#include <format>
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
#include "hud/hud.h"
#include "human/player.h"
#include "platform/play_level_mode.h"
#include "platform/render_engine.h"
#include "scenes/scene_disc.h"
#include "scenes/scene_list.h"
#include "scenes/scene_player.h"
#include "scripting/config_strings.h"
#include "scripting/script_system.h"
#include "world/sector_budget.h"
#include "world_objects/glass.h"
#include "world_objects/level_objects.h"

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

TEST_CASE("the disc's level99 store: square at a cabinet front breaks the pane when the attack reaches it",
          "[disc][story][objects]") {
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

    // The checkpoint's scene skipped (START, then cross on the skip prompt); square at the cabinet.
    constexpr int kSquareFrame = 520;
    auto input = coney::parseInputScript("300 tap start\n310 tap cross\n520 tap square\n");
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

    // The front pane of the watches' cabinet (type 1, below the top at z 2.02) nearest to (51.25, 56.99).
    const coney::world_objects::GlassPanes& glass = gameplay.objects().glass;
    const coney::world_objects::GlassPane* front = nullptr;
    float best = 3.0F;
    for (const coney::world_objects::GlassPane& pane : glass.panes()) {
        const float away = std::hypot(pane.centre.x - 51.25F, pane.centre.y - 56.99F);
        if (pane.type == 1 && pane.centre.z < 2.0F && away < best) {
            best = away;
            front = &pane;
        }
    }
    REQUIRE(front != nullptr);
    const double handle = front->handle;
    // Player 1 just south of it, facing it (+y), within the object attack's reach.
    play->teleport(coney::debug::Place{
        .name = "cabinet", .feet = {front->centre.x, front->centre.y - 0.9F, 0.3F}, .headingDegrees = 0.0F});
    stack.runUntilEmpty(timer, {}, kSquareFrame - 500);

    // Update by update from the press's (counted 0) until the pane breaks.
    int brokeAfter = -1;
    for (int step = 0; step < 60 && brokeAfter < 0; ++step) {
        stack.runUntilEmpty(timer, {}, 1);
        const coney::world_objects::GlassPane* pane = glass.find(handle);
        if (pane != nullptr && pane->broken) {
            brokeAfter = step;
        }
    }
    // Which way the hit came: the strike shapes' contact or the object attack's own.
    const std::string byShapes = std::format("objects: {:.0f} struck by clip", handle);
    const bool struck =
        std::ranges::any_of(log, [&byShapes](const std::string& line) { return line.starts_with(byShapes); });
    std::printf("  level99 store: cabinet pane broken %d updates after square, %s\n", brokeAfter,
                struck ? "by the strike shapes" : "by the attack");
    CHECK(scripts.scripts().errors() == 0);
    // Not as the attack starts (the dispatcher's hit update, 2) but when its strike shapes reach the pane: 12 updates
    // after the press, as at runtime (docs/research/combat.md#breakables).
    CHECK(struck);
    CHECK(brokeAfter == 12);
}
