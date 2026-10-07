// SPDX-License-Identifier: GPL-3.0-or-later

// Checks against the player's own disc that the last lesson of `level99`'s combat tutorial is a fight: the level plays
// as `--play-level level99 --checkpoint 1` plays it, headless; once the intro is skipped the script's own set-up of
// the lesson is called as the research's saved state was made (`AddFenceWarriors1`, the fence gang made friends with
// the Warriors, `P1.SetupWarriors`, docs/research/ai.md#level99-save), the scene `l99_c5` plays and its return
// function `P1.SendWarriors` gives each of the three sparring Warriors `GoalFight` on a player who stands still
// (the recording `warriors_passive`, docs/research/ai.md#level99-fight). Then each Warrior must close on the player
// and attack him. It runs only when the environment variable CONEY_DISC names the disc and skips otherwise; it prints
// counts and distances only (LEGAL.md).

#include <algorithm>
#include <array>
#include <cmath>
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
#include "ai/brains.h"
#include "ai/gangs.h"
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
#include "scripting/lua_value.h"
#include "scripting/script_system.h"
#include "world/sector_budget.h"

namespace {

// The pad: the intro scene skipped, then nothing (the player stands still, as in `warriors_passive`).
constexpr std::string_view kPad = "0 stick left 0 0\n100 tap cross\n";
// The frame the lesson's set-up is called on, the intro long skipped.
constexpr std::uint64_t kCallAt = 300;
// How long the scene `l99_c5` (16.7 s) may take to start and end, in frames.
constexpr std::uint64_t kSceneWait = 1500;
// How long the fight is watched after the scene, in frames (10 s at 30 per second).
constexpr int kFightFrames = 300;
// How near a Warrior must come to the player: within his far melee range (5 m, docs/research/ai.md#level99).
constexpr float kCloseRange = 3.0F;
// The sparring Warriors' gang (docs/research/ai.md#level99-fight).
constexpr std::string_view kSparringGang = "CombatWarriors";

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

// The id of the gang named `name`, or -1.
int gangNamed(const coney::ai::Gangs& gangs, std::string_view name) {
    for (std::size_t id = 0; id < coney::ai::kGangSlots; ++id) {
        const coney::ai::Gang* gang = gangs.find(static_cast<int>(id));
        if (gang != nullptr && gang->name() == name) {
            return static_cast<int>(id);
        }
    }
    return -1;
}

// What is watched of one sparring Warrior while the fight runs.
struct Watched {
    const coney::ai::Brain* brain = nullptr;
    float startDistance = 0.0F;
    float nearest = 1e9F;
    int attacks = 0; // attack actions started (its next-attack time moved on)
    std::uint64_t lastNextAttack = 0;
};

} // namespace

TEST_CASE("the disc's level99: the sparring Warriors of the last lesson close on the player and attack",
          "[disc][story][ai]") {
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

    // 1. The level starts and the intro is skipped.
    while (frame < kCallAt) {
        step();
    }
    REQUIRE(play() != nullptr);
    REQUIRE(sceneSystem != nullptr);

    // 2. The lesson's set-up as the research's state was made: the fence Warriors, their gang friends with the
    // Warriors', then the scene and its return function, played to the scene's end.
    REQUIRE(scripts.scripts().call("AddFenceWarriors1", {}));
    const int fenceGang = gangNamed(play()->fighters().brains().gangs(), "FenceWarriors1");
    if (fenceGang >= 0) {
        const std::array<coney::script::Value, 2> friends{coney::script::Value(0.0),
                                                          coney::script::Value(static_cast<double>(fenceGang))};
        CHECK(scripts.scripts().call("GangMakeFriends", friends));
    }
    REQUIRE(scripts.scripts().call("P1.SetupWarriors", {}));
    bool played = false;
    bool playing = false;
    while (frame < kCallAt + kSceneWait && !played) {
        step();
        const bool active = sceneSystem->cinematicActive();
        played = playing && !active;
        playing = active;
    }
    REQUIRE(played);

    // 3. The fight, the player standing still: each sparring Warrior's nearest approach and attacks.
    const coney::platform::PlayLevelMode& level = *play();
    const int sparring = gangNamed(level.fighters().brains().gangs(), kSparringGang);
    REQUIRE(sparring >= 0);
    const coney::ai::Gang* gang = level.fighters().brains().gangs().find(sparring);
    REQUIRE(gang != nullptr);
    std::vector<Watched> warriors;
    const coney::anim::Vec3 player = level.player().human().position();
    for (const coney::ai::Brain* member : gang->members()) {
        const coney::anim::Vec3 at = member->human().position();
        warriors.push_back(Watched{.brain = member,
                                   .startDistance = std::hypot(at.x - player.x, at.y - player.y),
                                   .lastNextAttack = member->nextAttackMs()});
    }
    const int healthBefore = level.player().human().fighter().health().value();
    for (int i = 0; i < kFightFrames; ++i) {
        step();
        const coney::anim::Vec3 now = play()->player().human().position();
        for (Watched& watched : warriors) {
            const coney::anim::Vec3 at = watched.brain->human().position();
            watched.nearest = std::min(watched.nearest, std::hypot(at.x - now.x, at.y - now.y));
            if (watched.brain->nextAttackMs() != watched.lastNextAttack) {
                watched.lastNextAttack = watched.brain->nextAttackMs();
                ++watched.attacks;
            }
        }
    }
    const int healthAfter = play()->player().human().fighter().health().value();
    int closed = 0;
    int attacked = 0;
    for (const Watched& watched : warriors) {
        std::printf("  level99 sparring Warrior: %.2f m from the player when sent, nearest %.2f m, %d attacks, %zu "
                    "goals\n",
                    watched.startDistance, watched.nearest, watched.attacks, watched.brain->goalCount());
        closed += watched.nearest < kCloseRange ? 1 : 0;
        attacked += watched.attacks > 0 ? 1 : 0;
    }
    std::printf("  level99 sparring fight: %zu Warriors, %d closed in, %d attacked; player health %d -> %d\n",
                warriors.size(), closed, attacked, healthBefore, healthAfter);
    if (closed < 3 || attacked < 3) {
        for (const std::string& line : log) {
            UNSCOPED_INFO(line);
        }
    }
    CHECK(warriors.size() == 3);
    CHECK(closed == 3);
    CHECK(attacked == 3);
    CHECK(scripts.scripts().errors() == 0);
}
