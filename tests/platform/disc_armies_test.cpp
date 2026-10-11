// SPDX-License-Identifier: GPL-3.0-or-later

// Checks against the player's own disc that the debug menus lead to Armies of the Night's first stage and not to a
// black screen (owner item 19, issue #85; docs/research/armies.md#coney-fixes). The level table's record `armies` has
// no level file, so the Levels page leaves it out and a jump to it is refused; `level60` checkpoint 1, which the
// Missions page now lists, is jumped to from a level in play and plays its intro scene to the end with the picture
// clear. The session runs as `coney` runs it (support/disc_session.h), the pad idle. It runs only when the environment
// variable CONEY_DISC names the disc and skips otherwise; it prints counts only (LEGAL.md).

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "debug/debug_session.h"
#include "debug/menu_model.h"
#include "debug/tunables.h"
#include "fileio/wad.h"
#include "gamemodes/start_up_flow.h"
#include "platform/game_session.h"
#include "platform/play_level_mode.h"
#include "support/disc_play_fixtures.h"
#include "support/disc_session.h"
#include "warriors/game_state.h"

namespace {

// How many of the session's log lines start with `text`.
std::size_t linesStarting(const std::vector<std::string>& lines, std::string_view text) {
    return static_cast<std::size_t>(
        std::ranges::count_if(lines, [text](const std::string& line) { return line.starts_with(text); }));
}

// How many frames of `level60` are watched after the jump: its intro scene ends about 21 s in
// (docs/research/armies.md#first-frames).
constexpr std::uint64_t kWatchFrames = 1200;

} // namespace

TEST_CASE("the disc's level60: the debug menus reach Armies of the Night's first stage, not a black screen",
          "[disc][story][armies]") {
    std::optional<coney::io::Wad> wad = coney::test::openDisc();
    if (!wad) {
        SKIP("CONEY_DISC is not set: no disc to check");
    }
    coney::test::DiscSession run(*wad, "");
    run.session().startAtLevel("level99", 1);
    REQUIRE(run.runUntilPlay());
    run.run(100);

    // The Levels page as `coney` wires it: every record of the level table but the one with no level file.
    coney::debug::DebugServices services;
    coney::platform::connectDebugServices(services, [&run] { return &run.session(); });
    coney::debug::TunableRegistry tunables;
    coney::debug::DebugSession debug(tunables, services, nullptr);
    const auto levels = debug.model().openPage("Levels");
    REQUIRE(levels != nullptr);
    const bool listsLevel60 = levels->find("024 level60") != nullptr;
    const bool listsArmies = levels->find("101 armies") != nullptr;

    // A jump to the record is refused, so the level in play goes on.
    const bool armiesTaken = run.session().jumpToLevel("armies", 1);
    run.run(30);
    const bool stillPlaying = run.session().inPlay();

    // The Missions page's jump to Armies of the Night 1, checkpoint 1: its load, intro scene and play.
    REQUIRE(run.session().jumpToLevel("level60", 1));
    run.run(5);
    REQUIRE(run.runUntilPlay());
    run.run(kWatchFrames);
    const coney::platform::PlayLevelMode* play = run.session().play();
    REQUIRE(play != nullptr);
    const float fade = play->stage().fadeLevel();
    const std::size_t scenes = linesStarting(run.log(), "scene sound: started");
    const std::size_t refused = linesStarting(run.log(), "level flow: jump to armies: no armies.lev");
    std::printf("  level60: Levels lists level60 %d, armies %d; armies jump taken %d (%zu refusals), level99 still "
                "playing %d; level60 in play %d, level %zu, scenes started %zu, fade %.2f\n",
                listsLevel60 ? 1 : 0, listsArmies ? 1 : 0, armiesTaken ? 1 : 0, refused, stillPlaying ? 1 : 0,
                run.session().inPlay() ? 1 : 0, run.session().flow().state().currentLevel, scenes, fade);
    CHECK(listsLevel60);
    CHECK_FALSE(listsArmies);
    CHECK_FALSE(armiesTaken);
    CHECK(refused == 1);
    CHECK(stillPlaying);
    // level60 plays (record 24) after its intro scene, with nothing over the picture.
    CHECK(run.session().inPlay());
    CHECK(run.session().flow().state().currentLevel == 24);
    CHECK(scenes >= 2); // level99's intro scene, then level60's `l60_intro`
    CHECK(fade == 0.0F);
}
