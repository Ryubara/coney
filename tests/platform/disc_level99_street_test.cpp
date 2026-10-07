// SPDX-License-Identifier: GPL-3.0-or-later

// Checks against the player's own disc that the end of `level99`'s combat course moves the tutorial to the street:
// the level plays as `--play-level level99 --checkpoint 1` plays it, headless; once the intro is skipped the script's
// own end of the last lesson (`P1.CombatOver`, which the Warriors' stopwatch runs) is called, the scene `l99_c6`
// plays, `P1.Cleanup` sets checkpoint 2 and `P2.SetupLesson1` makes the Warriors again on the street and plays
// `l99_c2` (docs/research/scripting.md#level99-checkpoints). Then player 1 must be on the street, Vermin beside him and
// the course's humans gone. It runs only when the environment variable CONEY_DISC names the disc and skips otherwise;
// it prints counts and positions only (LEGAL.md).

#include <array>
#include <cmath>
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
#include "ai/ai_humans.h"
#include "ai/brains.h"
#include "characters/character_types.h"
#include "core/error.h"
#include "core/game_random.h"
#include "core/game_timer.h"
#include "core/input_script.h"
#include "core/pads.h"
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
#include "scripting/script_system.h"
#include "world/sector_budget.h"

namespace {

// The pad: the intro scene skipped, then nothing.
constexpr std::string_view kPad = "0 stick left 0 0\n100 tap cross\n";
// The frame the course's end is called on, the intro long skipped.
constexpr std::uint64_t kCallAt = 300;
// How long the two scenes (`l99_c6`, then `l99_c2`) and the set-up between them may take, in frames.
constexpr std::uint64_t kSceneWait = 6000;
// Frames after the street's scene ends before the humans are looked at.
constexpr int kSettle = 60;
// Where the street lesson makes player 1 (P2.SetupLesson1's HuCreate, read at run time from the disc's script), and
// how far away a scene may leave him; the course is about 330 m away.
constexpr float kStreetX = 46.0F;
constexpr float kStreetY = 34.2F;
constexpr float kStreetRange = 20.0F;
// The middle of the combat course (the course's first player 1 is made at about (-284, 120)) and its extent.
constexpr float kCourseX = -280.0F;
constexpr float kCourseY = 120.0F;
constexpr float kCourseRange = 60.0F;

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

// The distance in plan from `position` to (x, y).
float planDistance(coney::anim::Vec3 position, float x, float y) { return std::hypot(position.x - x, position.y - y); }

// The brain the scripts name `handle`, or null.
const coney::ai::Brain* brainNamed(const coney::platform::PlayLevelMode& play, double handle) {
    const coney::ai::Brains& brains = play.fighters().brains();
    for (std::size_t i = 0; i < brains.size(); ++i) {
        if (brains.at(i).handle() == handle) {
            return &brains.at(i);
        }
    }
    return nullptr;
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

} // namespace

TEST_CASE("the disc's level99: the end of the combat course moves the tutorial to the street",
          "[disc][story][scenes]") {
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
    coney::scenes::SceneSystem* sceneSystem = nullptr;
    gameplay.setSceneMaker([&wad, &sceneList, &sceneSystem] {
        auto made = std::make_unique<coney::scenes::SceneSystem>(*sceneList, coney::scenes::wadSceneSource(*wad),
                                                                 coney::scenes::SceneSystem::ScriptCall{});
        sceneSystem = made.get();
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
    const auto play = [&gameplay] { return dynamic_cast<const coney::platform::PlayLevelMode*>(gameplay.level()); };
    std::uint64_t frame = 0;
    const auto step = [&] {
        stack.runUntilEmpty(timer, {}, 1);
        ++frame;
    };

    // 1. The level starts and the intro is skipped; the course's player 1 is on the course.
    while (frame < kCallAt) {
        step();
    }
    REQUIRE(play() != nullptr);
    REQUIRE(sceneSystem != nullptr);
    const double coursePlayer = lastNamed(scripts.humans(), "Rembrandt");
    CHECK(planDistance(play()->player().human().position(), kCourseX, kCourseY) < kCourseRange);
    CHECK(scripts.state().checkPoint == 1);
    // Every human the course made but player 1, each of them in a gang the course's end deletes.
    std::vector<double> courseHumans;
    for (const coney::HumanCreation& human : scripts.humans().all()) {
        if (human.handle != coursePlayer) {
            courseHumans.push_back(human.handle);
        }
    }

    // 2. The course's end, as the script runs it; then its scene, the set-up of checkpoint 2 and the street's scene,
    // each played to its end.
    REQUIRE(scripts.scripts().call("P1.CombatOver", {}));
    int scenesPlayed = 0;
    bool playing = false;
    while (frame < kCallAt + kSceneWait && scenesPlayed < 2) {
        step();
        const bool active = sceneSystem->cinematicActive();
        scenesPlayed += playing && !active ? 1 : 0;
        playing = active;
    }
    for (int i = 0; i < kSettle; ++i) {
        step();
    }

    // 3. Player 1 on the street as the new Rembrandt, Vermin beside him, nobody left on the course.
    const coney::platform::PlayLevelMode& level = *play();
    const coney::anim::Vec3 player = level.player().human().position();
    const double streetPlayer = lastNamed(scripts.humans(), "Rembrandt");
    const double vermin = lastNamed(scripts.humans(), "Vermin");
    const coney::ai::Brain* verminBrain = brainNamed(level, vermin);
    // The course's humans still in the world (their handles still naming someone).
    int onCourse = 0;
    for (const double human : courseHumans) {
        onCourse += brainNamed(level, human) != nullptr ? 1 : 0;
    }
    std::printf("  level99 after the course (frame %llu, %d scenes): checkpoint %g, player 1 at (%.2f, %.2f, %.2f), "
                "%.2f m from the street's start; Vermin %s; %zu course humans made, %d left\n",
                static_cast<unsigned long long>(frame), scenesPlayed, scripts.state().checkPoint, player.x, player.y,
                player.z, planDistance(player, kStreetX, kStreetY),
                verminBrain != nullptr ? std::format("{:.2f} m from him",
                                                     planDistance(verminBrain->human().position(), player.x, player.y))
                                             .c_str()
                                       : "not made",
                courseHumans.size(), onCourse);
    const bool failed = scenesPlayed < 2 || planDistance(player, kStreetX, kStreetY) >= kStreetRange;
    if (failed) {
        for (const std::string& line : log) {
            UNSCOPED_INFO(line);
        }
    }
    CHECK(scenesPlayed == 2);
    CHECK(scripts.state().checkPoint == 2);
    CHECK(streetPlayer != coursePlayer);
    CHECK(brainNamed(level, coursePlayer) == nullptr);
    CHECK(level.fighters().playerBrain().handle() == streetPlayer);
    CHECK(planDistance(player, kStreetX, kStreetY) < kStreetRange);
    REQUIRE(verminBrain != nullptr);
    CHECK(planDistance(verminBrain->human().position(), player.x, player.y) < kStreetRange);
    CHECK(onCourse == 0);
    CHECK(scripts.scripts().errors() == 0);
}
