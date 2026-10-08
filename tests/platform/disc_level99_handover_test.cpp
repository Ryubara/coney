// SPDX-License-Identifier: GPL-3.0-or-later

// Checks against the player's own disc that player 1 stays player 1 when `level99` goes from checkpoint 1 to 2 in one
// session, as the story plays it: `P1.Cleanup` sets checkpoint 2 and `P2.SetupLesson1` makes a new Rembrandt and hands
// him the player with `HuChangePlayerGang` (docs/research/characters.md#level99-handover). From then on every lookup of
// player 1 must name the new human, or the street lesson stalls: the stereo theft's handler `P2.CarRadioStolen` asks
// `HuIsAPlayer` of the thief and only then schedules the next step. The level plays headless as `--play-level level99`
// plays it, the course's end called by the test (as level99's street test does), the cars' step called once the
// street scene is skipped, and the theft driven with the pad (stick turned in circles at 90 %). It runs only when the
// environment variable CONEY_DISC names the disc and skips otherwise; it prints counts only (LEGAL.md).

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <expected>
#include <format>
#include <memory>
#include <numbers>
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
#include "human/player.h"
#include "platform/play_level_mode.h"
#include "platform/render_engine.h"
#include "scenes/scene_disc.h"
#include "scenes/scene_list.h"
#include "scenes/scene_player.h"
#include "scripting/config_strings.h"
#include "scripting/script_system.h"
#include "warriors/created_humans.h"
#include "world/sector_budget.h"

namespace {

// The frames of the run, from the level's first: the course's end called, the street scene `l99_c2` skipped (cross
// after its 2 s), the cars' step called, player 1 put at car 142's front-left door, the theft from there.
constexpr std::uint64_t kCleanupAt = 300;
constexpr std::uint64_t kStreetSkipAt = 405;
constexpr std::uint64_t kCarsAt = 545;
constexpr std::uint64_t kTheftAt = 745;
constexpr std::uint64_t kEnd = 1500;

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

// The pad: the intro scene skipped, the street scene skipped, then at `theft` square at the car's window, triangle for
// the stereo and the left stick turned in circles at 90 % for the theft's stick game (combat.md#stereo-theft).
std::string padScript(std::uint64_t theft) {
    std::string script = std::format("100 tap cross\n{} tap cross\n{} tap square\n{} tap triangle\n", kStreetSkipAt,
                                     theft + 40, theft + 120);
    constexpr int kTurnUpdates = 400;
    constexpr float kDeflection = 90.0F;
    for (int i = 0; i < kTurnUpdates; ++i) {
        const float angle =
            (std::numbers::pi_v<float> / 2.0F) + (static_cast<float>(i) * std::numbers::pi_v<float> / 6.0F);
        script += std::format("{} stick left {} {}\n", theft + 150 + static_cast<std::uint64_t>(i),
                              static_cast<int>(kDeflection * std::cos(angle)),
                              static_cast<int>(kDeflection * std::sin(angle)));
    }
    script += std::format("{} stick left 0 0\n", theft + 150 + kTurnUpdates);
    return script;
}

// The handle of the last human the scripts made with `name`, or 0.
double lastNamed(const coney::CreatedHumans& humans, std::string_view name) {
    double handle = 0.0;
    for (const coney::HumanCreation& human : humans.all()) {
        if (human.name == name) {
            handle = human.handle;
        }
    }
    return handle;
}

// How many of `lines` contain `text`.
std::size_t countOf(const std::vector<std::string>& lines, std::string_view text) {
    std::size_t count = 0;
    for (const std::string& line : lines) {
        count += line.find(text) != std::string::npos ? 1 : 0;
    }
    return count;
}

} // namespace

TEST_CASE("the disc's level99: player 1 handed over at checkpoint 2 steals the stereo as the player",
          "[disc][story][handover]") {
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

    // The scripts as --play-level runs them at checkpoint 1, with the game's random table; their calls traced.
    coney::LevelScriptOptions options;
    std::vector<std::uint32_t> table;
    if (auto words = coney::io::readExecutableWords(wad->disc(), coney::GameRandom::kExecutableName,
                                                    coney::GameRandom::kTableAddress, coney::GameRandom::kTableSize)) {
        table = std::move(*words);
        options.randomTable = table;
    }
    coney::LevelScripts scripts(coney::script::wadScriptSource(*wad), "level99", 1, print, options);
    std::vector<std::string> trace;
    scripts.scripts().traceCalls([&trace](std::string_view line) { trace.emplace_back(line); });
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

    auto events = coney::parseInputScript(padScript(kTheftAt));
    REQUIRE(events.has_value());
    coney::ScriptedInput pad(std::move(*events));
    coney::GameModeStack stack;
    stack.setInput(&pad);
    stack.push(gameplay);
    coney::GameTimer timer;
    timer.setFixedStep(true);
    std::uint64_t frame = 0;
    const auto runTo = [&](std::uint64_t to) {
        stack.runUntilEmpty(timer, {}, to - frame);
        frame = to;
    };

    // The course's end: checkpoint 2, a new Rembrandt handed the player.
    runTo(kCleanupAt);
    const double coursePlayer = lastNamed(scripts.humans(), "Rembrandt");
    CHECK(scripts.scripts().call("P1.Cleanup"));
    runTo(kCarsAt);
    const double streetPlayer = lastNamed(scripts.humans(), "Rembrandt");
    REQUIRE(streetPlayer != coursePlayer);
    const coney::HumanCreation* player = scripts.humans().player(1);
    REQUIRE(player != nullptr);
    CHECK(player->handle == streetPlayer);
    CHECK(scripts.state().checkPoint == 2);

    // The cars' step (CarSpawnRadio, the theft handler), then player 1 at car 142's front-left door, facing it.
    CHECK(scripts.scripts().call("P2.SetupCars"));
    runTo(kTheftAt);
    auto* play = dynamic_cast<coney::platform::PlayLevelMode*>(gameplay.level());
    REQUIRE(play != nullptr);
    play->teleport(coney::debug::Place{.name = "car", .feet = {58.0F, 39.5F, 0.3F}, .headingDegrees = 180.0F});
    runTo(kEnd);

    const std::string stolenBy = std::format("> P2.CarRadioStolen({:.0f},", streetPlayer);
    const std::string isPlayer = std::format("HuIsAPlayer({:.0f}) -> 1", streetPlayer);
    std::printf(
        "  level99 hand-over: player 1 %.0f -> %.0f; %zu stereo thefts, %zu by the new player 1, %zu next steps\n",
        coursePlayer, streetPlayer, countOf(log, "theft: stole the stereo"), countOf(trace, stolenBy),
        countOf(trace, "ScheduleFunc(\"P2.SetupPeds\""));
    CHECK(scripts.scripts().errors() == 0);
    CHECK(countOf(log, "theft: stole the stereo") == 1);
    // The handler is told the new player 1 stole it, sees a player and schedules the next step (the mugging).
    CHECK(countOf(trace, stolenBy) == 1);
    CHECK(countOf(trace, isPlayer) >= 1);
    CHECK(countOf(trace, "ScheduleFunc(\"P2.SetupPeds\", 3000)") == 1);
}
