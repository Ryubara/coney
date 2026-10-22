// SPDX-License-Identifier: GPL-3.0-or-later

// Checks against the player's own disc that the debug menus lead to Armies of the Night's first stage and not to a
// black screen (owner item 19, issue #85; docs/research/armies.md#coney-fixes). The level table's record `armies` has
// no level file, so the Levels page leaves it out and a jump to it is refused; `level60` checkpoint 1, which the
// Missions page now lists, is jumped to from a level in play and plays its intro scene to the end with the picture
// clear. The session runs as `coney` runs it (support/disc_session.h), the pad idle. It runs only when the environment
// variable CONEY_DISC names the disc and skips otherwise; it prints counts only (LEGAL.md).

#include <algorithm>
#include <cmath>
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
#include "camera/cameras.h"
#include "camera/rail_camera.h"
#include "gamemodes/armies_game_over_mode.h"
#include "gamemodes/start_up_flow.h"
#include "hud/armies_hud.h"
#include "platform/game_session.h"
#include "platform/game_session_debug.h"
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

// Flag_02, where P1.Start03 walks the player (level60.lua's AddFlag), and the walk's radius.
constexpr float kFlag02X = -94.9F;
constexpr float kFlag02Y = 24.0F;
constexpr float kArrivalRadius = 0.5F;
// Frames allowed for the intro scene and the walk (the original's timer starts about 28 s in), for the 99 s timer, and
// for the game over's countdown (10 steps of 1 s) with a margin.
constexpr std::uint64_t kWalkLimit = 40 * 30;
constexpr std::uint64_t kTimerLimit = 110 * 30;
constexpr std::uint64_t kCountdownFrames = 11 * 30;

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

TEST_CASE("the disc's level60 with the pad idle: the walk out, the Armies HUD, the timer and the game over",
          "[disc][story][armies]") {
    std::optional<coney::io::Wad> wad = coney::test::openDisc();
    if (!wad) {
        SKIP("CONEY_DISC is not set: no disc to check");
    }
    // As the debug menus' and `--play-level`'s start, checkpoint 1 with the pad idle
    // (docs/research/armies.md#first-frames).
    coney::test::DiscSession run(*wad, "");
    run.session().startAtLevel("level60", 1);
    REQUIRE(run.runUntilPlay());
    coney::StartUpFlow& flow = run.session().flow();
    const auto feet = [&run]() -> std::optional<coney::anim::Vec3> {
        const coney::platform::PlayLevelMode* play = run.session().play();
        if (play == nullptr) {
            return std::nullopt;
        }
        return play->player().human().position();
    };

    // The intro scene, then P1.Start03's walk out of the garage to Flag_02 (-94.9, 24): the original stops at
    // (-94.78, 23.54) about 20.4 s in (PCSX2, 2026-10-21). Then F1.Restore02 starts the 99 s timer.
    const bool timerRuns = run.runUntil([&flow] { return flow.state().player.stopWatch.running(); }, kWalkLimit);
    const std::optional<coney::anim::Vec3> out = feet();
    REQUIRE(out.has_value());
    const coney::camera::Cameras* cameras = run.session().gameplay().cameras();
    const coney::camera::RailCamera* rail = cameras != nullptr ? cameras->rail() : nullptr;
    const coney::hud::ArmiesHud& armies = flow.hud().armies();
    const std::uint32_t creditsAtStart = armies.credits();
    const float walkedTo = std::hypot(out->x - kFlag02X, out->y - kFlag02Y);
    std::printf("  level60 idle: timer running %d after %llu frames, player at %.2f from Flag_02, rail mode %d, "
                "credits %u, GO sign %u\n",
                timerRuns ? 1 : 0, static_cast<unsigned long long>(run.frames()), static_cast<double>(walkedTo),
                rail != nullptr ? static_cast<int>(rail->mode()) : -1, creditsAtStart, armies.goSignMode());
    CHECK(timerRuns);
    CHECK(walkedTo <= kArrivalRadius);
    REQUIRE(rail != nullptr);
    CHECK(rail->mode() == coney::camera::RailMode::Trailing);
    CHECK(creditsAtStart == 7);
    CHECK(armies.active());

    // The idle stage runs its timer out and the game-over screen opens (the original: "CONTINUE?" at about 126 s).
    const bool over = run.runUntil([&run] { return run.stack().topId() == coney::ArmiesGameOverMode::kId; },
                                   kTimerLimit);
    const std::uint64_t overAt = run.frames();
    std::printf("  level60 idle: game over %d at frame %llu, countdown %d\n", over ? 1 : 0,
                static_cast<unsigned long long>(overAt), flow.armiesGameOver().menu().count());
    CHECK(over);
    // The countdown, then the end phase: about 10 s more with nobody pressing Start.
    run.run(kCountdownFrames);
    CHECK(flow.armiesGameOver().menu().ending());
}
