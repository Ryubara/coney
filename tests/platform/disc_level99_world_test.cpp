// SPDX-License-Identifier: GPL-3.0-or-later

// Checks against the player's own disc that `level99`'s world objects and health rings come up in play: checkpoint 1
// played as `--play-level level99 --checkpoint 1` plays it, headless, through the intro scene. Once the first hint is
// up, the first objective marker (`dyn_w_mission`, shown by `P1.SetupCam`) is in the world with its disc and gold
// column at full alpha (docs/research/objects.md#objective-markers); SELECT brings player 1's two health rings up
// (docs/research/hud.md#the-health-rings); and the lesson-9 bats, laid on the ground as the scene `l99_c8` leaves them,
// are drawn there (docs/research/objects.md#pickable). It runs only when the environment variable CONEY_DISC names
// the disc and skips otherwise; it prints counts only (LEGAL.md).
#include <array>
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
#include "fileio/disc.h"
#include "fileio/executable.h"
#include "fileio/wad.h"
#include "gamemodes/game_mode_stack.h"
#include "gamemodes/gameplay_mode.h"
#include "gamemodes/level_start.h"
#include "hud/health_rings.h"
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
#include "world_objects/object_tasks.h"
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

TEST_CASE("the disc's level99 draws its objective marker, the health rings and the bats on the ground",
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
    // The level's scenes over the disc's scene list, as the game makes them, so the intro plays with its letterbox.
    auto sceneList = coney::scenes::loadSceneList(*wad);
    REQUIRE(sceneList.has_value());
    coney::GameplayMode gameplay(renderer, scripts.scripts(), scripts.context(), scripts.state(), scripts.humans(),
                                 scripts.flags(), scripts.recorded(), std::move(loader), print);
    gameplay.setLevel("level99");
    gameplay.setSceneMaker([&wad, &sceneList] {
        return std::make_unique<coney::scenes::SceneSystem>(*sceneList, coney::scenes::wadSceneSource(*wad),
                                                            coney::scenes::SceneSystem::ScriptCall{});
    });

    // The pad at rest through the intro (not skipped), then SELECT.
    auto input = coney::parseInputScript("2405 tap select\n");
    REQUIRE(input.has_value());
    coney::ScriptedInput pad(std::move(*input));
    coney::GameModeStack stack;
    stack.setInput(&pad);
    stack.push(gameplay);
    coney::GameTimer timer;
    timer.setFixedStep(true);

    // The intro, its letterbox going out and the first hint: by 2,400 steps the first marker has faded in.
    stack.runUntilEmpty(timer, {}, 2400);
    const auto* play = dynamic_cast<const coney::platform::PlayLevelMode*>(gameplay.level());
    REQUIRE(play != nullptr);
    const coney::world_objects::ObjectTasks& objects = play->worldObjects();
    std::size_t markers = 0;
    std::size_t columns = 0;
    for (const coney::world_objects::ObjectDraw& draw : objects.draws()) {
        if ((draw.tint & 0xFFU) < static_cast<std::uint32_t>(coney::world_objects::kMinDrawnAlpha)) {
            continue;
        }
        markers += draw.sizeCullExempt && !draw.column && objects.marker(draw.handle) != nullptr ? 1 : 0;
        columns += draw.column && draw.tint == 0xC1A047FFU ? 1 : 0;
    }
    CHECK(scripts.scripts().errors() == 0);
    CHECK(markers == 1);
    CHECK(columns == 1);
    CHECK(play->healthRings().rings().empty()); // no trigger yet at full health

    // SELECT: the outer and inner rings under player 1.
    stack.runUntilEmpty(timer, {}, 30);
    const std::size_t rings = play->healthRings().rings().size();
    CHECK(rings == 2);

    // The three bats as `l99_c8` leaves them: bound to the scene (resolved and pinned) and laid at the player's feet.
    coney::world_objects::SpawnRecords& records = scripts.spawnRecords();
    const coney::anim::Vec3 feet = play->player().human().position();
    std::size_t placed = 0;
    for (const coney::world_objects::SpawnRecord& record : records.all()) {
        if (record.typeName != "dyn_bat_tuff") {
            continue;
        }
        coney::world_objects::SpawnRecord* bat = records.resolve(record.handle);
        REQUIRE(bat != nullptr);
        bat->pinned = true;
        bat->position = {feet.x + 1.0F + static_cast<float>(placed) * 0.3F, feet.y, feet.z};
        ++placed;
    }
    stack.runUntilEmpty(timer, {}, 40);
    std::size_t bats = 0;
    for (const coney::world_objects::ObjectDraw& draw : objects.draws()) {
        const coney::world_objects::SpawnRecord* record = records.find(draw.handle);
        bats += record != nullptr && record->typeName == "dyn_bat_tuff" && (draw.tint & 0xFFU) == 255U ? 1 : 0;
    }
    CHECK(placed == 3);
    CHECK(bats == 3);
    std::printf("  level99 checkpoint 1: %zu objects in the world, %zu drawn; marker discs %zu, gold columns %zu; "
                "rings after SELECT %zu; bats on the ground %zu\n",
                objects.count(), objects.draws().size(), markers, columns, rings, bats);
}
