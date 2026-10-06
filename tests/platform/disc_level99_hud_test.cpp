// SPDX-License-Identifier: GPL-3.0-or-later

// Checks against the player's own disc that the first mission's HUD comes back after its intro: `level99` at
// checkpoint 1 plays as `--play-level level99 --checkpoint 1` plays it, headless, through the intro scene `l99_c1`
// (which `SuperRunScene` starts with `HideHud`). Once the scene has ended and its letterbox is out the HUD is shown
// and drawn and the first tutorial hint is up, and a pause hides the HUD and shows it again
// (docs/research/hud.md#who-shows-the-hud-again). It runs only when the environment variable CONEY_DISC names the
// disc and skips otherwise; it prints counts only (LEGAL.md).

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

TEST_CASE("the disc's level99 shows its HUD and first tutorial hint once the intro scene ends", "[disc][story][hud]") {
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

    // The pad at rest throughout: the intro is not skipped.
    auto input = coney::parseInputScript("");
    REQUIRE(input.has_value());
    coney::ScriptedInput pad(std::move(*input));
    coney::GameModeStack stack;
    stack.setInput(&pad);
    stack.push(gameplay);
    coney::GameTimer timer;
    timer.setFixedStep(true);

    // 10 s in, the intro plays letterboxed: the HUD is hidden and draws nothing.
    stack.runUntilEmpty(timer, {}, 300);
    const auto* play = dynamic_cast<const coney::platform::PlayLevelMode*>(gameplay.level());
    if (play == nullptr) {
        for (const std::string& line : log) {
            UNSCOPED_INFO(line);
        }
    }
    REQUIRE(play != nullptr);
    CHECK_FALSE(scripts.hud().visible());
    CHECK(play->hudSprites() == 0);
    const std::size_t spritesDuring = play->hudSprites();
    // A pause during the scene (the mode over play suspended, then resumed) leaves the HUD hidden: player 1 is in it.
    gameplay.suspend();
    gameplay.resume();
    CHECK_FALSE(scripts.hud().visible());

    // 2,026 frames of scene, then the letterbox's 1.5 s out: by 2,400 steps the HUD is back with the first hint.
    stack.runUntilEmpty(timer, {}, 2100);
    coney::hud::Hud& hud = scripts.hud();
    CHECK(scripts.scripts().errors() == 0);
    CHECK(hud.visible());
    CHECK(hud.hints().showing().has_value());
    CHECK_FALSE(hud.hintsHidden());
    CHECK(play->hudSprites() > 0);
    // A pause now hides the HUD under the menu and closing it shows the HUD again, even after a script's HideHud.
    scripts.hud().hideAll();
    gameplay.suspend();
    CHECK_FALSE(hud.visible());
    gameplay.resume();
    CHECK(hud.visible());
    std::printf("  level99 checkpoint 1: HUD sprites %zu during the intro, %zu after it; hint showing %d, queued %zu\n",
                spritesDuring, play->hudSprites(), hud.hints().showing().has_value() ? 1 : 0,
                hud.hints().queued().size());
}
