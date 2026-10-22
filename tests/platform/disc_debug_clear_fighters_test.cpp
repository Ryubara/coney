// SPDX-License-Identifier: GPL-3.0-or-later

// Checks against the player's own disc that the debug menus' AI fighters page clears only the fighters it spawned
// (docs/guides/debug-menu.md): in `level80` played in the game session as `coney --play-level level80 --checkpoint 2`
// plays it (support/disc_session.h), where the player has his Warriors and the level's scripts hold their humans by
// handle, "Spawn fighters" and "Clear fighters" are run through the page as a person runs them. The spawned fighters
// leave; the player, his crew and every human the level's scripts made stay, and play goes on. It runs only when the
// environment variable CONEY_DISC names the disc and skips otherwise; it prints counts only (LEGAL.md).

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "ai/ai_config.h"
#include "ai/ai_humans.h"
#include "ai/brain.h"
#include "ai/brains.h"
#include "characters/character_types.h"
#include "core/game_timer.h"
#include "debug/debug_session.h"
#include "debug/fighter_spawn.h"
#include "debug/menu_model.h"
#include "debug/play_controls.h"
#include "debug/tunables.h"
#include "fileio/wad.h"
#include "gamemodes/game_mode_stack.h"
#include "gamemodes/gameplay_mode.h"
#include "gamemodes/level_start.h"
#include "human/human.h"
#include "platform/game_session.h"
#include "platform/game_session_debug.h"
#include "platform/play_level_controls.h"
#include "platform/play_level_mode.h"
#include "platform/render_engine.h"
#include "sandbox/sandbox_world.h"
#include "scripting/config_strings.h"
#include "support/disc_play_fixtures.h"
#include "support/disc_session.h"
#include "support/test_paths.h"

namespace {

// How long anything here may take: 60 s of frames.
constexpr std::uint64_t kLimit = 1800;

// A pad script that taps cross every second for the first 20 s of play: it skips the level's opening scene.
constexpr const char* kSkipScene = "30 tap cross\n60 tap cross\n90 tap cross\n120 tap cross\n150 tap cross\n"
                                   "180 tap cross\n210 tap cross\n240 tap cross\n270 tap cross\n300 tap cross\n"
                                   "330 tap cross\n360 tap cross\n390 tap cross\n420 tap cross\n450 tap cross\n"
                                   "480 tap cross\n510 tap cross\n540 tap cross\n570 tap cross\n600 tap cross\n";

// How many fighters one click of "Spawn fighters" makes here.
constexpr int kAmount = 5;

// The humans of `fighters` that are still in the world, in their order.
std::vector<const coney::human::Human*> liveHumans(const coney::ai::AiHumans& fighters) {
    std::vector<const coney::human::Human*> live;
    for (const coney::ai::AiHuman& entry : fighters.humans()) {
        if (!entry.removed) {
            live.push_back(entry.human.get());
        }
    }
    return live;
}

// Whether `human` is among `list`.
bool contains(const std::vector<const coney::human::Human*>& list, const coney::human::Human* human) {
    for (const coney::human::Human* other : list) {
        if (other == human) {
            return true;
        }
    }
    return false;
}

// The brains of the player's gang that are Warriors: his crew.
std::size_t crewSize(const coney::ai::Brains& brains) {
    const coney::ai::Brain* chief = nullptr;
    for (std::size_t i = 0; i < brains.size(); ++i) {
        if (brains.at(i).type() == coney::ai::BrainType::Player) {
            chief = &brains.at(i);
        }
    }
    std::size_t crew = 0;
    for (std::size_t i = 0; i < brains.size() && chief != nullptr; ++i) {
        const coney::ai::Brain& brain = brains.at(i);
        if (brain.type() == coney::ai::BrainType::Warrior && brain.gang() == chief->gang()) {
            ++crew;
        }
    }
    return crew;
}

} // namespace

