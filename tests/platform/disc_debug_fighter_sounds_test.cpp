// SPDX-License-Identifier: GPL-3.0-or-later

// Checks against the player's own disc that the fighters the debug menus' AI fighters page spawns make their sounds as
// the level's scripts' humans do (docs/guides/debug-menu.md, issue 923): in `level80` played in the game
// session as `coney --play-level level80 --checkpoint 2` plays it (support/disc_session.h), a stock fighter and one
// drawn as another model are spawned beside the player and sent at him. Each is held by a handle as a script's human
// is, has the character type it is drawn as (its voice set), and its animation sounds reach the game's sound; once
// cleared, its handle is gone. It runs only when the environment variable CONEY_DISC names the disc and skips
// otherwise; it prints counts only (LEGAL.md).

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "ai/ai_humans.h"
#include "ai/brain.h"
#include "ai/brains.h"
#include "ai/scripted_brains.h"
#include "audio/game_sound.h"
#include "characters/character_choice.h"
#include "debug/play_controls.h"
#include "fileio/wad.h"
#include "gamemodes/gameplay_mode.h"
#include "human/human.h"
#include "platform/audio_output.h"
#include "platform/game_session.h"
#include "platform/play_level_mode.h"
#include "support/disc_play_fixtures.h"
#include "support/disc_session.h"

namespace {

// How long anything here may take: 60 s of frames.
constexpr std::uint64_t kLimit = 1800;

// A pad script that taps cross every second for the first 20 s of play: it skips the level's opening scene.
constexpr const char* kSkipScene = "30 tap cross\n60 tap cross\n90 tap cross\n120 tap cross\n150 tap cross\n"
                                   "180 tap cross\n210 tap cross\n240 tap cross\n270 tap cross\n300 tap cross\n"
                                   "330 tap cross\n360 tap cross\n390 tap cross\n420 tap cross\n450 tap cross\n"
                                   "480 tap cross\n510 tap cross\n540 tap cross\n570 tap cross\n600 tap cross\n";

// The last human `fighters` made.
const coney::human::Human& newest(const coney::ai::AiHumans& fighters) {
    REQUIRE_FALSE(fighters.humans().empty());
    return *fighters.humans().back().human;
}

} // namespace

// The fighters the page spawned had a brain but no handle: gameplay passed on only the scripts' humans' sounds, so
// their footsteps, swooshes and grunts (and the hit sounds of the strikes they took) were dropped, and with no
// character type they had no voice set.
TEST_CASE("the AI fighters page's fighters make their sounds and have the type they are drawn as",
          "[disc][story][debug][fighters][audio]") {
    std::optional<coney::io::Wad> wad = coney::test::openDisc();
    if (!wad) {
        SKIP("CONEY_DISC is not set: no disc to check");
    }
    coney::test::DiscSession run(*wad, kSkipScene);
    run.session().startAtLevel("level80", 2);
    REQUIRE(run.runUntilPlay());
    REQUIRE(run.runUntil(
        [&run] { return run.session().play() != nullptr && run.session().play()->player().padControlled(); }, kLimit));
    run.run(30);
    coney::platform::PlayLevelMode* play = run.session().play();
    REQUIRE(play != nullptr);
    const coney::ai::ScriptedBrains* scripted = run.session().gameplay().scripted();
    REQUIRE(scripted != nullptr);
    coney::ai::AiHumans& fighters = play->fighters();

    // A stock fighter (the player's look and type), then one drawn as the first model of another type.
    const std::vector<std::string> models = play->fighterModels();
    REQUIRE_FALSE(models.empty());
    int modelType = -1;
    for (const coney::characters::CharacterChoice& choice : play->characterChoices()) {
        if (choice.model == models.front()) {
            modelType = choice.type;
            break;
        }
    }
    REQUIRE(modelType >= 0);
    REQUIRE(play->spawnFighter(coney::debug::spotAhead(play->playerFeet(), play->playerHeadingDegrees(), 3.0F),
                               play->playerHeadingDegrees() + 180.0F));
    const coney::human::Human& stock = newest(fighters);
    REQUIRE(play->spawnFighterAs(coney::debug::spotAhead(play->playerFeet(), play->playerHeadingDegrees(), 4.0F),
                                 play->playerHeadingDegrees() + 180.0F, models.front()));
    const coney::human::Human& drawn = newest(fighters);
    const coney::ai::Brain* stockBrain = fighters.brains().find(stock);
    const coney::ai::Brain* drawnBrain = fighters.brains().find(drawn);
    REQUIRE(stockBrain != nullptr);
    REQUIRE(drawnBrain != nullptr);

    // Held by handles of their own, as the scripts' humans are, with the type each is drawn as.
    CHECK(stockBrain->handle() != 0.0);
    CHECK(drawnBrain->handle() != 0.0);
    CHECK(stockBrain->handle() != drawnBrain->handle());
    CHECK(scripted->brain(stockBrain->handle()) == stockBrain);
    CHECK(scripted->brain(drawnBrain->handle()) == drawnBrain);
    CHECK(stockBrain->characterClass() == play->playerType());
    CHECK(drawnBrain->characterClass() == modelType);

    // Sent at the player: the walk and the fight ask for their animation sounds.
    fighters.fightPlayer(stock);
    fighters.fightPlayer(drawn);
    const coney::audio::GameSound& game = run.audio().game();
    REQUIRE(run.runUntil(
        [&] { return game.lastAnimSound(stockBrain->handle()) != 0 && game.lastAnimSound(drawnBrain->handle()) != 0; },
        kLimit));
    std::printf("debug fighter sounds: level80 checkpoint 2, stock type %d last SA %u, drawn type %d last SA %u\n",
                stockBrain->characterClass(), game.lastAnimSound(stockBrain->handle()), drawnBrain->characterClass(),
                game.lastAnimSound(drawnBrain->handle()));

    // Cleared: the handles name no one, and play goes on.
    const double stockHandle = stockBrain->handle();
    const double drawnHandle = drawnBrain->handle();
    play->clearFighters();
    run.run(30);
    CHECK(scripted->brain(stockHandle) == nullptr);
    CHECK(scripted->brain(drawnHandle) == nullptr);
    CHECK(play->player().padControlled());
}
