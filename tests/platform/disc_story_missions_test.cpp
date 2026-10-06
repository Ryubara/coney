// SPDX-License-Identifier: GPL-3.0-or-later

// Checks against the player's own disc that the story's second and third missions (`level80`, `level87`) play as
// `--play-level NAME --checkpoint 1` plays them: the level's scripts run for a while in gameplay over the play mode,
// headless, with no script error, and player 1 stands under the pad's control, moving when the stick is pushed. They
// run only when the environment variable CONEY_DISC names the disc and skip otherwise; they print counts only
// (LEGAL.md).

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
#include "human/player.h"
#include "platform/play_level_mode.h"
#include "platform/render_engine.h"
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

// What one level's run found.
struct MissionRun {
    std::uint64_t scriptErrors = 0;
    std::size_t missingBindings = 0;
    bool loaded = false;
    bool standing = false;
    float travelled = 0.0F;
    std::size_t humans = 0;
};

// Plays `level` at checkpoint 1 as `--play-level` does (the preloads, the level's script, gameplay over the play mode
// with the scripts' AI and character configuration) for 20 s, the stick pushed forward for the last 2 s.
MissionRun playMission(const coney::io::Wad& wad, std::string_view level) {
    MissionRun run;
    auto engine = coney::platform::RenderEngine::start(coney::platform::RenderBackend::Null, {});
    REQUIRE(engine.has_value());
    if (!engine) {
        return run;
    }
    coney::platform::RenderEngine& renderer = **engine;
    coney::world::SectorBudget budget(coney::world::kSectorPoolSize);
    std::vector<std::string> log;
    const auto print = [&log](std::string_view line) { log.emplace_back(line); };

    // The scripts as the story reaches the level, with the game's random table.
    coney::LevelScriptOptions options;
    std::vector<std::uint32_t> table;
    if (auto words = coney::io::readExecutableWords(wad.disc(), coney::GameRandom::kExecutableName,
                                                    coney::GameRandom::kTableAddress, coney::GameRandom::kTableSize)) {
        table = std::move(*words);
        options.randomTable = table;
    }
    coney::LevelScripts scripts(coney::script::wadScriptSource(wad), level, 1, print, options);
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
            const std::array<float, 3> p =
                player.teleported ? player.teleported->position : player.position.value_or(std::array<float, 3>{});
            playerStart = coney::human::PlayerStart{
                .position = coney::anim::Vec3{p[0], p[1], p[2]},
                .headingDegrees = player.teleported ? player.teleported->headingDegrees : player.headingDegrees};
            setup.model = player.model.empty() ? std::string(coney::human::kPlayerModel) : player.model;
            setup.type = player.type;
            setup.snapToGround = !player.teleported;
        }
        auto mode = coney::platform::PlayLevelMode::create(renderer, wad, start.level, budget, print, playerStart,
                                                           setup, &cast);
        if (!mode) {
            return std::unexpected(std::move(mode.error()));
        }
        return std::unique_ptr<coney::GameMode>(std::move(*mode));
    };
    coney::GameplayMode gameplay(renderer, scripts.scripts(), scripts.context(), scripts.state(), scripts.humans(),
                                 scripts.flags(), scripts.recorded(), std::move(loader), print);
    gameplay.setLevel(std::string(level));

    // 18 s with the pad at rest, then the stick 70 % forward for 2 s.
    auto input = coney::parseInputScript("540 stick left 0 70\n");
    REQUIRE(input.has_value());
    coney::ScriptedInput pad(std::move(*input));
    coney::GameModeStack stack;
    stack.setInput(&pad);
    stack.push(gameplay);
    coney::GameTimer timer;
    timer.setFixedStep(true);
    stack.runUntilEmpty(timer, {}, 540);
    const auto* play = dynamic_cast<const coney::platform::PlayLevelMode*>(gameplay.level());
    run.loaded = play != nullptr;
    if (play == nullptr) {
        for (const std::string& line : log) {
            UNSCOPED_INFO(line);
        }
        return run;
    }
    const float before = play->stats().travelled;
    stack.runUntilEmpty(timer, {}, 60);
    run.travelled = play->stats().travelled - before;
    const coney::human::Human& human = play->player().human();
    run.standing = !human.airborne() && !human.fighter().health().depleted();
    run.scriptErrors = scripts.scripts().errors();
    run.humans = scripts.humans().all().size();
    for (const std::string& line : log) {
        if (line.find("is not a binding Coney has") != std::string::npos) {
            UNSCOPED_INFO(line);
            ++run.missingBindings;
        }
    }
    return run;
}

} // namespace

TEST_CASE("the disc's level80 and level87 play their first checkpoint without a script error", "[disc][story]") {
    std::optional<coney::io::Wad> wad = openDisc();
    if (!wad) {
        SKIP("CONEY_DISC is not set: no disc to check");
    }
    for (const std::string_view level : {std::string_view("level80"), std::string_view("level87")}) {
        INFO(level);
        const MissionRun run = playMission(*wad, level);
        REQUIRE(run.loaded);
        CHECK(run.scriptErrors == 0);
        CHECK(run.standing);
        CHECK(run.travelled > 1.0F);
        std::printf("  %.*s: %zu humans created, %llu script errors, %zu missing bindings, %.1f m walked\n",
                    static_cast<int>(level.size()), level.data(), run.humans,
                    static_cast<unsigned long long>(run.scriptErrors), run.missingBindings, run.travelled);
    }
}
