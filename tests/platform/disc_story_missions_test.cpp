// SPDX-License-Identifier: GPL-3.0-or-later

// Checks against the player's own disc that the story's second, third, fourth and seventh missions (`level80`,
// `level87`, `level34`, `level5`) play as `--play-level NAME --checkpoint N` plays them, at every checkpoint: the
// level's scripts run for a while in gameplay over the play mode, headless, with no script error, and player 1 stands
// under the pad's control, moving when the stick is pushed. They run only when the environment variable CONEY_DISC
// names the disc and skip otherwise; they print counts only (LEGAL.md).

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <expected>
#include <format>
#include <functional>
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
#include "platform/scene_stage.h"
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

// What one level's run found.
struct MissionRun {
    std::uint64_t scriptErrors = 0;
    std::size_t missingBindings = 0;
    bool loaded = false;
    bool standing = false;
    float travelled = 0.0F;
    std::size_t humans = 0;
    std::vector<std::string> errors; // the scripts' error lines
};

// The game's random table from the disc's executable; empty when it cannot be read.
std::vector<std::uint32_t> randomTable(const coney::io::Wad& wad) {
    auto words = coney::io::readExecutableWords(wad.disc(), coney::GameRandom::kExecutableName,
                                                coney::GameRandom::kTableAddress, coney::GameRandom::kTableSize);
    return words ? std::move(*words) : std::vector<std::uint32_t>{};
}

// The level loader `--play-level` gives gameplay: the play mode at the scripts' player start, with the scripts' AI and
// character configuration.
coney::GameplayMode::LevelLoader playLoader(coney::platform::RenderEngine& renderer, const coney::io::Wad& wad,
                                            coney::world::SectorBudget& budget, coney::LevelScripts& scripts,
                                            const std::function<void(std::string_view)>& print) {
    return [&renderer, &wad, &budget, &scripts,
            print](const coney::LevelStart& start,
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
}

// Plays `level` at `checkpoint` as `--play-level` does (the preloads, the level's script, gameplay over the play mode
// with the scripts' AI and character configuration) for 20 s, the stick pushed forward for the last 2 s.
MissionRun playMission(const coney::io::Wad& wad, std::string_view level, int checkpoint) {
    MissionRun run;
    auto engine = coney::platform::RenderEngine::start(coney::platform::RenderBackend::Null, {});
    REQUIRE(engine.has_value());
    if (!engine) {
        return run;
    }
    coney::platform::RenderEngine& renderer = **engine;
    coney::world::SectorBudget budget(coney::world::kSectorPoolSize);
    std::vector<std::string> log;
    const std::function<void(std::string_view)> print = [&log](std::string_view line) { log.emplace_back(line); };

    // The scripts as the story reaches the level, with the game's random table.
    coney::LevelScriptOptions options;
    const std::vector<std::uint32_t> table = randomTable(wad);
    if (!table.empty()) {
        options.randomTable = table;
    }
    coney::LevelScripts scripts(coney::script::wadScriptSource(wad), level, checkpoint, print, options);
    coney::GameplayMode gameplay(renderer, scripts.scripts(), scripts.context(), scripts.state(), scripts.humans(),
                                 scripts.flags(), scripts.recorded(), playLoader(renderer, wad, budget, scripts, print),
                                 print);
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
        if (line.starts_with("script error")) {
            run.errors.push_back(line);
        }
        if (line.find("is not a binding Coney has") != std::string::npos) {
            UNSCOPED_INFO(line);
            ++run.missingBindings;
        }
    }
    return run;
}

} // namespace

TEST_CASE("the disc's level80 and level87 play each checkpoint without a script error", "[disc][story]") {
    std::optional<coney::io::Wad> wad = openDisc();
    if (!wad) {
        SKIP("CONEY_DISC is not set: no disc to check");
    }
    // level80 has four checkpoints, level87 five.
    for (const auto& [level, checkpoints] :
         {std::pair{std::string_view("level80"), 4}, std::pair{std::string_view("level87"), 5}}) {
        for (int checkpoint = 1; checkpoint <= checkpoints; ++checkpoint) {
            INFO(level << " checkpoint " << checkpoint);
            const MissionRun run = playMission(*wad, level, checkpoint);
            REQUIRE(run.loaded);
            for (const std::string& error : run.errors) {
                UNSCOPED_INFO(error);
            }
            CHECK(run.scriptErrors == 0);
            CHECK(run.standing);
            CHECK(run.travelled > 1.0F);
            std::printf("  %.*s checkpoint %d: %zu humans created, %llu script errors, %zu missing bindings, %.1f m "
                        "walked\n",
                        static_cast<int>(level.size()), level.data(), checkpoint, run.humans,
                        static_cast<unsigned long long>(run.scriptErrors), run.missingBindings, run.travelled);
        }
    }
}

TEST_CASE("the disc's level34 plays each checkpoint without a script error or a missing binding", "[disc][story]") {
    std::optional<coney::io::Wad> wad = openDisc();
    if (!wad) {
        SKIP("CONEY_DISC is not set: no disc to check");
    }
    // Mission 4 has five checkpoints.
    constexpr int kCheckpoints = 5;
    for (int checkpoint = 1; checkpoint <= kCheckpoints; ++checkpoint) {
        INFO("level34 checkpoint " << checkpoint);
        const MissionRun run = playMission(*wad, "level34", checkpoint);
        REQUIRE(run.loaded);
        for (const std::string& error : run.errors) {
            UNSCOPED_INFO(error);
        }
        CHECK(run.scriptErrors == 0);
        CHECK(run.missingBindings == 0);
        CHECK(run.standing);
        CHECK(run.travelled > 1.0F);
        std::printf("  level34 checkpoint %d: %zu humans created, %llu script errors, %zu missing bindings, %.1f m "
                    "walked\n",
                    checkpoint, run.humans, static_cast<unsigned long long>(run.scriptErrors), run.missingBindings,
                    run.travelled);
    }
}

