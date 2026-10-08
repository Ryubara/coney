// SPDX-License-Identifier: GPL-3.0-or-later

// Checks against the player's own disc that a level is set up one way however it is reached: `level99` at checkpoint
// 2 started directly (GameSession::startAtLevel(), `coney --play-level level99 --checkpoint 2`) and jumped to from the
// story's front end (GameSession::startStory() and the debug menus' jump) run the same modes and services. Both
// pause with START, and closing the pause gives the level its own sound bank back (glass and material sounds need it,
// docs/research/pause.md); the debug menus' Lua console and Cheats page call into the level's own scripts. It runs
// only when the environment variable CONEY_DISC names the disc and skips otherwise; it prints counts only (LEGAL.md).

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <string>
#include <string_view>

#include <catch2/catch_test_macros.hpp>

#include "debug/debug_session.h"
#include "debug/tunables.h"
#include "fileio/wad.h"
#include "gamemodes/pause_mode.h"
#include "platform/game_session.h"
#include "support/disc_play_fixtures.h"
#include "support/disc_session.h"
#include "warriors/game_state.h"

namespace {

// START during the level's first seconds (its street scene: START pauses a scene too), triangle to resume.
constexpr std::string_view kPausePad = "30 tap start\n45 tap triangle\n";
// The play frame by which the pause has closed (its fade and hold take 60 frames).
constexpr std::uint64_t kAfterPause = 150;

// What a level's set-up gives it, as a run sees it from outside.
struct SetUp {
    std::size_t level = 0;
    double checkpoint = 0.0;
    bool playing = false;
    std::uint64_t pauses = 0;
    std::string bankInPlay;
    std::string bankAfterPause;
    std::string servicesBank;
    bool debugUsesGame = false;
    bool debugSeesLevel = false;
};

// Plays the pause pad in `run` from the level's first frame of play and reads its set-up.
SetUp playAndRead(coney::test::DiscSession& run) {
    SetUp seen;
    REQUIRE(run.runUntilPlay());
    seen.playing = run.session().play() != nullptr;
    seen.bankInPlay = run.loadedBank();
    REQUIRE(run.runToPlayFrame(kAfterPause));
    coney::StartUpFlow& flow = run.session().flow();
    seen.level = flow.state().currentLevel;
    seen.checkpoint = flow.state().checkPoint;
    seen.pauses = flow.pause().pauses();
    seen.bankAfterPause = run.loadedBank();
    seen.servicesBank = flow.services().bank();
    // The debug menus as `coney` wires them.
    coney::debug::DebugServices services;
    coney::platform::connectDebugServices(services, [&run] { return &run.session(); });
    coney::debug::TunableRegistry tunables;
    coney::debug::DebugSession debug(tunables, services, nullptr);
    seen.debugUsesGame = debug.usingGameScripts();
    seen.debugSeesLevel = !debug.vm().global("P2").isNil();
    return seen;
}

} // namespace

TEST_CASE("the disc's level99 checkpoint 2 is set up the same started directly and jumped to from the story",
          "[disc][story][session]") {
    std::optional<coney::io::Wad> wad = coney::test::openDisc();
    if (!wad) {
        SKIP("CONEY_DISC is not set: no disc to check");
    }

    // Started directly, as `--play-level level99 --checkpoint 2` starts it. One run at a time: a run owns the renderer.
    SetUp started;
    {
        coney::test::DiscSession direct(*wad, kPausePad);
        direct.session().startAtLevel("level99", 2);
        started = playAndRead(direct);
    }

    // From boot: the start-up movies, the legal screen and the menus, then the debug menus' jump once the level
    // table is read.
    SetUp jumped;
    {
        coney::test::DiscSession story(*wad, kPausePad);
        story.session().startStory();
        REQUIRE(story.runUntil([&story] { return story.session().flow().state().levels.find("level99").has_value(); },
                               coney::test::DiscSession::kPlayLimit));
        REQUIRE(story.session().jumpToLevel("level99", 2));
        jumped = playAndRead(story);
    }

    std::printf(
        "  level99 cp2: direct %zu/%.0f, %llu pauses, banks %s -> %s; story jump %zu/%.0f, %llu pauses, banks %s -> "
        "%s\n",
        started.level, started.checkpoint, static_cast<unsigned long long>(started.pauses), started.bankInPlay.c_str(),
        started.bankAfterPause.c_str(), jumped.level, jumped.checkpoint, static_cast<unsigned long long>(jumped.pauses),
        jumped.bankInPlay.c_str(), jumped.bankAfterPause.c_str());
    CHECK(started.playing);
    CHECK(started.checkpoint == 2);
    CHECK(started.pauses == 1);
    // Closing the pause put the level's own bank back, not the front end's.
    CHECK(started.bankAfterPause == started.bankInPlay);
    CHECK(started.servicesBank == started.bankInPlay);
    CHECK(started.debugUsesGame);
    CHECK(started.debugSeesLevel);

    CHECK(jumped.playing == started.playing);
    CHECK(jumped.level == started.level);
    CHECK(jumped.checkpoint == started.checkpoint);
    CHECK(jumped.pauses == started.pauses);
    CHECK(jumped.bankInPlay == started.bankInPlay);
    CHECK(jumped.bankAfterPause == started.bankAfterPause);
    CHECK(jumped.servicesBank == started.servicesBank);
    CHECK(jumped.debugUsesGame == started.debugUsesGame);
    CHECK(jumped.debugSeesLevel == started.debugSeesLevel);
}
