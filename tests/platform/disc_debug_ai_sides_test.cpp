// SPDX-License-Identifier: GPL-3.0-or-later

// Checks against the player's own disc that the debug menus' AI fighters page spawns fighters on the side picked
// (docs/guides/debug-menu.md): in `level80` played in the game session as `coney --play-level level80 --checkpoint 2`
// plays it (support/disc_session.h), where the player has his Warriors, "Side" is set to Ally and then to Enemy and
// "Spawn fighters" run through the page as a person runs them. The allies are in the player's camp as the original's
// gang tests define it, the enemies in his enemies', and over twenty seconds the two sides fight each other and
// neither takes on its own side, the player or his crew. It runs only when the environment variable CONEY_DISC names
// the disc and skips otherwise; it prints counts only (LEGAL.md).

#include <algorithm>
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
#include "ai/fighter_side.h"
#include "ai/gangs.h"
#include "characters/character_types.h"
#include "core/game_timer.h"
#include "debug/debug_session.h"
#include "debug/menu_model.h"
#include "debug/play_controls.h"
#include "debug/tunables.h"
#include "fileio/wad.h"
#include "gamemodes/game_mode_stack.h"
#include "gamemodes/level_start.h"
#include "human/human.h"
#include "human/human_flags.h"
#include "platform/game_session.h"
#include "platform/game_session_debug.h"
#include "platform/play_level_controls.h"
#include "platform/play_level_mode.h"
#include "platform/render_engine.h"
#include "platform/sandbox_play_scenery.h"
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

// How many fighters of each side one click makes, and how long the fight is watched (20 s of frames).
constexpr int kAmount = 4;
constexpr std::uint64_t kWatchFrames = 600;

// The brains of the spawned fighters of `side`.
std::vector<coney::ai::Brain*> brainsOf(coney::ai::AiHumans& fighters, coney::ai::FighterSide side) {
    std::vector<coney::ai::Brain*> found;
    for (const coney::ai::AiHuman& entry : fighters.humans()) {
        if (entry.debugSpawned && !entry.removed && entry.side == side) {
            if (coney::ai::Brain* brain = fighters.brainOf(*entry.human); brain != nullptr) {
                found.push_back(brain);
            }
        }
    }
    return found;
}

// Whether `brain` is one of `list`.
bool among(const std::vector<coney::ai::Brain*>& list, const coney::ai::Brain* brain) {
    for (const coney::ai::Brain* other : list) {
        if (other == brain) {
            return true;
        }
    }
    return false;
}

// The brains of the player's gang other than the spawned allies: him and his crew.
std::vector<coney::ai::Brain*> campOf(coney::ai::Brains& brains, const coney::ai::Brain& player) {
    std::vector<coney::ai::Brain*> camp;
    for (std::size_t i = 0; i < brains.size(); ++i) {
        coney::ai::Brain& brain = brains.at(i);
        if (brain.gang() != nullptr && brain.gang() == player.gang()) {
            camp.push_back(&brain);
        }
    }
    return camp;
}

// How the two sides' brains pointed at each other over the watched frames.
struct Tally {
    std::size_t allyOnEnemy = 0;     // frames an ally's target was an enemy fighter
    std::size_t enemyOnAlly = 0;     // frames an enemy's target was an ally
    std::size_t enemyOnCamp = 0;     // frames an enemy's target was the player or his crew
    std::size_t allyOnOwn = 0;       // frames an ally's target or listed enemy was an ally, the player or his crew
    std::size_t enemyOnOwn = 0;      // frames an enemy's target or listed enemy was an enemy
    std::size_t playerListsAlly = 0; // frames the player's brain listed an ally among his enemies
};

} // namespace

