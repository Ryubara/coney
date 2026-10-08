// SPDX-License-Identifier: GPL-3.0-or-later

// Checks against the player's own disc that `level5`'s checkpoint 2, the bar fight, plays to its end as
// `--play-level level5 --checkpoint 2` plays it, headless, driven by the pad alone: the scene `l5_c2`, then the player
// walks up to the nearest Hurricane still standing (the left stick at 50 %) until one is within 2.5 m and attacks
// (square) until every Hurricane of both gangs is down; their gangs' message 18 runs `BarCleared`, the scene `l5_c5`
// plays and its end sets checkpoint 3 (docs/research/scripting.md#level5). It runs only when the environment variable
// CONEY_DISC names the disc and skips otherwise; it prints counts and positions only (LEGAL.md).

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
#include "ai/ai_humans.h"
#include "ai/brains.h"
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

// The frame the fight starts on (the scene `l5_c2` is over by then) and the last frame the test waits for.
constexpr std::uint64_t kFightFrom = 600;
constexpr std::uint64_t kLastFrame = 15000;
// The stick's reach while walking up to a Hurricane, and how often the walk is aimed again and square tapped.
constexpr int kWalkPercent = 50;
constexpr std::uint64_t kAimEvery = 40;
constexpr std::uint64_t kTapEvery = 10;
// Within this distance (metres) of the nearest Hurricane the player stops walking and fights where he stands.
constexpr float kFightRange = 2.5F;

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

// The pad: from kFightFrom on, every kAimEvery frames a `steer` line toward the point (0, 0), which the test's steer
// source places on the nearest standing Hurricane, and square tapped every kTapEvery frames.
std::string fightScript() {
    std::string script = "0 stick left 0 0\n";
    for (std::uint64_t frame = kFightFrom; frame < kLastFrame; frame += kTapEvery) {
        if ((frame - kFightFrom) % kAimEvery == 0) {
            script += std::to_string(frame) + " steer " + std::to_string(kWalkPercent) + " 0,0\n";
        }
        script += std::to_string(frame) + " tap square\n";
    }
    return script;
}

} // namespace

TEST_CASE("the disc's level5: the bar fight, won with the pad, moves the mission to the chase",
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
    coney::LevelScripts scripts(coney::script::wadScriptSource(*wad), "level5", 2, print, options);
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
    gameplay.setLevel("level5");
    gameplay.setSceneMaker([&wad, &sceneList] {
        return std::make_unique<coney::scenes::SceneSystem>(*sceneList, coney::scenes::wadSceneSource(*wad),
                                                            coney::scenes::SceneSystem::ScriptCall{});
    });

    auto events = coney::parseInputScript(fightScript());
    REQUIRE(events.has_value());
    coney::ScriptedInput pad(std::move(*events));
    const auto play = [&gameplay] { return dynamic_cast<const coney::platform::PlayLevelMode*>(gameplay.level()); };
    // The Hurricanes of both bar gangs: the humans the chapter names `HurrB...`.
    std::vector<double> hurricanes;
    // The steer source puts the route's point (0, 0) on the nearest Hurricane still standing: the view is player 1's
    // feet less that Hurricane's. With none standing, or one within kFightRange, the stick rests.
    pad.setSteerSource([&play, &hurricanes](std::size_t port) -> std::optional<coney::SteerView> {
        const coney::platform::PlayLevelMode* level = play();
        if (port != 0 || level == nullptr) {
            return std::nullopt;
        }
        const coney::anim::Vec3 feet = level->playerFeet();
        std::optional<coney::anim::Vec3> nearest;
        for (const double handle : hurricanes) {
            const coney::ai::Brain* brain = brainNamed(*level, handle);
            if (brain == nullptr || brain->human().health().value() <= 0) {
                continue;
            }
            const coney::anim::Vec3 at = brain->human().position();
            if (!nearest ||
                std::hypot(at.x - feet.x, at.y - feet.y) < std::hypot(nearest->x - feet.x, nearest->y - feet.y)) {
                nearest = at;
            }
        }
        if (!nearest || std::hypot(nearest->x - feet.x, nearest->y - feet.y) < kFightRange) {
            return std::nullopt;
        }
        const coney::anim::Vec3 eye = level->cameraEye();
        const coney::anim::Vec3 target = level->cameraTarget();
        constexpr float kDegrees = 57.29578F;
        return coney::SteerView{.x = feet.x - nearest->x,
                                .y = feet.y - nearest->y,
                                .cameraHeading = std::atan2(-(target.x - eye.x), target.y - eye.y) * kDegrees};
    });
    coney::GameModeStack stack;
    stack.setInput(&pad);
    stack.push(gameplay);
    coney::GameTimer timer;
    timer.setFixedStep(true);

    // 1. The chapter is set up and its scene starts; the bar's Hurricanes are known.
    stack.runUntilEmpty(timer, {}, static_cast<int>(kFightFrom));
    REQUIRE(play() != nullptr);
    for (const coney::HumanCreation& human : scripts.humans().all()) {
        if (std::string_view(human.name).starts_with("HurrB")) {
            hurricanes.push_back(human.handle);
        }
    }
    CHECK(scripts.state().checkPoint == 2);

    // 2. The fight, until the end scene's callback sets checkpoint 3.
    std::uint64_t frame = kFightFrom;
    while (frame < kLastFrame && scripts.state().checkPoint < 3) {
        stack.runUntilEmpty(timer, {}, 1);
        ++frame;
    }
    int standing = 0;
    for (const double handle : hurricanes) {
        const coney::ai::Brain* brain = play() != nullptr ? brainNamed(*play(), handle) : nullptr;
        standing += brain != nullptr && brain->human().health().value() > 0 ? 1 : 0;
    }
    const int health = play() != nullptr ? play()->player().human().health().value() : 0;
    std::printf("  level5 bar fight: checkpoint %g at frame %llu, %zu Hurricanes, %d standing, player 1 health %d\n",
                scripts.state().checkPoint, static_cast<unsigned long long>(frame), hurricanes.size(), standing,
                health);
    if (scripts.state().checkPoint < 3) {
        for (const std::string& line : log) {
            UNSCOPED_INFO(line);
        }
    }
    CHECK(hurricanes.size() == 8);
    CHECK(scripts.state().checkPoint == 3);
    CHECK(scripts.scripts().errors() == 0);
}
