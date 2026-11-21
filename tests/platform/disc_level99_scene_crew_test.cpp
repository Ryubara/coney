// SPDX-License-Identifier: GPL-3.0-or-later

// Checks against the player's own disc what a story cinematic's end does to player 1's gang: `level99` at checkpoint 1
// plays as `--play-level level99 --checkpoint 1` plays it, headless, through the intro scene `l99_c1`, which binds
// player 1 (Rembrandt). With hold ordered for his crew and his command menu locked while it plays, the update in which
// the scene ends leaves his menu unlocked, follow his last command and his gang's new follow tactic started by the
// gang update the end runs (docs/research/scenes.md#gang). It runs only when the environment variable CONEY_DISC
// names the disc and skips otherwise; it prints counts only (LEGAL.md).

#include <array>
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
#include "ai/brain.h"
#include "ai/gangs.h"
#include "ai/scripted_brains.h"
#include "ai/tactic.h"
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
#include "warriors/character_rules.h"
#include "warriors/game_state.h"
#include "warriors/story_state.h"
#include "world/sector_budget.h"

namespace {

// The follow tactic's type, WarriorFollowTactic (docs/research/ai-gangs.md#warrior-follow).
constexpr int kFollowTacticType = 0x12;

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

// What a test reads of level99's intro playing: the play mode, its stack and timer, the level's scripts and scenes.
struct IntroPlay {
    coney::GameplayMode& gameplay;
    coney::GameModeStack& stack;
    coney::GameTimer& timer;
    coney::LevelScripts& scripts;
    coney::scenes::SceneSystem& system;
};

// Plays level99 at checkpoint 1 as --play-level plays it, headless, 10 s into its intro l99_c1 (player 1 bound in
// it, playing), then hands what plays to `body`.
void playIntro(coney::io::Wad& wad, const std::function<void(IntroPlay&)>& body) {
    auto engine = coney::platform::RenderEngine::start(coney::platform::RenderBackend::Null, {});
    REQUIRE(engine.has_value());
    coney::platform::RenderEngine& renderer = **engine;
    coney::world::SectorBudget budget(coney::world::kSectorPoolSize);
    std::vector<std::string> log;
    const auto print = [&log](std::string_view line) { log.emplace_back(line); };

    // The scripts with the game's random table; the play mode draws the scripts' HUD.
    coney::LevelScriptOptions options;
    std::vector<std::uint32_t> table;
    if (auto words = coney::io::readExecutableWords(wad.disc(), coney::GameRandom::kExecutableName,
                                                    coney::GameRandom::kTableAddress, coney::GameRandom::kTableSize)) {
        table = std::move(*words);
        options.randomTable = table;
    }
    coney::LevelScripts scripts(coney::script::wadScriptSource(wad), "level99", 1, print, options);
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
        auto mode = coney::platform::PlayLevelMode::create(renderer, wad, start.level, budget, print, playerStart,
                                                           setup, &cast);
        if (!mode) {
            return std::unexpected(std::move(mode.error()));
        }
        (*mode)->useHud(scripts.hud());
        return std::unique_ptr<coney::GameMode>(std::move(*mode));
    };
    // The level's scenes over the disc's scene list, as the game makes them.
    auto sceneList = coney::scenes::loadSceneList(wad);
    REQUIRE(sceneList.has_value());
    coney::GameplayMode gameplay(renderer, scripts.scripts(), scripts.context(), scripts.state(), scripts.humans(),
                                 scripts.flags(), scripts.recorded(), std::move(loader), print);
    gameplay.setLevel("level99");
    gameplay.setSceneMaker([&wad, &sceneList] {
        return std::make_unique<coney::scenes::SceneSystem>(*sceneList, coney::scenes::wadSceneSource(wad),
                                                            coney::scenes::SceneSystem::ScriptCall{});
    });

    // The pad at rest throughout, 10 s in.
    auto input = coney::parseInputScript("");
    REQUIRE(input.has_value());
    coney::ScriptedInput pad(std::move(*input));
    coney::GameModeStack stack;
    stack.setInput(&pad);
    stack.push(gameplay);
    coney::GameTimer timer;
    timer.setFixedStep(true);
    stack.runUntilEmpty(timer, {}, 300);
    coney::scenes::SceneSystem* system = scripts.context().scenes;
    REQUIRE(system != nullptr);
    const std::optional<std::uint32_t> intro = system->idOf("l99_c1");
    REQUIRE(intro.has_value());
    // NOLINTNEXTLINE(bugprone-unchecked-optional-access): the REQUIRE above ends the test when empty
    REQUIRE(system->state(*intro) == coney::scenes::SceneState::Playing);
    IntroPlay play{.gameplay = gameplay, .stack = stack, .timer = timer, .scripts = scripts, .system = *system};
    body(play);
}

} // namespace

