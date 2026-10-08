// SPDX-License-Identifier: GPL-3.0-or-later

// Checks against the player's own disc that player 1 stays player 1 when `level99` goes from checkpoint 1 to 2 in one
// session, as the story plays it: `P1.Cleanup` sets checkpoint 2 and `P2.SetupLesson1` makes a new Rembrandt and hands
// him the player with `HuChangePlayerGang` (docs/research/characters.md#level99-handover). From then on every lookup of
// player 1 must name the new human, or the street lesson stalls: the stereo theft's handler `P2.CarRadioStolen` asks
// `HuIsAPlayer` of the thief and only then schedules the next step. The level plays in the game session as
// `--play-level level99` plays it (support/disc_session.h), the course's end called by the test (as level99's street
// test does), the cars' step called once the street scene is skipped, and the theft driven with the pad (stick turned
// in circles at 90 %). It runs only when the environment variable CONEY_DISC names the disc and skips otherwise; it
// prints counts only (LEGAL.md).

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <format>
#include <numbers>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "debug/play_controls.h"
#include "fileio/wad.h"
#include "platform/game_session.h"
#include "platform/play_level_mode.h"
#include "scripting/script_system.h"
#include "support/disc_play_fixtures.h"
#include "support/disc_session.h"
#include "warriors/created_humans.h"

namespace {

// The frames of play: the course's end called, the street scene `l99_c2` skipped (cross after its 2 s), the cars' step
// called, player 1 put at car 142's front-left door, the theft from there.
constexpr std::uint64_t kCleanupAt = 300;
constexpr std::uint64_t kStreetSkipAt = 405;
constexpr std::uint64_t kCarsAt = 545;
constexpr std::uint64_t kTheftAt = 745;
constexpr std::uint64_t kEnd = 1500;

// The pad: the intro scene skipped, the street scene skipped, then at `theft` square at the car's window, triangle for
// the stereo and the left stick turned in circles at 90 % for the theft's stick game (combat.md#stereo-theft).
std::string padScript(std::uint64_t theft) {
    std::string script = std::format("100 tap cross\n{} tap cross\n{} tap square\n{} tap triangle\n", kStreetSkipAt,
                                     theft + 40, theft + 120);
    constexpr int kTurnUpdates = 400;
    constexpr float kDeflection = 90.0F;
    for (int i = 0; i < kTurnUpdates; ++i) {
        const float angle =
            (std::numbers::pi_v<float> / 2.0F) + (static_cast<float>(i) * std::numbers::pi_v<float> / 6.0F);
        script += std::format("{} stick left {} {}\n", theft + 150 + static_cast<std::uint64_t>(i),
                              static_cast<int>(kDeflection * std::cos(angle)),
                              static_cast<int>(kDeflection * std::sin(angle)));
    }
    script += std::format("{} stick left 0 0\n", theft + 150 + kTurnUpdates);
    return script;
}

// The handle of the last human the scripts made with `name`, or 0.
double lastNamed(const coney::CreatedHumans& humans, std::string_view name) {
    double handle = 0.0;
    for (const coney::HumanCreation& human : humans.all()) {
        if (human.name == name) {
            handle = human.handle;
        }
    }
    return handle;
}

// How many of `lines` contain `text`.
std::size_t countOf(const std::vector<std::string>& lines, std::string_view text) {
    std::size_t count = 0;
    for (const std::string& line : lines) {
        count += line.find(text) != std::string::npos ? 1 : 0;
    }
    return count;
}

} // namespace

TEST_CASE("the disc's level99: player 1 handed over at checkpoint 2 steals the stereo as the player",
          "[disc][story][handover]") {
    std::optional<coney::io::Wad> wad = coney::test::openDisc();
    if (!wad) {
        SKIP("CONEY_DISC is not set: no disc to check");
    }

    // Checkpoint 1 as --play-level starts it, the scripts' calls traced from the level's Lua state on.
    coney::test::DiscSession run(*wad, padScript(kTheftAt));
    coney::script::ScriptSystem& scripts = run.session().flow().scripts();
    std::vector<std::string> trace;
    run.session().startAtLevel("level99", 1, [&scripts, &trace] {
        scripts.traceCalls([&trace](std::string_view line) { trace.emplace_back(line); });
    });
    REQUIRE(run.runUntilPlay());
    const coney::CreatedHumans& humans = run.session().flow().humans();

    // The course's end: checkpoint 2, a new Rembrandt handed the player.
    REQUIRE(run.runToPlayFrame(kCleanupAt));
    const double coursePlayer = lastNamed(humans, "Rembrandt");
    CHECK(scripts.call("P1.Cleanup"));
    REQUIRE(run.runToPlayFrame(kCarsAt));
    const double streetPlayer = lastNamed(humans, "Rembrandt");
    REQUIRE(streetPlayer != coursePlayer);
    const coney::HumanCreation* player = humans.player(1);
    REQUIRE(player != nullptr);
    CHECK(player->handle == streetPlayer);
    CHECK(run.session().flow().state().checkPoint == 2);

    // The cars' step (CarSpawnRadio, the theft handler), then player 1 at car 142's front-left door, facing it.
    CHECK(scripts.call("P2.SetupCars"));
    REQUIRE(run.runToPlayFrame(kTheftAt));
    coney::platform::PlayLevelMode* play = run.session().play();
    REQUIRE(play != nullptr);
    play->teleport(coney::debug::Place{.name = "car", .feet = {58.0F, 39.5F, 0.3F}, .headingDegrees = 180.0F});
    REQUIRE(run.runToPlayFrame(kEnd));

    const std::string stolenBy = std::format("> P2.CarRadioStolen({:.0f},", streetPlayer);
    const std::string isPlayer = std::format("HuIsAPlayer({:.0f}) -> 1", streetPlayer);
    std::printf(
        "  level99 hand-over: player 1 %.0f -> %.0f; %zu stereo thefts, %zu by the new player 1, %zu next steps\n",
        coursePlayer, streetPlayer, countOf(run.log(), "theft: stole the stereo"), countOf(trace, stolenBy),
        countOf(trace, "ScheduleFunc(\"P2.SetupPeds\""));
    CHECK(countOf(run.log(), "theft: stole the stereo") == 1);
    // The handler is told the new player 1 stole it, sees a player and schedules the next step (the mugging).
    CHECK(countOf(trace, stolenBy) == 1);
    CHECK(countOf(trace, isPlayer) >= 1);
    CHECK(countOf(trace, "ScheduleFunc(\"P2.SetupPeds\", 3000)") == 1);
}
