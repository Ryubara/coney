// SPDX-License-Identifier: GPL-3.0-or-later

// Checks against the player's own disc that `level99`'s street props are solid: checkpoint 2 played as
// `--play-level level99 --checkpoint 2` plays it, headless, its scene skipped; player 1 is put south of a
// `dyn_trashcan` and walks north into it with the left stick at 60 %, and his walking body slides along the can's
// `BLOCKHUMANS` box instead of passing through it (docs/research/physics.md#bodies, #layers). It runs only when the
// environment variable CONEY_DISC names the disc and skips otherwise; it prints counts and positions only (LEGAL.md).
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
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
#include "hud/hud.h"
#include "human/body.h"
#include "human/player.h"
#include "platform/play_level_mode.h"
#include "platform/render_engine.h"
#include "scenes/scene_disc.h"
#include "scenes/scene_list.h"
#include "scenes/scene_player.h"
#include "scripting/config_strings.h"
#include "scripting/script_system.h"
#include "world/sector_budget.h"
#include "world_objects/object_bodies.h"
#include "world_objects/object_tasks.h"
#include "world_objects/object_types.h"
#include "world_objects/spawn_records.h"

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

TEST_CASE("the disc's level99 street: a trash can stops player 1 walking into it", "[disc][story][objects]") {
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

    // The checkpoint's scene skipped (START, then cross on the skip prompt); then the left stick at 60 % up for 3 s.
    auto input = coney::parseInputScript("300 tap start\n310 tap cross\n520 stick left 0 60\n610 stick left 0 0\n");
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

    // The trash can in the world nearest player 1, and its body.
    const coney::anim::Vec3 feet = play->player().human().position();
    const coney::world_objects::SpawnRecord* can = nullptr;
    float best = 1e9F;
    for (const coney::world_objects::SpawnRecord& record : scripts.spawnRecords().all()) {
        const float away = std::hypot(record.position[0] - feet.x, record.position[1] - feet.y);
        if (record.typeName == "dyn_trashcan" && !record.removed && play->worldObjects().inWorld(record.handle) &&
            away < best) {
            best = away;
            can = &record;
        }
    }
    REQUIRE(can != nullptr);
    const coney::world_objects::ObjectType* type = scripts.objectTypes().find(can->typeName);
    REQUIRE(type != nullptr);
    const std::optional<coney::world_objects::ObjectBody> body = coney::world_objects::bodyOf(*can, *type);
    REQUIRE(body.has_value());
    CHECK((type->bodyWord & coney::world_objects::kPhyBlockHumans) != 0);

    // Player 1 2 m south of it, facing north (the camera turns behind him), then walking north.
    play->teleport(coney::debug::Place{
        .name = "can", .feet = {can->position[0], can->position[1] - 2.0F, can->position[2]}, .headingDegrees = 0.0F});
    float nearest = 1e9F;
    float deepest = 0.0F;
    for (int step = 500; step < 640; ++step) {
        stack.runUntilEmpty(timer, {}, 1);
        const coney::human::Human& human = play->player().human();
        const float radius = coney::human::playerWalkingRadius(human.scale());
        const coney::anim::Vec3 at = human.position();
        const coney::anim::Vec3 centre{at.x, at.y, at.z + coney::human::playerWalkingCentreHeight(human.scale())};
        nearest = std::min(nearest, std::hypot(centre.x - body->pose.t.x, centre.y - body->pose.t.y));
        if (const std::optional<coney::anim::Vec3> push = coney::world_objects::spherePush(*body, centre, radius)) {
            deepest = std::max(deepest, coney::anim::length(*push));
        }
    }
    std::printf("  level99 street: trash can layers %#x, nearest approach %.2f m, deepest overlap %.3f m\n",
                static_cast<unsigned>(type->bodyWord), nearest, deepest);
    CHECK(scripts.scripts().errors() == 0);
    // He reached the can (his sphere within its reach) but never went into it.
    CHECK(nearest < 1.0F);
    CHECK(deepest < 0.05F);
}
