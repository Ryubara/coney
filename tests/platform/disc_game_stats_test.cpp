// SPDX-License-Identifier: GPL-3.0-or-later
// A check against the player's own disc that the statistics screen works as the hub's scripts reach it
// (docs/research/game-stats.md#mode): `level95` started as `coney --play-level` starts it, the binding
// `ShowGameStatsInterface` called through the script system as the hub calls it; mode 0x13 comes up with its sheets
// loaded, the disc's level table gives 23 story missions, and a fresh profile shows the two entries that are always
// listed (the hub and the Rumble arenas); the d-pad moves down the list and triangle closes it, the mode leaving
// after its fade out and calling `onClose`. It
// runs only when the environment variable CONEY_DISC names the disc and skips otherwise; it prints counts only
// (LEGAL.md).
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <string>

#include <catch2/catch_test_macros.hpp>

#include "fileio/wad.h"
#include "gamemodes/game_stats_mode.h"
#include "platform/game_session.h"
#include "scripting/lua_value.h"
#include "support/disc_play_fixtures.h"
#include "support/disc_session.h"

namespace {

// The d-pad down once, then triangle. Frames of play.
constexpr std::string_view kPad = "40 tap down\n"
                                  "80 tap triangle\n";
// Frames of play by which each step has happened.
constexpr std::uint64_t kShow = 10;
constexpr std::uint64_t kOpen = 30;
constexpr std::uint64_t kMoved = 60;
constexpr std::uint64_t kFading = 100;
constexpr std::uint64_t kClosed = 160;

} // namespace

TEST_CASE("the disc's statistics screen: shown by the binding in the hub, moved and closed with the pad",
          "[disc][game_stats]") {
    // Expected: game-stats.md#mode, confirmed (code)
    std::optional<coney::io::Wad> wad = coney::test::openDisc();
    if (!wad) {
        SKIP("CONEY_DISC is not set: no disc to check");
    }
    coney::test::DiscSession run(*wad, kPad);
    run.session().startAtLevel("level95", 1);
    REQUIRE(run.runUntilPlay());
    REQUIRE(run.runToPlayFrame(kShow));
    const std::array<coney::script::Value, 1> args{coney::script::Value(std::string("ConeyGameStatsClosed"))};
    (void)run.session().flow().scripts().call("ShowGameStatsInterface", args);

    REQUIRE(run.runToPlayFrame(kOpen));
    const coney::GameStatsMode& mode = run.session().flow().gameStats();
    const coney::gui::GameStatsScreen& screen = mode.screen();
    const bool onTop = run.stack().topId() == coney::GameStatsMode::kId;
    const bool open = screen.isOpen();
    const std::size_t listed = screen.data().missionsListed;
    const std::size_t shown = screen.data().levels.size();
    const std::size_t summaryLength = screen.summary().text().size();
    std::size_t sheetFailures = 0;
    for (const std::string& line : run.log()) {
        sheetFailures += line.find("game stats: sheet record") != std::string::npos ? 1 : 0;
        sheetFailures += line.find("game stats: part_page0") != std::string::npos ? 1 : 0;
    }

    REQUIRE(run.runToPlayFrame(kMoved));
    const std::size_t selected = screen.list().selected();

    // Triangle has closed the screen, but the mode stays while its fade out runs.
    REQUIRE(run.runToPlayFrame(kFading));
    const bool fading = run.stack().topId() == coney::GameStatsMode::kId && screen.closed();

    REQUIRE(run.runToPlayFrame(kClosed));
    const bool gone = run.stack().topId() != coney::GameStatsMode::kId;
    std::size_t closedCalls = 0;
    for (const std::string& line : run.log()) {
        closedCalls += line.find("game stats: closed, calling ConeyGameStatsClosed") != std::string::npos ? 1 : 0;
    }
    std::printf("  game stats: on top %d, open %d, %zu missions listed, %zu shown, summary %zu bytes, sheet failures "
                "%zu, selected %zu after down, fading %d, closed %d with %zu onClose calls\n",
                onTop ? 1 : 0, open ? 1 : 0, listed, shown, summaryLength, sheetFailures, selected, fading ? 1 : 0,
                gone ? 1 : 0, closedCalls);
    CHECK(onTop);
    CHECK(open);
    CHECK(listed == 23);
    CHECK(shown == 2);
    CHECK(summaryLength > 0);
    CHECK(sheetFailures == 0);
    CHECK(selected == 1);
    CHECK(fading);
    CHECK(gone);
    CHECK(closedCalls == 1);
}
