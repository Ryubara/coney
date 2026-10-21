// SPDX-License-Identifier: GPL-3.0-or-later
// A check against the player's own disc that the pause menu's Controls screen works as a player reaches it
// (docs/research/pause.md#fn-controlmenuhud): `level99` started as `coney --play-level` starts it, START, the left
// stick pushed most of the way right to the Controls item, cross; cross on Controller shows the pad picture's page,
// triangle closes it, and no label there runs into the one below it; the stick down to Tutorial, cross, and after
// 550 ms the first tip's text unfolds. It runs only when the environment variable CONEY_DISC names the disc and skips
// otherwise; it prints counts only (LEGAL.md).
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "fileio/wad.h"
#include "gamemodes/pause_mode.h"
#include "gui/pause_menu/control_menu.h"
#include "gui/pause_menu/pause_menu.h"
#include "platform/game_session.h"
#include "support/disc_play_fixtures.h"
#include "support/disc_session.h"

namespace {

// START, the stick right three times (70 %: Objectives, Stats, Options, Controls), cross; cross on Controller, then
// triangle; the stick down (70 %) to Tutorial, cross. Frames of play.
constexpr std::string_view kPad = "30 tap start\n"
                                  "80 stick left 70 0\n82 stick left 0 0\n"
                                  "92 stick left 70 0\n94 stick left 0 0\n"
                                  "104 stick left 70 0\n106 stick left 0 0\n"
                                  "120 tap cross\n"
                                  "140 tap cross\n"
                                  "170 tap triangle\n"
                                  "190 stick left 0 -70\n192 stick left 0 0\n"
                                  "210 tap cross\n";
// Frames of play by which each step has happened.
constexpr std::uint64_t kOnControls = 130;
constexpr std::uint64_t kOnController = 160;
constexpr std::uint64_t kOnTutorial = 220;
constexpr std::uint64_t kTipShown = 260;

// How many of `lines` are not empty.
std::size_t nonEmpty(const std::vector<std::string>& lines) {
    std::size_t count = 0;
    for (const std::string& line : lines) {
        count += line.empty() ? 0 : 1;
    }
    return count;
}

// How many Controller labels run into the label under them in their column, and how many take two lines or more, as
// laid out in `canvas`'s fonts. A label's first line is centred on its y and further lines go down, so its last line
// is centred its height less one line's height lower. It runs into the next label when that label's first line is
// nearer to its last line than 90 % of the table's closest two rows are to each other (a stand-in two-line label
// 0.05 above the next ended 0.009 above it).
struct LabelFit {
    std::size_t overlaps = 0;
    std::size_t multiLine = 0;
};
LabelFit labelFit(const coney::gui::ControlMenu& controls, const coney::gui::GuiCanvas& canvas) {
    constexpr std::size_t kColumn = coney::gui::kControllerLabels;
    const auto& rows = controls.layout().rows;
    float closest = 1.0F;
    for (std::size_t row = 0; row + 1 < kColumn; ++row) {
        closest = std::min(closest, rows.at(row + 1) - rows.at(row));
    }
    // One line's height: the smallest label's.
    float oneLine = 1.0F;
    for (std::size_t i = 0; i < 2 * kColumn; ++i) {
        oneLine = std::min(oneLine, controls.label(i).layout(canvas).height);
    }
    LabelFit fit;
    for (std::size_t column = 0; column < 2; ++column) {
        for (std::size_t row = 0; row < kColumn; ++row) {
            const coney::gui::TextWidget& label = controls.label((column * kColumn) + row);
            const coney::gui::TextLayout layout = label.layout(canvas);
            fit.multiLine += layout.lines > 1 ? 1 : 0;
            if (row + 1 < kColumn) {
                const float lastLine = label.style().y + (layout.height - oneLine);
                const float nextY = controls.label((column * kColumn) + row + 1).style().y;
                fit.overlaps += nextY - lastLine < 0.9F * closest ? 1 : 0;
            }
        }
    }
    return fit;
}

} // namespace

TEST_CASE("the disc's pause menu: Controls opens the pad picture and the tutorial tips through the pad",
          "[disc][pause_menu][controls]") {
    std::optional<coney::io::Wad> wad = coney::test::openDisc();
    if (!wad) {
        SKIP("CONEY_DISC is not set: no disc to check");
    }
    coney::test::DiscSession run(*wad, kPad);
    run.session().startAtLevel("level99", 1);
    REQUIRE(run.runUntilPlay());
    const coney::gui::PauseMenu& menu = run.session().flow().pause().menu();

    REQUIRE(run.runToPlayFrame(kOnControls));
    REQUIRE(menu.screen() == coney::gui::PauseScreen::Controls);
    const std::size_t entries = nonEmpty(menu.screenLines());

    REQUIRE(run.runToPlayFrame(kOnController));
    CHECK(menu.controls().page() == coney::gui::ControlPage::Controller);
    const std::vector<std::string> controller = menu.screenLines();
    const LabelFit fit = labelFit(menu.controls(), run.session().flow().pause().canvas());
    std::size_t pictureFailures = 0;
    for (const std::string& line : run.log()) {
        pictureFailures += line.find("sheet record 399") != std::string::npos ? 1 : 0;
    }

    REQUIRE(run.runToPlayFrame(kOnTutorial));
    CHECK(menu.controls().page() == coney::gui::ControlPage::Tutorial);
    CHECK(menu.controls().selected() == 1);
    REQUIRE(run.runToPlayFrame(kTipShown));
    CHECK(menu.controls().descriptionShown());
    const std::vector<std::string> tutorial = menu.screenLines();

    std::printf("  pause controls: %zu entries, controller page %zu of %zu texts (%zu of two lines or more, %zu "
                "overlapping the next), picture sheet failures %zu, tutorial %zu of %zu texts shown with the first "
                "tip's text\n",
                entries, nonEmpty(controller), controller.size(), fit.multiLine, fit.overlaps, pictureFailures,
                nonEmpty(tutorial), tutorial.size());
    CHECK(entries == 2);
    CHECK(controller.size() == 1 + (2 * coney::gui::kControllerLabels));
    CHECK(nonEmpty(controller) == controller.size());
    CHECK(fit.overlaps == 0);
    CHECK(pictureFailures == 0);
    // The title, eight tips and the first tip's text, fully unfolded.
    CHECK(menu.controls().descriptionReveal() == 1.0F);
    CHECK(tutorial.size() == 10);
    CHECK(nonEmpty(tutorial) == tutorial.size());
}