TEST_CASE("the disc's level99: the intro scene's end gives player 1's crew the follow and starts it",
          "[disc][story][scenes][crew]") {
    // Expected: scenes.md#gang, confirmed (runtime), run r-20261120-scene-crew-follow: SceneTask_End (0x0039f450)
    // gives player 1's crew the follow once a cinematic ends, in force right after it.
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

    // The pad at rest throughout: the intro plays out.
    auto input = coney::parseInputScript("");
    REQUIRE(input.has_value());
    coney::ScriptedInput pad(std::move(*input));
    coney::GameModeStack stack;
    stack.setInput(&pad);
    stack.push(gameplay);
    coney::GameTimer timer;
    timer.setFixedStep(true);

    // 10 s in, the intro plays with player 1 in it. As in the runtime check on the original (scenes.md#gang): every
    // command enabled for him (level99 enables none) and the automatic commands off, so only the scene's end can
    // change his command. A command cannot be ordered while the scene camera is his, so hold (3) is written as his
    // last command, and his menu locked; his crew's tactic now is noted, to tell a new one from it.
    stack.runUntilEmpty(timer, {}, 300);
    coney::scenes::SceneSystem* system = scripts.context().scenes;
    REQUIRE(system != nullptr);
    const std::optional<std::uint32_t> intro = system->idOf("l99_c1");
    REQUIRE(intro.has_value());
    // NOLINTNEXTLINE(bugprone-unchecked-optional-access): the REQUIRE above ends the test when empty
    REQUIRE(system->state(*intro) == coney::scenes::SceneState::Playing);
    coney::CharacterRules& rules = scripts.state().characters;
    coney::StoryState& story = scripts.state().story;
    rules.warriorCommands.at(0).fill(true);
    scripts.state().autoSwitch = false;
    rules.lastWarriorCommand.at(0) = 3;
    story.menuLocked.at(0) = true;
    const coney::ai::ScriptedBrains* scripted = gameplay.scripted();
    REQUIRE(scripted != nullptr);
    const coney::ai::Brain* chief = scripted->player();
    REQUIRE(chief != nullptr);
    const coney::ai::Gang* crew = chief->gang();
    REQUIRE(crew != nullptr);
    const std::uint32_t tacticsBefore = crew->tacticsSet();

    // Step by step to the update in which the scene ends (2,026 frames of scene; done() holds from state 7, the
    // update before the end).
    const std::uint64_t endedBefore = system->stats().ended;
    int steps = 0;
    while (system->stats().ended == endedBefore && steps < 2400) {
        stack.runUntilEmpty(timer, {}, 1);
        ++steps;
    }
    REQUIRE(system->stats().ended == endedBefore + 1);
    CHECK(scripts.scripts().errors() == 0);
    CHECK(rules.lastWarriorCommand.at(0) == 0);
    CHECK_FALSE(story.menuLocked.at(0));
    REQUIRE(scripted->player() == chief);
    REQUIRE(chief->gang() == crew);
    const coney::ai::Tactic* tactic = crew->tactic();
    REQUIRE(tactic != nullptr);
    CHECK(crew->tacticsSet() == tacticsBefore + 1); // one new tactic: the follow's
    CHECK(tactic->type() == kFollowTacticType);
    CHECK(tactic->started());
    std::printf("  level99 l99_c1: ended after %d more steps; crew of %zu, tactic %#x started %d\n", steps,
                crew->members().size(), static_cast<unsigned>(tactic->type()), tactic->started() ? 1 : 0);
}