// "Clear fighters" took every AI human of the level, the player's crew and the humans the level's scripts hold by
// handle among them, and the game ended on the next script call that named one.
TEST_CASE("the AI fighters page clears only the fighters it spawned, not the crew or the level's humans",
          "[disc][story][debug][fighters]") {
    std::optional<coney::io::Wad> wad = coney::test::openDisc();
    if (!wad) {
        SKIP("CONEY_DISC is not set: no disc to check");
    }
    coney::test::DiscSession run(*wad, kSkipScene);
    run.session().startAtLevel("level80", 2);
    REQUIRE(run.runUntilPlay());
    // The debug menus as `coney` wires them, with the play mode as `coney --play-level` gives it to them.
    coney::debug::DebugServices services;
    coney::platform::connectDebugServices(services, [&run] { return &run.session(); });
    coney::platform::PlayLevelControls controls;
    services.play = [&run, &controls] { return controls.controlsOf(run.session().play()); };
    coney::debug::TunableRegistry tunables;
    coney::debug::DebugSession debug(tunables, services, nullptr);

    REQUIRE(run.runUntil(
        [&run] { return run.session().play() != nullptr && run.session().play()->player().padControlled(); }, kLimit));
    run.run(30);
    coney::platform::PlayLevelMode* play = run.session().play();
    REQUIRE(play != nullptr);
    coney::ai::AiHumans& fighters = play->fighters();
    const std::vector<const coney::human::Human*> levelHumans = liveHumans(fighters);
    const std::size_t crew = crewSize(fighters.brains());
    const std::size_t brainsBefore = fighters.brains().size();
    REQUIRE(crew > 0);
    REQUIRE_FALSE(levelHumans.empty());

    // "Spawn fighters" as a person runs it: the amount set, then the action.
    const auto page = debug.model().openPage("AI fighters");
    REQUIRE(page != nullptr);
    const coney::debug::MenuItem* amount = page->find("Amount");
    const coney::debug::MenuItem* spawn = page->find("Spawn fighters");
    const coney::debug::MenuItem* clear = page->find("Clear fighters");
    REQUIRE(amount != nullptr);
    REQUIRE(spawn != nullptr);
    REQUIRE(clear != nullptr);
    amount->setNumber(kAmount);
    spawn->run();
    run.run(10);
    const std::vector<const coney::human::Human*> withSpawned = liveHumans(fighters);
    REQUIRE(withSpawned.size() == levelHumans.size() + kAmount);

    // Cleared: the spawned ones leave, nothing else does, and play goes on.
    clear->run();
    run.run(10);
    const std::vector<const coney::human::Human*> after = liveHumans(fighters);
    std::size_t spawnedLeft = 0;
    for (const coney::human::Human* human : withSpawned) {
        if (!contains(levelHumans, human) && contains(after, human)) {
            ++spawnedLeft;
        }
    }
    std::size_t levelLost = 0;
    for (const coney::human::Human* human : levelHumans) {
        if (!contains(after, human)) {
            ++levelLost;
        }
    }
    std::printf("clear fighters: level80 checkpoint 2, %zu level humans and %zu crew; %d spawned, %zu left after the "
                "clear, %zu level humans lost\n",
                levelHumans.size(), crew, kAmount, spawnedLeft, levelLost);
    CHECK(spawnedLeft == 0);
    CHECK(levelLost == 0);
    CHECK(after.size() == levelHumans.size());
    CHECK(crewSize(fighters.brains()) == crew);
    CHECK(fighters.brains().size() == brainsBefore);
    CHECK(play->fighterCount() == levelHumans.size());
    CHECK(play->player().padControlled());

    // A second clear has nothing to take, and the level's scripts run on over the humans they hold.
    clear->run();
    run.run(300);
    CHECK(liveHumans(fighters).size() == levelHumans.size());
    CHECK(crewSize(fighters.brains()) == crew);
}

// In the sandbox's fight yard the layout puts fighters of its own; Clear fighters takes the ones the page spawned and
// leaves those, and a change of the player's character neither brings the cleared ones back nor loses the marking of
// the others.
TEST_CASE("the AI fighters page leaves the layout's fighters, and a change of character keeps the count",
          "[disc][debug][fighters]") {
    std::optional<coney::io::Wad> wad = coney::test::openDisc();
    if (!wad) {
        SKIP("CONEY_DISC is not set: no disc to check");
    }
    auto engine = coney::platform::RenderEngine::start(coney::platform::RenderBackend::Null, {});
    REQUIRE(engine.has_value());
    // The sandbox's setup as main makes it: level99's configuration.
    const coney::LevelScriptRun scriptRun =
        coney::runLevelScriptAlone(coney::script::wadScriptSource(*wad), "level99", 1, {});
    coney::platform::PlayerSetup setup;
    setup.ai = coney::ai::aiConfigFrom(scriptRun.recorded);
    setup.types = coney::characters::CharacterTypes::fromRecorded(scriptRun.recorded);
    auto world = coney::sandbox::SandboxWorld::load(coney::test::assetsDir() / "sandbox", "combat");
    REQUIRE(world.has_value());
    auto made = coney::platform::PlayLevelMode::createInSandbox(
        **engine, *wad, std::move(*world), std::nullopt, [](std::string_view) {}, setup);
    REQUIRE(made.has_value());
    coney::platform::PlayLevelMode& mode = **made;
    coney::GameModeStack modes;
    coney::GameTimer timer;
    timer.setFixedStep(true);
    modes.push(mode);
    (void)modes.runUntilEmpty(timer, {}, 10);

    const std::size_t layout = mode.fighterCount();
    const std::vector<std::string> models = mode.fighterModels();
    REQUIRE_FALSE(models.empty());
    for (int i = 0; i < kAmount; ++i) {
        REQUIRE(mode.spawnFighterAs(coney::debug::spotAhead(mode.playerFeet(), mode.playerHeadingDegrees(), 4.0F + i),
                                    mode.playerHeadingDegrees() + 180.0F,
                                    models[static_cast<std::size_t>(i) % models.size()]));
    }
    REQUIRE(mode.fighterCount() == layout + kAmount);
    (void)modes.runUntilEmpty(timer, {}, 10);

    mode.clearFighters();
    std::printf("clear fighters: sandbox combat, %zu from the layout, %d spawned, %zu left after the clear\n", layout,
                kAmount, mode.fighterCount());
    CHECK(mode.fighterCount() == layout);
    (void)modes.runUntilEmpty(timer, {}, 15);

    // Spawned again, then the player's character changes (the fighters are made again round the new player): the
    // cleared ones stay gone, the others are still the menu's to clear.
    REQUIRE(mode.spawnFighter(coney::debug::spotAhead(mode.playerFeet(), mode.playerHeadingDegrees(), 4.0F),
                              mode.playerHeadingDegrees() + 180.0F));
    REQUIRE(mode.fighterCount() == layout + 1);
    REQUIRE(mode.changeCharacter(40));
    CHECK(mode.fighterCount() == layout + 1);
    (void)modes.runUntilEmpty(timer, {}, 15);
    mode.clearFighters();
    CHECK(mode.fighterCount() == layout);
    (void)modes.runUntilEmpty(timer, {}, 15);
}
