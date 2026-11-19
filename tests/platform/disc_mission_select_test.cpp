// SPDX-License-Identifier: GPL-3.0-or-later
// A check against the player's own disc that the mission select works as the hub's subway menu reaches it
// (docs/research/frontend.md#fn-missionselecthud): `level95` started as `coney --play-level` starts it, four story
// missions marked complete through the script binding `UM_Unlock`, and `HUDShowMissionSelect` called through the
// script system as `menu.DoSubwayMenu` calls it. Mode 0x10 comes up with its sheets loaded and the missions in their
// areas; the pad (the d-pad and cross, as a player presses them) moves to the second area, opens its mission, steps to
// checkpoint B and starts it; the choice comes at once (the clock is past 2300 ms), the mode leaves after its fade out
// and calls `onChoose` with the level and the checkpoint. It runs only when the environment variable CONEY_DISC names
// the disc and skips otherwise; it prints counts only (LEGAL.md). It failed first on the unit's stubbed source
// (coney-tools orig fails-first).
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <string>

#include <catch2/catch_test_macros.hpp>

#include "fileio/wad.h"
#include "gamemodes/mission_select_mode.h"
#include "gui/mission_select.h"
#include "platform/game_session.h"
#include "scripting/lua_value.h"
#include "support/disc_play_fixtures.h"
#include "support/disc_session.h"

namespace {

// Up to the second area, cross into its list, cross to open its mission, down to checkpoint B, cross to start.
// Frames of play.
constexpr std::string_view kPad = "40 tap up\n"
                                  "60 tap cross\n"
                                  "80 tap cross\n"
                                  "100 tap down\n"
                                  "120 tap cross\n";
// Frames of play by which each step has happened.
constexpr std::uint64_t kShow = 10;
constexpr std::uint64_t kOpen = 30;
constexpr std::uint64_t kStarted = 130;
constexpr std::uint64_t kClosed = 300;

} // namespace

TEST_CASE("the disc's mission select: shown by the binding in the hub, a mission chosen with the pad",
          "[disc][mission_select]") {
    // Expected: frontend.md#fn-missionselecthud, confirmed (code) and confirmed (runtime)
    // (run r-20261119-mission-select): levels 99 and 80 share Coney, the largest map y; 87 (East Coney)
    // is next; 34's area is lower still; up shows the next area, cross opens, down steps the letter, cross starts.
    std::optional<coney::io::Wad> wad = coney::test::openDisc();
    if (!wad) {
        SKIP("CONEY_DISC is not set: no disc to check");
    }
    coney::test::DiscSession run(*wad, kPad);
    run.session().startAtLevel("level95", 1);
    REQUIRE(run.runUntilPlay());
    REQUIRE(run.runToPlayFrame(kShow));
    auto& scripts = run.session().flow().scripts();
    for (const double number : {99.0, 80.0, 87.0, 34.0}) {
        const std::array<coney::script::Value, 3> unlock{coney::script::Value(number), coney::script::Value(0.0),
                                                         coney::script::Value(0.0)};
        (void)scripts.call("UM_Unlock", unlock);
    }
    const std::array<coney::script::Value, 2> args{coney::script::Value(std::string("ConeyMissionCancel")),
                                                   coney::script::Value(std::string("ConeyMissionChoose"))};
    (void)scripts.call("HUDShowMissionSelect", args);

    REQUIRE(run.runToPlayFrame(kOpen));
    const coney::MissionSelectMode& mode = run.session().flow().missionSelect();
    const coney::gui::MissionSelectScreen& screen = mode.screen();
    const bool onTop = run.stack().topId() == coney::MissionSelectMode::kId;
    const bool open = screen.isOpen();
    const std::size_t areas = screen.areas().size();
    const std::size_t firstMissions = areas > 0 ? screen.areas().at(0).missions.size() : 0;
    const int secondLevel =
        areas > 1 && !screen.areas().at(1).missions.empty() ? screen.areas().at(1).missions.at(0).level : 0;
    std::size_t sheetFailures = 0;
    for (const std::string& line : run.log()) {
        sheetFailures += line.find("mission select: sheet record") != std::string::npos ? 1 : 0;
        sheetFailures += line.find("mission select: part_page0") != std::string::npos ? 1 : 0;
    }

    REQUIRE(run.runToPlayFrame(kStarted));
    const std::size_t cursor = screen.cursor();
    const bool chosen = screen.chosen();
    const int checkpoint = screen.chosenCheckpoint();

    REQUIRE(run.runToPlayFrame(kClosed));
    const bool gone = run.stack().topId() != coney::MissionSelectMode::kId;
    std::size_t chooseCalls = 0;
    for (const std::string& line : run.log()) {
        chooseCalls +=
            line.find("mission select: chose level 87 checkpoint 2, calling ConeyMissionChoose") != std::string::npos
                ? 1
                : 0;
    }
    std::printf("  mission select: on top %d, open %d, %zu areas, %zu missions in the first, second area's level %d, "
                "sheet failures %zu, cursor %zu, chosen %d at checkpoint %d, closed %d with %zu onChoose calls\n",
                onTop ? 1 : 0, open ? 1 : 0, areas, firstMissions, secondLevel, sheetFailures, cursor, chosen ? 1 : 0,
                checkpoint, gone ? 1 : 0, chooseCalls);
    CHECK(onTop);
    CHECK(open);
    CHECK(areas == 3);
    CHECK(firstMissions == 2);
    CHECK(secondLevel == 87);
    CHECK(sheetFailures == 0);
    CHECK(cursor == 1);
    CHECK(chosen);
    CHECK(checkpoint == 2);
    CHECK(gone);
    CHECK(chooseCalls == 1);
}