// The AI fighters page spawns an ally as a friend of the player and a fighter of his enemies, so with both sides
// spawned they fight each other and never their own.
TEST_CASE("allies and enemies spawned from the AI fighters page fight each other and never their own side",
          "[disc][story][debug][fighters]") {
    // Expected: ai-gangs.md#aggro-on-sight, confirmed (code)
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
    // The player cannot die while the fight is watched: the test is about the fighters.
    fighters.playerBrain().human().setFlag(coney::human::flag::kGod, true);

    // The page as a person uses it: the side picked, then the amount, then the action; allies first, then enemies.
    const auto page = debug.model().openPage("AI fighters");
    REQUIRE(page != nullptr);
    const coney::debug::MenuItem* side = page->find("Side");
    const coney::debug::MenuItem* amount = page->find("Amount");
    const coney::debug::MenuItem* spawn = page->find("Spawn fighters");
    REQUIRE(side != nullptr);
    REQUIRE(amount != nullptr);
    REQUIRE(spawn != nullptr);
    CHECK(side->choices == std::vector<std::string>{"Enemy", "Ally", "Neutral"});
    CHECK(side->getChoice() == 0);
    amount->setNumber(kAmount);
    side->setChoice(1);
    spawn->run();
    side->setChoice(0);
    spawn->run();
    run.run(2);

    const std::vector<coney::ai::Brain*> allies = brainsOf(fighters, coney::ai::FighterSide::Ally);
    const std::vector<coney::ai::Brain*> enemies = brainsOf(fighters, coney::ai::FighterSide::Enemy);
    REQUIRE(allies.size() == kAmount);
    REQUIRE(enemies.size() == kAmount);
    coney::ai::Brain& player = fighters.playerBrain();
    REQUIRE(player.gang() != nullptr);
    const std::vector<coney::ai::Brain*> camp = campOf(fighters.brains(), player);

    // The sides are made of the original's own gang tests: an ally is the player's friend and not his enemy, an enemy
    // the other way round, and the two fighter sides are enemies.
    for (const coney::ai::Brain* ally : allies) {
        REQUIRE(ally->gang() != nullptr);
        CHECK(coney::ai::Gangs::friends(ally->gang(), player.gang()));
        CHECK_FALSE(coney::ai::Gangs::enemies(ally->gang(), player.gang()));
    }
    for (const coney::ai::Brain* enemy : enemies) {
        REQUIRE(enemy->gang() != nullptr);
        CHECK_FALSE(coney::ai::Gangs::friends(enemy->gang(), player.gang()));
        CHECK(coney::ai::Gangs::enemies(enemy->gang(), player.gang()));
        CHECK(coney::ai::Gangs::enemies(enemy->gang(), allies.front()->gang()));
        CHECK(coney::ai::Gangs::enemies(allies.front()->gang(), enemy->gang()));
    }

    // Twenty seconds of play, each frame noting whom every fighter has as its target or lists as an enemy.
    Tally tally;
    std::size_t enemyHealthBefore = 0;
    for (const coney::ai::Brain* enemy : enemies) {
        enemyHealthBefore += static_cast<std::size_t>(enemy->human().health().value());
    }
    for (std::uint64_t frame = 0; frame < kWatchFrames; ++frame) {
        run.run(1);
        for (const coney::ai::Brain* ally : allies) {
            tally.allyOnEnemy += among(enemies, ally->target()) ? 1U : 0U;
            const bool own = among(allies, ally->target()) || among(camp, ally->target());
            bool listsOwn = false;
            for (const coney::ai::Brain* listed : ally->enemies()) {
                listsOwn = listsOwn || among(allies, listed) || among(camp, listed);
            }
            tally.allyOnOwn += own || listsOwn ? 1U : 0U;
        }
        for (const coney::ai::Brain* enemy : enemies) {
            tally.enemyOnAlly += among(allies, enemy->target()) ? 1U : 0U;
            tally.enemyOnCamp += among(camp, enemy->target()) && !among(allies, enemy->target()) ? 1U : 0U;
            bool listsOwn = false;
            for (const coney::ai::Brain* listed : enemy->enemies()) {
                listsOwn = listsOwn || among(enemies, listed);
            }
            tally.enemyOnOwn += among(enemies, enemy->target()) || listsOwn ? 1U : 0U;
        }
        for (const coney::ai::Brain* listed : player.enemies()) {
            if (among(allies, listed)) {
                ++tally.playerListsAlly;
            }
        }
    }
    std::size_t enemyHealthAfter = 0;
    for (const coney::ai::Brain* enemy : enemies) {
        enemyHealthAfter += static_cast<std::size_t>(std::max(0, enemy->human().health().value()));
    }
    std::printf("fighter sides: level80 checkpoint 2, %d allies and %d enemies, %llu frames; ally->enemy %zu, "
                "enemy->ally %zu, enemy->player or crew %zu; ally on own side %zu, enemy on own side %zu, player "
                "listed an ally %zu; the enemies' health %zu -> %zu\n",
                kAmount, kAmount, static_cast<unsigned long long>(kWatchFrames), tally.allyOnEnemy, tally.enemyOnAlly,
                tally.enemyOnCamp, tally.allyOnOwn, tally.enemyOnOwn, tally.playerListsAlly, enemyHealthBefore,
                enemyHealthAfter);

    // They fight each other: the allies go for the enemies and the enemies for the allies, and the allies hurt them.
    CHECK(tally.allyOnEnemy > 0);
    CHECK(tally.enemyOnAlly > 0);
    CHECK(enemyHealthAfter < enemyHealthBefore);
    // And neither takes its own side, nor the allies the player or his crew; the player does not list an ally.
    CHECK(tally.allyOnOwn == 0);
    CHECK(tally.enemyOnOwn == 0);
    CHECK(tally.playerListsAlly == 0);

    // Clear fighters still removes both sides, and nothing else.
    const coney::debug::MenuItem* clear = page->find("Clear fighters");
    REQUIRE(clear != nullptr);
    clear->run();
    run.run(10);
    CHECK(brainsOf(fighters, coney::ai::FighterSide::Ally).empty());
    CHECK(brainsOf(fighters, coney::ai::FighterSide::Enemy).empty());
    CHECK(play->player().padControlled());
}

