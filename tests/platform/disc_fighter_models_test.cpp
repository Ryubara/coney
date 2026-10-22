// SPDX-License-Identifier: GPL-3.0-or-later

// Checks against the player's own disc: the AI fighters page's model picker in the sandbox's fight yard
// (assets/sandbox/combat.layout), with level99's configuration as `--play-level sandbox:combat` takes it. Fighters are
// spawned as models the configuration names, one click's worth at random, and play goes on; a change of the player's
// character makes them again. Headless, on the fixed step with no clock. It runs only when the environment variable
// CONEY_DISC names the disc and skips otherwise; it prints counts only (LEGAL.md).

#include <cstddef>
#include <cstdio>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <SDL3/SDL_stdinc.h>
#include <catch2/catch_test_macros.hpp>

#include "ai/ai_config.h"
#include "characters/character_types.h"
#include "core/game_timer.h"
#include "debug/fighter_spawn.h"
#include "debug/play_controls.h"
#include "fileio/disc.h"
#include "fileio/wad.h"
#include "gamemodes/game_mode_stack.h"
#include "gamemodes/level_start.h"
#include "platform/play_level_mode.h"
#include "platform/render_engine.h"
#include "platform/sandbox_play_scenery.h"
#include "sandbox/sandbox_world.h"
#include "scripting/config_strings.h"
#include "support/test_paths.h"

TEST_CASE("fighters spawn as the models the picker offers, and keep them through a change of character",
          "[disc][debug][fighters]") {
    const char* discPath = SDL_getenv("CONEY_DISC");
    if (discPath == nullptr || *discPath == '\0') {
        SKIP("CONEY_DISC is not set: no disc to check");
    }
    auto disc = coney::io::Disc::open(discPath);
    REQUIRE(disc.has_value());
    auto wad = coney::io::Wad::open(std::move(*disc));
    REQUIRE(wad.has_value());
    auto engine = coney::platform::RenderEngine::start(coney::platform::RenderBackend::Null, {});
    REQUIRE(engine.has_value());

    // The sandbox's setup as main makes it: level99's configuration.
    const coney::LevelScriptRun run =
        coney::runLevelScriptAlone(coney::script::wadScriptSource(*wad), "level99", 1, {});
    coney::platform::PlayerSetup setup;
    setup.ai = coney::ai::aiConfigFrom(run.recorded);
    setup.types = coney::characters::CharacterTypes::fromRecorded(run.recorded);
    auto world = coney::sandbox::SandboxWorld::load(coney::test::assetsDir() / "sandbox", "combat");
    REQUIRE(world.has_value());
    auto made = coney::platform::createPlayInSandbox(
        **engine, *wad, std::move(*world), std::nullopt, [](std::string_view) {}, setup);
    REQUIRE(made.has_value());
    coney::platform::PlayLevelMode& mode = **made;

    // The picker's list: the distinct models of the types a player can be.
    const std::vector<std::string> models = mode.fighterModels();
    std::printf("fighter models: %zu distinct models offered\n", models.size());
    REQUIRE(models.size() > 20);

    coney::GameModeStack modes;
    coney::GameTimer timer;
    timer.setFixedStep(true);
    modes.push(mode);
    (void)modes.runUntilEmpty(timer, {}, 10);

    // One click's worth at random: six fighters, each its own draw, in rows ahead of the player.
    const std::size_t before = mode.fighterCount();
    coney::debug::SpawnRandom random(0x5eed5eedU);
    const std::vector<std::string> picks = coney::debug::pickFighterModels(models, 0, 6, random);
    const std::vector<coney::anim::Vec3> spots =
        coney::debug::fighterSpots(mode.playerFeet(), mode.playerHeadingDegrees(), 4.0F, 6);
    REQUIRE(picks.size() == 6);
    REQUIRE(spots.size() == 6);
    std::size_t spawned = 0;
    for (std::size_t i = 0; i < spots.size(); ++i) {
        if (mode.spawnFighterAs(spots[i], mode.playerHeadingDegrees() + 180.0F, picks[i])) {
            ++spawned;
        }
    }
    CHECK(spawned == 6);
    CHECK(mode.fighterCount() == before + 6);
    // A chosen model, and the scene's own (empty) one.
    CHECK(mode.spawnFighterAs(spots[0], 0.0F, models.front()));
    CHECK(mode.spawnFighterAs(spots[1], 0.0F, {}));
    CHECK(mode.fighterCount() == before + 8);
    std::printf("fighter models: %zu fighters spawned, %zu in the scene\n", spawned + 2, mode.fighterCount());

    // They play on, and a change of the player's character makes them again as the models they had.
    (void)modes.runUntilEmpty(timer, {}, 15);
    REQUIRE(mode.changeCharacter(40));
    CHECK(mode.fighterCount() == before + 8);
    (void)modes.runUntilEmpty(timer, {}, 15);
}
