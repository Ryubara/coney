// SPDX-License-Identifier: GPL-3.0-or-later
// The pause menu's Controls screen (docs/research/pause.md#fn-controlmenuhud), driven command by command with synthetic
// strings.
#include "gui/pause_menu/control_menu.h"

#include <cstdint>
#include <format>
#include <string>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "gui/colour_table.h"
#include "gui/global_strings.h"
#include "gui/menu_input.h"

using Catch::Approx;
using coney::gui::ControlMenu;
using coney::gui::ControlPage;
using coney::gui::GuiFrame;
using coney::gui::MenuCommand;

namespace {

// The screen opened at 10 s over strings that read "#<hex id>", recording its cues.
struct Screen {
    coney::gui::GlobalStrings strings;
    coney::gui::MenuInput input;
    ControlMenu menu;
    std::vector<int> cues;
    std::uint64_t nowMs = 10'000;

    Screen() {
        for (std::uint32_t id = 0x100; id < 0x180; ++id) {
            strings.set(id, std::format("#{:x}", id));
        }
        menu.open(&strings, input, [this](int cue) { cues.push_back(cue); }, nowMs);
    }
    // One command, 200 ms after the last (past the list's 100 ms gap); returns whether it left the screen.
    bool command(MenuCommand command) {
        nowMs += 200;
        menu.update(GuiFrame{nowMs, nullptr});
        return menu.handle(command, nowMs);
    }
    // Lets `ms` of game time pass.
    void wait(std::uint64_t ms) {
        nowMs += ms;
        menu.update(GuiFrame{nowMs, nullptr});
    }
    // The last cue played, or -1.
    [[nodiscard]] int lastCue() const { return cues.empty() ? -1 : cues.back(); }
};

} // namespace

TEST_CASE("controls: the screen opens on Controller and Tutorial, Controller selected", "[pause_menu][controls]") {
    Screen screen;
    CHECK(screen.menu.page() == ControlPage::None);
    CHECK(screen.menu.selected() == 0);
    CHECK(screen.menu.lines() == std::vector<std::string>{"#130", "#131"});
    CHECK(screen.cues.empty());
}

TEST_CASE("controls: up and down move between the entries, refusing at the ends; left and right refuse",
          "[pause_menu][controls]") {
    Screen screen;
    CHECK_FALSE(screen.command(MenuCommand::Up));
    CHECK(screen.lastCue() == 0xe);
    CHECK_FALSE(screen.command(MenuCommand::Down));
    CHECK(screen.lastCue() == 4);
    CHECK(screen.menu.selected() == 1);
    CHECK_FALSE(screen.command(MenuCommand::Down));
    CHECK(screen.lastCue() == 0xe);
    CHECK(screen.menu.selected() == 1);
    CHECK_FALSE(screen.command(MenuCommand::Left));
    CHECK(screen.lastCue() == 0xe);
    CHECK_FALSE(screen.command(MenuCommand::Right));
    CHECK(screen.lastCue() == 0xe);
    // Back leaves the screen with cue 0xf.
    CHECK(screen.command(MenuCommand::Back));
    CHECK(screen.lastCue() == 0xf);
}

TEST_CASE("controls: Controller opens with cue 8 on the pad picture and its 14 labels; back closes it",
          "[pause_menu][controls]") {
    Screen screen;
    CHECK_FALSE(screen.command(MenuCommand::Accept));
    CHECK(screen.lastCue() == 8);
    REQUIRE(screen.menu.page() == ControlPage::Controller);
    const std::vector<std::string> lines = screen.menu.lines();
    REQUIRE(lines.size() == 15);
    CHECK(lines.front() == "#130");
    CHECK(lines[1] == "#132");
    CHECK(lines[7] == "#138");
    CHECK(lines[8] == "#139");
    CHECK(lines.back() == "#13f");
    // The picture: centred at (0.5, 0.58), 0.51 x 0.45.
    const auto& picture = screen.menu.picture().setupValues();
    CHECK(picture.x == Approx(0.5F));
    CHECK(picture.y == Approx(0.58F));
    CHECK(picture.height == Approx(0.45F));
    REQUIRE(picture.width.has_value());
    CHECK(*picture.width == Approx(0.51F));
    // Directions refuse; accept does nothing; back closes the page, not the screen.
    CHECK_FALSE(screen.command(MenuCommand::Down));
    CHECK(screen.lastCue() == 0xe);
    const std::size_t cues = screen.cues.size();
    CHECK_FALSE(screen.command(MenuCommand::Accept));
    CHECK(screen.cues.size() == cues);
    CHECK_FALSE(screen.command(MenuCommand::Back));
    CHECK(screen.lastCue() == 0xf);
    CHECK(screen.menu.page() == ControlPage::None);
}

TEST_CASE("controls: Tutorial lists the tips, the focused one's text shown 550 ms after it is focused",
          "[pause_menu][controls]") {
    Screen screen;
    (void)screen.command(MenuCommand::Down);
    const std::size_t cues = screen.cues.size();
    CHECK_FALSE(screen.command(MenuCommand::Accept));
    // Only the Controller page's opening has a cue.
    CHECK(screen.cues.size() == cues);
    REQUIRE(screen.menu.page() == ControlPage::Tutorial);
    CHECK(screen.menu.tips().count() == coney::gui::kTutorialTips);
    CHECK(screen.menu.tips().visibleCount() == ControlMenu::kVisibleTips);
    // The title and the first ten tips; no text yet.
    std::vector<std::string> lines = screen.menu.lines();
    REQUIRE(lines.size() == 11);
    CHECK(lines[0] == "#131");
    CHECK(lines[1] == "#145");
    CHECK(lines[10] == "#14e");
    CHECK_FALSE(screen.menu.descriptionShown());
    screen.wait(549);
    CHECK_FALSE(screen.menu.descriptionShown());
    screen.wait(1);
    CHECK(screen.menu.descriptionShown());
    CHECK(screen.lastCue() == 0xd);
    lines = screen.menu.lines();
    REQUIRE(lines.size() == 12);
    CHECK(lines[2] == "#15c");
    // Down focuses the next tip (cue 4) and hides the text until it is due again.
    CHECK_FALSE(screen.command(MenuCommand::Down));
    CHECK(screen.menu.tips().selected() == 1);
    CHECK(screen.cues.at(screen.cues.size() - 1) == 4);
    CHECK_FALSE(screen.menu.descriptionShown());
    screen.wait(600);
    lines = screen.menu.lines();
    REQUIRE(lines.size() == 12);
    CHECK(lines[3] == "#15d");
    // Back closes the page.
    CHECK_FALSE(screen.command(MenuCommand::Back));
    CHECK(screen.menu.page() == ControlPage::None);
}

TEST_CASE("controls: the entries sit at (0.26, 0.29) and 0.06 below, size 1.3, the selected one in 0x005fd310",
          "[pause_menu][controls]") {
    Screen screen;
    CHECK(screen.menu.entry(0).style().x == Approx(0.26F));
    CHECK(screen.menu.entry(0).style().y == Approx(0.29F));
    CHECK(screen.menu.entry(1).style().y == Approx(0.35F));
    CHECK(screen.menu.entry(0).style().scale == Approx(1.3F));
    CHECK(screen.menu.entry(0).style().colour == coney::gui::kMenuGrey);
    CHECK(screen.menu.entry(1).style().colour == coney::gui::kDimGrey);
    (void)screen.command(MenuCommand::Down);
    CHECK(screen.menu.entry(0).style().colour == coney::gui::kDimGrey);
    CHECK(screen.menu.entry(1).style().colour == coney::gui::kMenuGrey);
}
