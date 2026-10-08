// SPDX-License-Identifier: GPL-3.0-or-later

// Checks against the player's own disc that `level5`'s two fights play to their ends as `--play-level level5
// --checkpoint N` plays them, headless, driven by the pad alone: after the chapter's scene the player walks up to the
// nearest foe still standing (the left stick at 50 %) until one is within 2.5 m and attacks (square, pressed only
// while one is that near).
// - Checkpoint 2, the bar: once every Hurricane of both bar gangs is down their gangs' message 18 runs `BarCleared`,
//   the scene `l5_c5` plays and its end sets checkpoint 3.
// - Checkpoint 4, the boss fight: `TacticBossScenarioA` takes Diego and Vargas through three stages and its last
//   callback ends the mission (`HUDLaunchMissionComplete`).
// See docs/research/scripting.md#level5. They run only when the environment variable CONEY_DISC names the disc and
// skip otherwise; they print counts and positions only (LEGAL.md).

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <expected>
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
#include "ai/ai_humans.h"
#include "ai/brains.h"
#include "characters/character_types.h"
#include "core/error.h"
#include "core/game_random.h"
#include "core/game_timer.h"
#include "core/input_script.h"
#include "core/pad.h"
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

// The frame the fight starts on (the chapter's scene is over by then).
constexpr std::uint64_t kFightFrom = 600;
// The stick's reach while walking up to a foe, and how often the walk is aimed again and square tapped.
constexpr int kWalkPercent = 50;
constexpr std::uint64_t kAimEvery = 40;
constexpr std::uint64_t kTapEvery = 10;
// Within this distance (metres) of the nearest foe the player stops walking and fights where he stands.
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

// The pad until `lastFrame`: from kFightFrom on, every kAimEvery frames a `steer` line toward the point (0, 0), which
// the fight's steer source places on the nearest foe standing, and square tapped every kTapEvery frames.
std::string fightScript(std::uint64_t lastFrame) {
    std::string script = "0 stick left 0 0\n";
    for (std::uint64_t frame = kFightFrom; frame < lastFrame; frame += kTapEvery) {
        if ((frame - kFightFrom) % kAimEvery == 0) {
            script += std::to_string(frame) + " steer " + std::to_string(kWalkPercent) + " 0,0\n";
        }
        script += std::to_string(frame) + " tap square\n";
    }
    return script;
}

// The script's pad with square let through only while `allowed` says so.
class FightPad final : public coney::InputSource {
  public:
    FightPad(coney::ScriptedInput& script, std::function<bool()> allowed)
        : m_script(script), m_allowed(std::move(allowed)) {}

    // The script's samples, port 1's square taken out when it is not allowed.
    [[nodiscard]] coney::PortSamples sample(std::uint64_t frame) override {
        coney::PortSamples samples = m_script.sample(frame);
        if (!m_allowed()) {
            samples[0].buttons = static_cast<std::uint16_t>(samples[0].buttons & ~coney::pad::kSquare);
            samples[0].pressure[static_cast<std::size_t>(coney::pad::Pressure::Square)] = 0;
        }
        return samples;
    }

  private:
    coney::ScriptedInput& m_script;
    std::function<bool()> m_allowed;
};

// What a fight's run found.
struct FightRun {
    std::uint64_t frame = 0;  // the frame the run stopped on
    std::size_t foes = 0;     // the humans the run counted as foes
    int standing = 0;         // of those, the ones with health left at the end
    int health = 0;           // player 1's health at the end
    std::uint64_t errors = 0; // the scripts' errors
};