// A fighter keeps its side when the player's character changes: the fighters are made again round the new player, each
// in a gang of the side it had, so allies are still his friends and enemies still his enemies.
TEST_CASE("fighters of each side keep their side through Change character", "[disc][debug][fighters]") {
    // Expected: unverified, #924 (Coney's own debug tool)
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
    auto made = coney::platform::createPlayInSandbox(
        **engine, *wad, std::move(*world), std::nullopt, [](std::string_view) {}, setup);
    REQUIRE(made.has_value());
    coney::platform::PlayLevelMode& mode = **made;
    coney::GameModeStack modes;
    coney::GameTimer timer;
    timer.setFixedStep(true);
    modes.push(mode);
    (void)modes.runUntilEmpty(timer, {}, 10);

    const std::vector<std::string> models = mode.fighterModels();
    REQUIRE_FALSE(models.empty());
    constexpr int kEach = 2;
    for (const coney::ai::FighterSide side : coney::ai::kFighterSides) {
        for (int i = 0; i < kEach; ++i) {
            REQUIRE(mode.spawnFighterAs(
                coney::debug::spotAhead(mode.playerFeet(), mode.playerHeadingDegrees(), 4.0F + static_cast<float>(i)),
                mode.playerHeadingDegrees() + 180.0F, models.front(), side));
        }
    }
    (void)modes.runUntilEmpty(timer, {}, 10);
    REQUIRE(mode.changeCharacter(40));
    (void)modes.runUntilEmpty(timer, {}, 10);

    // Each side still has its fighters, and each is in a gang that stands to the new player's gang as the side says.
    coney::ai::AiHumans& fighters = mode.fighters();
    const coney::ai::Gang* players = fighters.playerBrain().gang();
    REQUIRE(players != nullptr);
    for (const coney::ai::FighterSide side : coney::ai::kFighterSides) {
        const std::vector<coney::ai::Brain*> brains = brainsOf(fighters, side);
        CHECK(brains.size() == kEach);
        for (const coney::ai::Brain* brain : brains) {
            REQUIRE(brain->gang() != nullptr);
            CHECK(coney::ai::Gangs::friends(brain->gang(), players) == (side == coney::ai::FighterSide::Ally));
            CHECK(coney::ai::Gangs::enemies(brain->gang(), players) == (side == coney::ai::FighterSide::Enemy));
        }
    }
    mode.clearFighters();
    for (const coney::ai::FighterSide side : coney::ai::kFighterSides) {
        CHECK(brainsOf(fighters, side).empty());
    }
}