TEST_CASE("the disc's level80 intro, skipped, gives the screen and the player back", "[disc][story]") {
    // docs/research/scenes.md#skipping: the intro's camera track calls the end function three times while it plays
    // (global.lua's NumCallBacks), and PreCashTheWorld fades back in only once those calls are spent. A skip that
    // dropped them left the screen black for the rest of the level (the owner's "level80 flickers black").
    std::optional<coney::io::Wad> wad = openDisc();
    if (!wad) {
        SKIP("CONEY_DISC is not set: no disc to check");
    }
    auto engine = coney::platform::RenderEngine::start(coney::platform::RenderBackend::Null, {});
    REQUIRE(engine.has_value());
    coney::platform::RenderEngine& renderer = **engine;
    coney::world::SectorBudget budget(coney::world::kSectorPoolSize);
    std::vector<std::string> log;
    const std::function<void(std::string_view)> print = [&log](std::string_view line) { log.emplace_back(line); };
    coney::LevelScriptOptions options;
    const std::vector<std::uint32_t> table = randomTable(*wad);
    if (!table.empty()) {
        options.randomTable = table;
    }
    coney::LevelScripts scripts(coney::script::wadScriptSource(*wad), "level80", 1, print, options);
    auto sceneList = coney::scenes::loadSceneList(*wad);
    REQUIRE(sceneList.has_value());
    coney::GameplayMode gameplay(renderer, scripts.scripts(), scripts.context(), scripts.state(), scripts.humans(),
                                 scripts.flags(), scripts.recorded(),
                                 playLoader(renderer, *wad, budget, scripts, print), print);
    gameplay.setLevel("level80");
    gameplay.setSceneMaker([&wad, &sceneList] {
        return std::make_unique<coney::scenes::SceneSystem>(*sceneList, coney::scenes::wadSceneSource(*wad),
                                                            coney::scenes::SceneSystem::ScriptCall{});
    });

    // Cross 3 s in (past the 2 s a scene waits before a button skips it), then 10 s more.
    constexpr int kSkipFrame = 90;
    constexpr int kFrames = 400;
    auto input = coney::parseInputScript(std::format("{} tap cross\n", kSkipFrame));
    REQUIRE(input.has_value());
    coney::ScriptedInput pad(std::move(*input));
    coney::GameModeStack stack;
    stack.setInput(&pad);
    stack.push(gameplay);
    coney::GameTimer timer;
    timer.setFixedStep(true);
    // Each frame after the skip, whether the screen fade is fully black.
    int frame = 0;
    int blackAfterSkip = 0;
    const auto sample = [&]() {
        const auto* play = dynamic_cast<const coney::platform::PlayLevelMode*>(gameplay.level());
        if (play != nullptr && frame > kSkipFrame + 2 && play->stage().fadeLevel() >= 1.0F) {
            ++blackAfterSkip;
        }
        ++frame;
        return true;
    };
    stack.runUntilEmpty(timer, sample, kFrames);
    const auto* play = dynamic_cast<const coney::platform::PlayLevelMode*>(gameplay.level());
    REQUIRE(play != nullptr);
    const coney::anim::Vec3 at = play->player().human().position();
    const bool teleported = std::ranges::any_of(
        log, [](const std::string& line) { return line.starts_with("gameplay: player 1 teleported"); });
    CHECK(scripts.scripts().errors() == 0);
    CHECK(play->stage().fadeLevel() == 0.0F);
    // Black only for the frames the end takes to fade back in (0.5 s), not for the rest of the level.
    CHECK(blackAfterSkip < 30);
    CHECK(teleported);
    std::printf("  level80 checkpoint 1, intro skipped at frame %d: %d fully black frames after it, fade level %.2f "
                "at frame %d, player 1 %s at (%.1f, %.1f)\n",
                kSkipFrame, blackAfterSkip, static_cast<double>(play->stage().fadeLevel()), kFrames,
                teleported ? "teleported" : "not teleported", static_cast<double>(at.x), static_cast<double>(at.y));
}

TEST_CASE("the disc's level5 plays each checkpoint without a script error", "[disc][story]") {
    std::optional<coney::io::Wad> wad = openDisc();
    if (!wad) {
        SKIP("CONEY_DISC is not set: no disc to check");
    }
    // Mission 7 has four checkpoints.
    constexpr int kCheckpoints = 4;
    for (int checkpoint = 1; checkpoint <= kCheckpoints; ++checkpoint) {
        INFO("level5 checkpoint " << checkpoint);
        const MissionRun run = playMission(*wad, "level5", checkpoint);
        REQUIRE(run.loaded);
        for (const std::string& error : run.errors) {
            UNSCOPED_INFO(error);
        }
        CHECK(run.scriptErrors == 0);
        // At checkpoint 2 the Hurricanes in the room fight him from the start, so he may be down when the stick moves.
        if (checkpoint != 2) {
            CHECK(run.standing);
            CHECK(run.travelled > 1.0F);
        }
        std::printf("  level5 checkpoint %d: %zu humans created, %llu script errors, %zu missing bindings, %.1f m "
                    "walked\n",
                    checkpoint, run.humans, static_cast<unsigned long long>(run.scriptErrors), run.missingBindings,
                    run.travelled);
    }
}