// Plays `level5` at `checkpoint` as --play-level does, the pad fighting every human whose name starts with one of
// `foes`, until `done` holds or `lastFrame`. The log is shown when `done` never held.
FightRun playFight(const coney::io::Wad& wad, int checkpoint, std::vector<std::string_view> foes,
                   std::uint64_t lastFrame, const std::function<bool(coney::LevelScripts&)>& done) {
    FightRun run;
    auto engine = coney::platform::RenderEngine::start(coney::platform::RenderBackend::Null, {});
    REQUIRE(engine.has_value());
    coney::platform::RenderEngine& renderer = **engine;
    coney::world::SectorBudget budget(coney::world::kSectorPoolSize);
    std::vector<std::string> log;
    const auto print = [&log](std::string_view line) { log.emplace_back(line); };

    // The scripts as --play-level runs them, with the game's random table.
    coney::LevelScriptOptions options;
    std::vector<std::uint32_t> table;
    if (auto words = coney::io::readExecutableWords(wad.disc(), coney::GameRandom::kExecutableName,
                                                    coney::GameRandom::kTableAddress, coney::GameRandom::kTableSize)) {
        table = std::move(*words);
        options.randomTable = table;
    }
    coney::LevelScripts scripts(coney::script::wadScriptSource(wad), "level5", checkpoint, print, options);
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
        (*mode)->useHud(scripts.hud());
        return std::unique_ptr<coney::GameMode>(std::move(*mode));
    };
    auto sceneList = coney::scenes::loadSceneList(wad);
    REQUIRE(sceneList.has_value());
    coney::GameplayMode gameplay(renderer, scripts.scripts(), scripts.context(), scripts.state(), scripts.humans(),
                                 scripts.flags(), scripts.recorded(), std::move(loader), print);
    gameplay.setLevel("level5");
    gameplay.setSceneMaker([&wad, &sceneList] {
        return std::make_unique<coney::scenes::SceneSystem>(*sceneList, coney::scenes::wadSceneSource(wad),
                                                            coney::scenes::SceneSystem::ScriptCall{});
    });

    auto events = coney::parseInputScript(fightScript(lastFrame));
    REQUIRE(events.has_value());
    coney::ScriptedInput pad(std::move(*events));
    const auto play = [&gameplay] { return dynamic_cast<const coney::platform::PlayLevelMode*>(gameplay.level()); };
    // The foes made so far (a stage may make more), looked up afresh on each use.
    const auto foeBrains = [&scripts, &foes](const coney::platform::PlayLevelMode& level) {
        std::vector<const coney::ai::Brain*> found;
        for (const coney::HumanCreation& human : scripts.humans().all()) {
            for (const std::string_view prefix : foes) {
                if (std::string_view(human.name).starts_with(prefix)) {
                    if (const coney::ai::Brain* brain = brainNamed(level, human.handle); brain != nullptr) {
                        found.push_back(brain);
                    }
                    break;
                }
            }
        }
        return found;
    };
    // The nearest foe still standing, where player 1's feet are.
    const auto nearestFoe = [&play, &foeBrains]() -> std::optional<coney::anim::Vec3> {
        const coney::platform::PlayLevelMode* level = play();
        if (level == nullptr) {
            return std::nullopt;
        }
        const coney::anim::Vec3 feet = level->playerFeet();
        std::optional<coney::anim::Vec3> nearest;
        for (const coney::ai::Brain* brain : foeBrains(*level)) {
            if (brain->human().health().value() <= 0) {
                continue;
            }
            const coney::anim::Vec3 at = brain->human().position();
            if (!nearest ||
                std::hypot(at.x - feet.x, at.y - feet.y) < std::hypot(nearest->x - feet.x, nearest->y - feet.y)) {
                nearest = at;
            }
        }
        return nearest;
    };
    // Whether the nearest standing foe is within kFightRange of player 1.
    const auto inReach = [&play, &nearestFoe] {
        const std::optional<coney::anim::Vec3> nearest = nearestFoe();
        if (!nearest || play() == nullptr) {
            return false;
        }
        const coney::anim::Vec3 feet = play()->playerFeet();
        return std::hypot(nearest->x - feet.x, nearest->y - feet.y) < kFightRange;
    };
    // The steer source puts the route's point (0, 0) on the nearest foe still standing: the view is player 1's feet
    // less that foe's. With none standing, or one within kFightRange, the stick rests.
    pad.setSteerSource([&play, &nearestFoe, &inReach](std::size_t port) -> std::optional<coney::SteerView> {
        const coney::platform::PlayLevelMode* level = play();
        const std::optional<coney::anim::Vec3> nearest = nearestFoe();
        if (port != 0 || level == nullptr || !nearest || inReach()) {
            return std::nullopt;
        }
        const coney::anim::Vec3 feet = level->playerFeet();
        const coney::anim::Vec3 eye = level->cameraEye();
        const coney::anim::Vec3 target = level->cameraTarget();
        constexpr float kDegrees = 57.29578F;
        return coney::SteerView{.x = feet.x - nearest->x,
                                .y = feet.y - nearest->y,
                                .cameraHeading = std::atan2(-(target.x - eye.x), target.y - eye.y) * kDegrees};
    });
    // Square only with a standing foe in reach: pressed near a downed one it would start ground strikes on him
    // (combat-moves.md, 661 and 194) and hold the player there.
    FightPad fightPad(pad, inReach);
    coney::GameModeStack stack;
    stack.setInput(&fightPad);
    stack.push(gameplay);
    coney::GameTimer timer;
    timer.setFixedStep(true);

    // The chapter's set-up and scene, then the fight until `done`.
    stack.runUntilEmpty(timer, {}, static_cast<int>(kFightFrom));
    REQUIRE(play() != nullptr);
    run.frame = kFightFrom;
    while (run.frame < lastFrame && !done(scripts)) {
        stack.runUntilEmpty(timer, {}, 1);
        ++run.frame;
    }
    if (const coney::platform::PlayLevelMode* level = play(); level != nullptr) {
        const std::vector<const coney::ai::Brain*> found = foeBrains(*level);
        run.foes = found.size();
        for (const coney::ai::Brain* brain : found) {
            run.standing += brain->human().health().value() > 0 ? 1 : 0;
        }
        run.health = level->player().human().health().value();
    }
    run.errors = scripts.scripts().errors();
    if (!done(scripts)) {
        for (const std::string& line : log) {
            UNSCOPED_INFO(line);
        }
    }
    return run;
}

} // namespace

