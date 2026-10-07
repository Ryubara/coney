// SPDX-License-Identifier: GPL-3.0-or-later

// Checks against the player's own disc that the debug menus' Change character in a level whose scripts drive the cast
// (`level99`, as `--play-level level99 --checkpoint 1` plays it, headless) makes player 1 the chosen type as a player
// made as that type is: the same anim set's speeds, the class's damage on his square, his power class and full health;
// and that the scripts and his brain see the new type (HuGetCharType, his voice, his sounds). It runs only when the
// environment variable CONEY_DISC names the disc and skips otherwise; it prints counts only (LEGAL.md).

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
#include "ai/ai_humans.h"
#include "ai/brain.h"
#include "characters/character_data.h"
#include "characters/character_types.h"
#include "core/chunk_system.h"
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
#include "human/human.h"
#include "human/player.h"
#include "platform/play_level_mode.h"
#include "platform/render_engine.h"
#include "scenes/scene_disc.h"
#include "scenes/scene_list.h"
#include "scenes/scene_player.h"
#include "scripting/config_strings.h"
#include "world/sector_budget.h"

namespace {

// The pad: the intro scene skipped, then nothing.
constexpr std::string_view kPad = "0 stick left 0 0\n100 tap cross\n";
// The frame the character is changed on, the intro long skipped.
constexpr std::uint64_t kChangeAt = 300;
// The type he becomes: Ash (docs/guides/debug-menu.md#pages).
constexpr int kNewType = 40;

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

TEST_CASE("the disc's level99: Change character makes the scripts' player the type as a player made as it is",
          "[disc][story][debug]") {
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

    // The scripts as --play-level runs them, with the game's random table.
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
        auto made = std::make_unique<coney::scenes::SceneSystem>(*sceneList, coney::scenes::wadSceneSource(*wad),
                                                                 coney::scenes::SceneSystem::ScriptCall{});
        return made;
    });

    auto events = coney::parseInputScript(kPad);
    REQUIRE(events.has_value());
    coney::ScriptedInput pad(std::move(*events));
    coney::GameModeStack stack;
    stack.setInput(&pad);
    stack.push(gameplay);
    coney::GameTimer timer;
    timer.setFixedStep(true);
    const auto play = [&gameplay] { return dynamic_cast<coney::platform::PlayLevelMode*>(gameplay.level()); };
    std::uint64_t frame = 0;
    const auto step = [&] {
        stack.runUntilEmpty(timer, {}, 1);
        ++frame;
    };

    while (frame < kChangeAt) {
        step();
    }
    REQUIRE(play() != nullptr);
    coney::platform::PlayLevelMode& mode = *play();
    const coney::characters::CharacterTypes types = coney::characters::CharacterTypes::fromRecorded(scripts.recorded());
    const int startType = mode.playerType();
    const coney::HumanCreation* made = scripts.humans().player(1);
    REQUIRE(made != nullptr);
    const double handle = made->handle;

    REQUIRE(mode.changeCharacter(kNewType));
    CHECK(mode.playerType() == kNewType);
    const std::optional<std::string> model = types.modelFor(kNewType, 1, 99);
    REQUIRE(model.has_value());
    CHECK(mode.model() == *model);

    // A player made as the type from the start, from the same files and class.
    coney::chunk::ChunkHandlerTable handlers = coney::chunk::ChunkHandlerTable::withDefaults();
    coney::characters::addCharacterDataHandlers(handlers);
    auto character = coney::human::PlayerCharacter::load(*wad, handlers, *model);
    REQUIRE(character.has_value());
    const coney::human::Player fresh(**character, nullptr, coney::human::PlayerStart{},
                                     coney::human::playerClassOf(types, kNewType));
    const coney::human::Human& now = mode.player().human();
    const coney::human::Speeds speeds = coney::human::speedsOf(now.anims(), coney::human::AnimSlots::player());
    const coney::human::Speeds expected =
        coney::human::speedsOf(fresh.human().anims(), coney::human::AnimSlots::player());
    CHECK(speeds.walk == expected.walk);
    CHECK(speeds.run == expected.run);
    CHECK(speeds.sprint == expected.sprint);
    REQUIRE(now.ranges() != nullptr);
    REQUIRE(fresh.human().ranges() != nullptr);
    CHECK(now.ranges()->damage(12) == fresh.human().ranges()->damage(12));
    CHECK(now.fighterProfile().powerClass.powerMax == fresh.human().fighterProfile().powerClass.powerMax);
    CHECK(now.health().maximum() == fresh.human().health().maximum());
    CHECK(now.health().value() == now.health().maximum());
    // The scripts and his brain know him as the new type, by the same handle.
    const coney::HumanCreation* after = scripts.humans().player(1);
    REQUIRE(after != nullptr);
    CHECK(after->handle == handle);
    CHECK(after->type == kNewType);
    CHECK(mode.fighters().playerBrain().characterClass() == kNewType);
    std::printf("change character in level99: type %d to %d, square %d damage, walk %.3f m/s\n", startType, kNewType,
                now.ranges()->damage(12), static_cast<double>(speeds.walk));

    // Play goes on as the new type.
    for (int i = 0; i < 60; ++i) {
        step();
    }
    CHECK(mode.playerType() == kNewType);
}
