// SPDX-License-Identifier: GPL-3.0-or-later

// Checks against the player's own disc that the debug menus lead to Armies of the Night's first stage and not to a
// black screen (user item 19, issue #85; docs/research/armies.md#coney-fixes). The level table's record `armies` has
// no level file, so the Levels page leaves it out and a jump to it is refused; `level60` checkpoint 1, which the
// Missions page now lists, is jumped to from a level in play and plays its intro scene to the end with the picture
// clear. The session runs as `coney` runs it (support/disc_session.h), the pad idle. It runs only when the environment
// variable CONEY_DISC names the disc and skips otherwise; it prints counts only (LEGAL.md).

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "camera/cameras.h"
#include "camera/rail_camera.h"
#include "core/input_script.h"
#include "debug/debug_session.h"
#include "debug/menu_model.h"
#include "debug/tunables.h"
#include "fileio/wad.h"
#include "gamemodes/armies_game_over_mode.h"
#include "gamemodes/start_up_flow.h"
#include "hud/armies_hud.h"
#include "human/human.h"
#include "human/locomotion.h"
#include "human/player.h"
#include "platform/game_session.h"
#include "platform/game_session_debug.h"
#include "platform/play_level_mode.h"
#include "support/disc_play_fixtures.h"
#include "support/disc_session.h"
#include "support/live_pad.h"
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
constexpr std::uint64_t kWalkLimit = std::uint64_t{40} * 30;
constexpr std::uint64_t kTimerLimit = std::uint64_t{110} * 30;
constexpr std::uint64_t kCountdownFrames = std::uint64_t{11} * 30;
// The walk on along the rail (toward -x, level60's Rail1) to the first fight, where P1.CamLock01 locks the rail
// camera; the stick at 60 %, a walk (the stage makes the player walk only, CfgPlayerCombatWalkOnly), and the frames
// allowed for it.
constexpr float kWalkOnX = -400.0F;
constexpr int kStickPercent = 60;
constexpr std::uint64_t kLockLimit = std::uint64_t{90} * 30;
constexpr float kDegreesPerRadian = 57.29578F;
// The two bosses level60_destroyers' main chunk registers (Lemmy and Virgil) and their pictures' sprite words.
constexpr std::array<std::uint32_t, 2> kDestroyersBossPictures{1966095, 1966110};

} // namespace

TEST_CASE("the disc's level60: the debug menus reach Armies of the Night's first stage, not a black screen",
          "[disc][story][armies]") {
    // Expected: unverified, #85 (Coney's own debug menus)
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
    // Expected: armies.md#first-frames, confirmed (runtime)
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
    // The REQUIRE above is the check; value_or reads the position without an unchecked access.
    const coney::anim::Vec3 feetAt = out.value_or(coney::anim::Vec3{});
    const float walkedTo = std::hypot(feetAt.x - kFlag02X, feetAt.y - kFlag02Y);
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
    // Player 2's panel asks him to join from the set-up on; player 1's attach cleared his (PCSX2, 2026-10-26).
    CHECK_FALSE(armies.joinMessage(0));
    CHECK(armies.joinMessage(1));

    // The idle stage runs its timer out and the game-over screen opens (the original: "CONTINUE?" at about 126 s).
    const bool over =
        run.runUntil([&run] { return run.stack().topId() == coney::ArmiesGameOverMode::kId; }, kTimerLimit);
    const std::uint64_t overAt = run.frames();
    std::printf("  level60 idle: game over %d at frame %llu, countdown %d\n", over ? 1 : 0,
                static_cast<unsigned long long>(overAt), flow.armiesGameOver().menu().count());
    CHECK(over);
    // The countdown, then the end phase: about 10 s more with nobody pressing Start.
    run.run(kCountdownFrames);
    CHECK(flow.armiesGameOver().menu().ending());
}

TEST_CASE("the disc's level60 played on: the pad taken and given back, the bosses' pictures and the rail camera "
          "locked at the first fight",
          "[disc][story][armies]") {
    // Expected: armies.md#level60-start, confirmed (code): level60_destroyers' main chunk calls HUDSetANBossTexture for
    // Lemmy and Virgil, F1.NoBrain and F1.Restore02 switch HuEnableController off and on round the walk out, and
    // P1.CamLock01 calls CamLockRail(true) once the camera sees the first fight's marker.
    std::optional<coney::io::Wad> wad = coney::test::openDisc();
    if (!wad) {
        SKIP("CONEY_DISC is not set: no disc to check");
    }
    coney::test::LivePad pad;
    coney::test::DiscSession run(*wad, pad);
    run.session().startAtLevel("level60", 1);
    REQUIRE(run.runUntilPlay());
    coney::StartUpFlow& flow = run.session().flow();
    const auto padControlled = [&run] {
        const coney::platform::PlayLevelMode* play = run.session().play();
        return play != nullptr && play->player().padControlled();
    };

    // The intro and the walk out with the pad taken (F1.NoBrain), then the timer with it given back (F1.Restore02).
    bool padTaken = false;
    const bool timerRuns = run.runUntil(
        [&] {
            padTaken = padTaken || !padControlled();
            return flow.state().player.stopWatch.running() && padControlled();
        },
        kWalkLimit);
    const coney::hud::ArmiesHud& armies = flow.hud().armies();
    std::array<std::size_t, 2> pictures{};
    for (std::size_t player = 0; player < pictures.size(); ++player) {
        for (const coney::hud::BossPicture& picture : armies.bossPictures(player)) {
            if (std::ranges::find(kDestroyersBossPictures, picture.texture) != kDestroyersBossPictures.end()) {
                ++pictures.at(player);
            }
        }
    }
    std::printf("  level60 on: pad taken %d, timer and pad back %d after %llu frames, boss pictures %zu and %zu\n",
                padTaken ? 1 : 0, timerRuns ? 1 : 0, static_cast<unsigned long long>(run.frames()), pictures[0],
                pictures[1]);
    CHECK(padTaken);
    REQUIRE(timerRuns);
    CHECK(pictures == std::array<std::size_t, 2>{2, 2});

    // On along the rail with the stick, through the camera as it faces, until the rail camera is held.
    const coney::camera::Cameras* cameras = run.session().gameplay().cameras();
    const coney::camera::RailCamera* rail = cameras != nullptr ? cameras->rail() : nullptr;
    REQUIRE(rail != nullptr);
    REQUIRE_FALSE(rail->held());
    bool locked = false;
    std::uint64_t walked = 0;
    for (; walked < kLockLimit && !locked; ++walked) {
        const coney::platform::PlayLevelMode* play = run.session().play();
        REQUIRE(play != nullptr);
        const coney::human::Human& player = play->player().human();
        const coney::SteerView view{.x = player.position().x,
                                    .y = player.position().y,
                                    .cameraHeading =
                                        coney::human::headingOf(player.record().cameraForward) * kDegreesPerRadian};
        const std::array<int, 2> stick = coney::steerStick(view, kWalkOnX, kFlag02Y, kStickPercent);
        pad.play(std::format("{} stick left {} {}\n", pad.nextFrame(), stick[0], stick[1]));
        run.run(1);
        locked = rail->held();
    }
    const coney::platform::PlayLevelMode* play = run.session().play();
    REQUIRE(play != nullptr);
    std::printf("  level60 on: rail camera locked %d after %llu frames of walking, player at x %.1f\n", locked ? 1 : 0,
                static_cast<unsigned long long>(walked), static_cast<double>(play->player().human().position().x));
    CHECK(locked);
}