TEST_CASE("the disc's level5: the bar fight, won with the pad, moves the mission to the chase",
          "[disc][story][scenes]") {
    std::optional<coney::io::Wad> wad = openDisc();
    if (!wad) {
        SKIP("CONEY_DISC is not set: no disc to check");
    }
    // The Hurricanes of both bar gangs: the humans the chapter names `HurrB...`.
    double checkPoint = 0.0;
    const FightRun run = playFight(*wad, 2, {"HurrB"}, 15000, [&checkPoint](coney::LevelScripts& scripts) {
        checkPoint = scripts.state().checkPoint;
        return checkPoint >= 3;
    });
    std::printf("  level5 bar fight: checkpoint %g at frame %llu, %zu Hurricanes, %d standing, player 1 health %d\n",
                checkPoint, static_cast<unsigned long long>(run.frame), run.foes, run.standing, run.health);
    CHECK(run.foes == 8);
    CHECK(checkPoint == 3);
    CHECK(run.errors == 0);
}

TEST_CASE("the disc's level5: the boss fight, won with the pad, ends the mission", "[disc][story][scenes]") {
    std::optional<coney::io::Wad> wad = openDisc();
    if (!wad) {
        SKIP("CONEY_DISC is not set: no disc to check");
    }
    bool complete = false;
    const FightRun run = playFight(*wad, 4, {"Diego", "Vargas"}, 15000, [&complete](coney::LevelScripts& scripts) {
        complete = scripts.host().missionCompleteRequested();
        return complete;
    });
    std::printf("  level5 boss fight: mission complete %d at frame %llu, %zu bosses, %d standing, player 1 health %d\n",
                complete ? 1 : 0, static_cast<unsigned long long>(run.frame), run.foes, run.standing, run.health);
    CHECK(complete);
    CHECK(run.errors == 0);
}