TEST_CASE("the disc's level99: a switch of player 1 during the intro gives his crew only the end's follow",
          "[disc][story][scenes][crew]") {
    // Expected: scenes.md#gang, confirmed (code): Human_MakePlayer (0x00229c40) dispatches the follow at once, and the
    // dispatcher (0x0041c4e0) refuses it while the scene camera is current or the menu is locked; SceneTask_End
    // (0x0039f450) gives one follow at the cinematic's end (a player, player 2's human, is still bound in it), and
    // nothing gives another after it. So the new player 1's crew gets one new tactic, the end's.
    std::optional<coney::io::Wad> wad = openDisc();
    if (!wad) {
        SKIP("CONEY_DISC is not set: no disc to check");
    }
    playIntro(*wad, [](IntroPlay& play) {
        // As in the test above: every command enabled, the automatic commands off, hold his last, his menu locked.
        coney::CharacterRules& rules = play.scripts.state().characters;
        rules.warriorCommands.at(0).fill(true);
        play.scripts.state().autoSwitch = false;
        rules.lastWarriorCommand.at(0) = 3;
        play.scripts.state().story.menuLocked.at(0) = true;
        // Player 1 handed, while the intro plays, to another human the scene does not hold.
        const coney::ai::ScriptedBrains* scripted = play.gameplay.scripted();
        REQUIRE(scripted != nullptr);
        const coney::HumanCreation* chief = play.scripts.humans().player(1);
        REQUIRE(chief != nullptr);
        double next = 0.0;
        for (const coney::HumanCreation& human : play.scripts.humans().all()) {
            if (human.handle != chief->handle && !play.system.inScene(human.handle) &&
                scripted->brain(human.handle) != nullptr && scripted->brain(human.handle)->gang() != nullptr) {
                next = human.handle;
                break;
            }
        }
        REQUIRE(next != 0.0);
        play.scripts.humans().setPlayer(1, next);
        const coney::ai::Brain* brain = scripted->brain(next);
        REQUIRE(brain != nullptr);
        const coney::ai::Gang* crew = brain->gang();
        REQUIRE(crew != nullptr);
        const std::uint32_t tacticsBefore = crew->tacticsSet();

        // To the update in which the scene ends, then 3 s more.
        const std::uint64_t endedBefore = play.system.stats().ended;
        int steps = 0;
        while (play.system.stats().ended == endedBefore && steps < 2400) {
            play.stack.runUntilEmpty(play.timer, {}, 1);
            ++steps;
        }
        REQUIRE(play.system.stats().ended == endedBefore + 1);
        play.stack.runUntilEmpty(play.timer, {}, 90);
        CHECK(play.scripts.scripts().errors() == 0);
        CHECK(crew->tacticsSet() == tacticsBefore + 1);
        std::printf("  level99 l99_c1, player 1 switched: ended after %d more steps; %u new tactics 3 s later\n", steps,
                    static_cast<unsigned>(crew->tacticsSet() - tacticsBefore));
    });
}

TEST_CASE("the disc's level99: a switch of player 1 after the intro starts his crew's follow in the same update",
          "[disc][story][scenes][crew]") {
    // Expected: scenes.md#gang, confirmed (code): Human_MakePlayer (0x00229c40) dispatches the follow forced and runs
    // the gang update with 1 (0x00166708) right after it, so the new follow tactic has started when the call returns.
    std::optional<coney::io::Wad> wad = openDisc();
    if (!wad) {
        SKIP("CONEY_DISC is not set: no disc to check");
    }
    playIntro(*wad, [](IntroPlay& play) {
        // The intro played to its end and 1 s more, the automatic commands off so only the switch gives a command.
        play.scripts.state().autoSwitch = false;
        const std::uint64_t endedBefore = play.system.stats().ended;
        int steps = 0;
        while (play.system.stats().ended == endedBefore && steps < 2400) {
            play.stack.runUntilEmpty(play.timer, {}, 1);
            ++steps;
        }
        REQUIRE(play.system.stats().ended == endedBefore + 1);
        play.stack.runUntilEmpty(play.timer, {}, 30);
        // Every command enabled, hold his last and his menu unlocked, so the switch's follow is not refused.
        coney::CharacterRules& rules = play.scripts.state().characters;
        rules.warriorCommands.at(0).fill(true);
        rules.lastWarriorCommand.at(0) = 3;
        play.scripts.state().story.menuLocked.at(0) = false;
        // Player 1 handed to another human with a gang, one no player was made with (player 2's would answer for
        // player 2's commands).
        const coney::ai::ScriptedBrains* scripted = play.gameplay.scripted();
        REQUIRE(scripted != nullptr);
        const coney::HumanCreation* chief = play.scripts.humans().player(1);
        REQUIRE(chief != nullptr);
        double next = 0.0;
        for (const coney::HumanCreation& human : play.scripts.humans().all()) {
            if (human.handle != chief->handle && human.playerIndex == 0 && scripted->brain(human.handle) != nullptr &&
                scripted->brain(human.handle)->gang() != nullptr) {
                next = human.handle;
                break;
            }
        }
        REQUIRE(next != 0.0);
        play.scripts.humans().setPlayer(1, next);
        const coney::ai::Gang* crew = scripted->brain(next)->gang();
        REQUIRE(crew != nullptr);

        // One update: the switch's follow is his last command, and its tactic is in force and started.
        play.stack.runUntilEmpty(play.timer, {}, 1);
        CHECK(play.scripts.scripts().errors() == 0);
        CHECK(rules.lastWarriorCommand.at(0) == 0);
        const coney::ai::Tactic* tactic = crew->tactic();
        REQUIRE(tactic != nullptr);
        CHECK(tactic->type() == kFollowTacticType);
        CHECK(tactic->started());
        std::printf("  level99 after l99_c1, player 1 switched: tactic %#x started %d\n",
                    static_cast<unsigned>(tactic->type()), tactic->started() ? 1 : 0);
    });
}
