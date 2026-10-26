// SPDX-License-Identifier: GPL-3.0-or-later
// A check against the player's own disc that the pause menu's top header and Stats screen work as a player reaches
// them (docs/research/pause.md#the-top-header, docs/research/pause.md#stats-screen): `level99` started as `coney
// --play-level` starts it, START: the header shows the mission's title after two circles (its number and the
// checkpoint's letter) with the level's name under it; the left stick pushed most of the way right to Stats, cross:
// the player's Warrior's screen with its four bars and texts, its pictures loaded; triangle back to the grid. It runs
// only when the environment variable CONEY_DISC names the disc and skips otherwise; it prints counts only (LEGAL.md).
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "fileio/wad.h"
#include "gamemodes/pause_mode.h"
#include "gui/pause_menu/pause_menu.h"
#include "platform/game_session.h"
#include "support/disc_play_fixtures.h"
#include "support/disc_session.h"

namespace {

// START, the stick right once (70 %: Objectives to Stats), cross, then triangle. Frames of play.
constexpr std::string_view kPad = "30 tap start\n"
                                  "80 stick left 70 0\n82 stick left 0 0\n"
                                  "100 tap cross\n"
                                  "140 tap triangle\n";
// Frames of play by which each step has happened.
constexpr std::uint64_t kOnGrid = 70;
constexpr std::uint64_t kOnStats = 120;
constexpr std::uint64_t kBackOnGrid = 160;

} // namespace

TEST_CASE("the disc's pause menu: the circled header and the Stats screen through the pad",
          "[disc][pause_menu][pause_stats]") {
    // Expected: pause.md#stats-screen, confirmed (runtime)
    std::optional<coney::io::Wad> wad = coney::test::openDisc();
    if (!wad) {
        SKIP("CONEY_DISC is not set: no disc to check");
    }
    coney::test::DiscSession run(*wad, kPad);
    run.session().startAtLevel("level99", 1);
    REQUIRE(run.runUntilPlay());
    const coney::gui::PauseMenu& menu = run.session().flow().pause().menu();

    REQUIRE(run.runToPlayFrame(kOnGrid));
    const coney::gui::CircledTextHeader& header = menu.topHeader();
    const std::size_t circles = header.circles();
    const bool titled = !header.title().text().empty() && !header.subtitle().text().empty();

    REQUIRE(run.runToPlayFrame(kOnStats));
    const bool onStats = menu.screen() == coney::gui::PauseScreen::Stats;
    const std::vector<std::string> lines = menu.screenLines();
    std::size_t texts = 0;
    for (const std::string& line : lines) {
        texts += line.empty() ? 0 : 1;
    }
    std::size_t barsInRange = 0;
    for (const float value : menu.stats().values()) {
        barsInRange += value > 0.0F && value <= 1.0F ? 1 : 0;
    }
    std::size_t pictureFailures = 0;
    for (const std::string& line : run.log()) {
        pictureFailures += line.find("pause: sheet record") != std::string::npos ? 1 : 0;
        pictureFailures += line.find("pause: part_page0") != std::string::npos ? 1 : 0;
    }

    REQUIRE(run.runToPlayFrame(kBackOnGrid));
    std::printf("  pause stats: header %zu circles, titled %d; stats screen %d with %zu of %zu texts, %zu of 4 bars "
                "in (0, 1], picture failures %zu\n",
                circles, titled ? 1 : 0, onStats ? 1 : 0, texts, lines.size(), barsInRange, pictureFailures);
    CHECK(circles == 2);
    CHECK(titled);
    CHECK(onStats);
    CHECK(lines.size() == 5);
    CHECK(texts == lines.size());
    CHECK(barsInRange == 4);
    CHECK(pictureFailures == 0);
    CHECK(menu.screen() == coney::gui::PauseScreen::Grid);
}
