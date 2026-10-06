// SPDX-License-Identifier: GPL-3.0-or-later
// The pause menu, its Yes/No box and the mission-failed menu (docs/research/pause.md), driven frame by frame with a
// synthetic pad and synthetic strings.
#include "gui/pause_menu/pause_menu.h"

#include <cstddef>
#include <cstdint>
#include <format>
#include <string>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "core/pad.h"
#include "gui/global_strings.h"
#include "gui/pause_menu/mission_failed_menu.h"
#include "gui/pause_menu/pause_items.h"
#include "gui/pause_menu/yes_no_box.h"
#include "warriors/game_state.h"

using Catch::Approx;
using coney::Pad;
using coney::PadSample;
using coney::gui::FailedItem;
using coney::gui::GlobalStrings;
using coney::gui::GuiFrame;
using coney::gui::MenuCommand;
using coney::gui::MissionFailedMenu;
using coney::gui::PauseItem;
using coney::gui::PauseMenu;
using coney::gui::PauseMenuSetup;
using coney::gui::PauseOutcome;
using coney::gui::PauseScreen;
using coney::gui::YesNoAnswer;
using coney::gui::YesNoBox;
namespace s = coney::gui::pause_strings;

namespace {

// Every string the menus ask for, as "#<hex id>", so a test can tell which one is shown.
GlobalStrings syntheticStrings() {
    GlobalStrings strings;
    for (std::uint32_t id = 0; id < 0x200; ++id) {
        strings.set(id, std::format("#{:x}", id));
    }
    return strings;
}

// A connected sample holding `buttons`.
PadSample sample(std::uint16_t buttons) {
    PadSample s;
    s.connected = true;
    s.buttons = buttons;
    s.sticks = {coney::pad::kStickCentre, coney::pad::kStickCentre, coney::pad::kStickCentre, coney::pad::kStickCentre};
    return s;
}

// A menu with a pad, run one 1/30 s frame at a time from game time 10 s.
template <typename Menu> struct Driver {
    Menu& menu;

    // Drives `driven` from game time 10 s.
    explicit Driver(Menu& driven) : menu(driven) {}
    Pad pad;
    std::uint64_t nowMs = 10'000;
    std::uint64_t frame = 0;

    // One frame holding `buttons`.
    void step(std::uint16_t buttons = 0) {
        pad.update(sample(buttons));
        ++frame;
        nowMs = 10'000 + (frame * 100 / 3);
        menu.update(GuiFrame{nowMs, &pad});
    }
    // Idle frames, enough for the menus' command gap.
    void idle(int frames = 5) {
        for (int i = 0; i < frames; ++i) {
            step();
        }
    }
    // A press of `buttons` and its release (cross and triangle act on release), then a gap.
    void tap(std::uint16_t buttons) {
        step(buttons);
        idle();
    }
};

// Opens a pause menu in level `level` with the synthetic strings.
struct Paused {
    GlobalStrings strings = syntheticStrings();
    PauseMenu menu;
    std::vector<int> cues;
    Driver<PauseMenu> driver{menu};

    explicit Paused(int level, PauseMenuSetup setup = {}) {
        menu.setStrings(&strings);
        menu.setSoundSink([this](int cue) { cues.push_back(cue); });
        setup.level = level;
        menu.open(setup, driver.nowMs);
        driver.idle();
    }
    // Moves the grid to the item with `code` by pressing right.
    void selectItem(PauseItem code) {
        for (int i = 0; i < 10 && menu.grid().code(menu.grid().selected()) != static_cast<int>(code); ++i) {
            driver.tap(coney::pad::kRight);
        }
        REQUIRE(menu.grid().code(menu.grid().selected()) == static_cast<int>(code));
    }
};

} // namespace

TEST_CASE("pause items: the story grid is Objectives : Stats : Options / Controls : Restart : Resume : Quit",
          "[pause_menu]") {
    const coney::gui::PauseGridLayout story = coney::gui::pauseGrid(99);
    REQUIRE(story.items.size() == 7);
    CHECK(story.rows == std::vector<std::size_t>{3, 4});
    CHECK(story.items[0].text == s::kObjectives);
    CHECK(story.items[2].code == PauseItem::Options);
    CHECK_FALSE(story.items[2].separator); // the end of row 1
    CHECK(story.items[4].text == s::kRestart);
    CHECK_FALSE(story.items[6].separator);

    // The hangout has no Restart; a Rumble level has Rules and Replay and no Stats.
    const coney::gui::PauseGridLayout hangout = coney::gui::pauseGrid(95);
    CHECK(hangout.items.size() == 6);
    CHECK(hangout.rows == std::vector<std::size_t>{3, 3});
    const coney::gui::PauseGridLayout rumble = coney::gui::pauseGrid(102);
    REQUIRE(rumble.items.size() == 6);
    CHECK(rumble.items[0].text == s::kRules);
    CHECK(rumble.items[1].code == PauseItem::Options);
    CHECK(rumble.items[3].text == s::kReplay);
}

TEST_CASE("pause items: the hangout is offered in the 15 listed story levels only", "[pause_menu]") {
    CHECK(coney::gui::offersHangout(34));
    CHECK(coney::gui::offersHangout(11));
    CHECK_FALSE(coney::gui::offersHangout(99));
    CHECK_FALSE(coney::gui::offersHangout(95));
    CHECK(coney::gui::usesArmiesPauseMenu(60));
    CHECK_FALSE(coney::gui::usesArmiesPauseMenu(70));
    CHECK(coney::gui::isRumbleLevel(100));
}

TEST_CASE("pause menu: opening tints the world over 1.4 s and fades the cycling background in over 3 s",
          "[pause_menu]") {
    GlobalStrings strings = syntheticStrings();
    PauseMenu menu;
    menu.setStrings(&strings);
    menu.open(PauseMenuSetup{.level = 99}, 1000);
    CHECK(menu.worldTint(1000) == Approx(0.0F));
    CHECK(menu.worldTint(1700) == Approx(0.5F));
    CHECK(menu.worldTint(3000) == Approx(1.0F));

    // The first item selected, the title on the top header, the usage line.
    CHECK(menu.grid().selected() == 0);
    CHECK(menu.grid().items() == 7);
    CHECK(menu.headers()[0].text() == "#e7");
    CHECK(menu.alpha() == Approx(255.0F));

    menu.update(GuiFrame{1000 + 1500, nullptr});
    CHECK(menu.background().colour().a == 127);
    // At 5 s the cycle has reached its second colour, at 10 s its third.
    menu.update(GuiFrame{1000 + 5000, nullptr});
    CHECK(menu.background().colour().r == 50);
    CHECK(menu.background().colour().b == 150);
    CHECK(menu.background().colour().a == 255);
    menu.update(GuiFrame{1000 + 10000, nullptr});
    CHECK(menu.background().colour().g == 150);
}

TEST_CASE("pause menu: triangle resumes with a 1.5 s fade and a 0.5 s hold", "[pause_menu]") {
    Paused paused(99);
    paused.driver.tap(coney::pad::kTriangle);
    CHECK(paused.menu.closing());
    CHECK(paused.cues.back() == PauseMenu::kBackCue);
    CHECK_FALSE(paused.menu.closed());
    // 45 frames is 1.5 s: transparent, still held; 15 more is the hold.
    paused.driver.idle(45);
    CHECK(paused.menu.alpha() == Approx(0.0F));
    CHECK(paused.menu.grid().fade() == Approx(0.0F));
    CHECK_FALSE(paused.menu.closed());
    paused.driver.idle(16);
    CHECK(paused.menu.closed());
    CHECK(paused.menu.outcome() == PauseOutcome::Resume);
}

TEST_CASE("pause menu: START closes only once 1.5 s have passed since opening", "[pause_menu]") {
    Paused paused(99);
    paused.driver.tap(coney::pad::kStart);
    CHECK_FALSE(paused.menu.closing());
    paused.driver.idle(40);
    paused.driver.step(coney::pad::kStart);
    CHECK(paused.menu.closing());
}

TEST_CASE("pause menu: Resume closes, and the grid moves left and right through the rows", "[pause_menu]") {
    Paused paused(99);
    paused.selectItem(PauseItem::Resume);
    paused.driver.tap(coney::pad::kCross);
    CHECK(paused.menu.closing());
    CHECK(paused.menu.outcome() == PauseOutcome::Resume);
}

TEST_CASE("pause menu: Objectives shows the lists, None for an empty one, and up and down choose", "[pause_menu]") {
    PauseMenuSetup setup;
    setup.objectives[0] = {"Reach the park", "Find Cleon"};
    Paused paused(99, setup);
    paused.driver.tap(coney::pad::kCross);
    REQUIRE(paused.menu.screen() == PauseScreen::Objectives);
    CHECK(paused.menu.selection() == 0);
    CHECK(paused.menu.screenLines() == std::vector<std::string>{"Reach the park", "Find Cleon"});
    CHECK(paused.menu.usage().text() == "#19");
    paused.driver.tap(coney::pad::kDown);
    CHECK(paused.menu.selection() == 1);
    CHECK(paused.menu.screenLines() == std::vector<std::string>{"#109"});
    // Up from the first refuses; triangle goes back to the grid.
    paused.driver.tap(coney::pad::kUp);
    paused.driver.tap(coney::pad::kUp);
    CHECK(paused.cues.back() == PauseMenu::kRefusedCue);
    paused.driver.tap(coney::pad::kTriangle);
    CHECK(paused.menu.screen() == PauseScreen::Grid);
    CHECK_FALSE(paused.menu.closing());
}

TEST_CASE("pause menu: Stats opens nothing without stats; Options and Controls open their lists", "[pause_menu]") {
    Paused paused(99);
    paused.selectItem(PauseItem::Stats);
    paused.driver.tap(coney::pad::kCross);
    CHECK(paused.menu.screen() == PauseScreen::Grid);
    paused.selectItem(PauseItem::Options);
    paused.driver.tap(coney::pad::kCross);
    CHECK(paused.menu.screen() == PauseScreen::Options);
    CHECK(paused.menu.headers()[0].text() == "#116");
    CHECK(paused.menu.screenLines().size() == PauseMenu::kOptionsEntries.size());
    paused.driver.tap(coney::pad::kTriangle);
    paused.selectItem(PauseItem::Controls);
    paused.driver.tap(coney::pad::kCross);
    CHECK(paused.menu.screen() == PauseScreen::Controls);
    CHECK(paused.menu.headers()[0].text() == "#173");
}

TEST_CASE("pause menu: Restart Last Check Point is selected first and asks 0x103, No selected", "[pause_menu]") {
    Paused paused(99);
    paused.selectItem(PauseItem::Restart);
    paused.driver.tap(coney::pad::kCross);
    REQUIRE(paused.menu.screen() == PauseScreen::Restart);
    CHECK(paused.menu.selection() == 1);
    CHECK(paused.menu.screenLines() == std::vector<std::string>{"#112", "#114", "#115"});
    paused.driver.tap(coney::pad::kCross);
    REQUIRE(paused.menu.yesNo().isOpen());
    CHECK(paused.menu.yesNo().question().text() == "#103");
    CHECK_FALSE(paused.menu.yesNo().yesSelected());

    // No closes the box and keeps the screen; Yes closes the menu with the choice.
    paused.driver.tap(coney::pad::kCross);
    CHECK_FALSE(paused.menu.yesNo().isOpen());
    CHECK_FALSE(paused.menu.closing());
    CHECK(paused.menu.screen() == PauseScreen::Restart);
    paused.driver.tap(coney::pad::kUp);
    paused.driver.tap(coney::pad::kCross);
    CHECK(paused.menu.yesNo().question().text() == "#105");
    paused.driver.tap(coney::pad::kLeft);
    CHECK(paused.menu.yesNo().yesSelected());
    paused.driver.tap(coney::pad::kCross);
    CHECK(paused.menu.closing());
    CHECK(paused.menu.outcome() == PauseOutcome::RestartLevel);
}

TEST_CASE("pause menu: Quit asks at once in level99 and offers the hangout first where it is open", "[pause_menu]") {
    Paused story(99);
    story.selectItem(PauseItem::Quit);
    story.driver.tap(coney::pad::kCross);
    REQUIRE(story.menu.yesNo().isOpen());
    CHECK(story.menu.yesNo().question().text() == "#102");
    story.driver.tap(coney::pad::kRight);
    story.driver.tap(coney::pad::kCross);
    CHECK(story.menu.outcome() == PauseOutcome::QuitToMainMenu);

    Paused later(34);
    later.selectItem(PauseItem::Quit);
    later.driver.tap(coney::pad::kCross);
    REQUIRE(later.menu.screen() == PauseScreen::Quit);
    CHECK(later.menu.screenLines() == std::vector<std::string>{"#111", "#10f", "#10e"});
    later.driver.tap(coney::pad::kCross);
    later.driver.tap(coney::pad::kLeft);
    later.driver.tap(coney::pad::kCross);
    CHECK(later.menu.outcome() == PauseOutcome::QuitToHangout);
}

TEST_CASE("pause menu: in a Rumble level Replay asks 0x105 at once and Objectives starts on the overview",
          "[pause_menu]") {
    Paused rumble(102);
    rumble.driver.tap(coney::pad::kCross);
    CHECK(rumble.menu.screen() == PauseScreen::Objectives);
    CHECK(rumble.menu.selection() == 2);
    rumble.driver.tap(coney::pad::kTriangle);
    rumble.selectItem(PauseItem::Restart);
    rumble.driver.tap(coney::pad::kCross);
    REQUIRE(rumble.menu.yesNo().isOpen());
    CHECK(rumble.menu.yesNo().question().text() == "#105");
}

TEST_CASE("yes/no box: up and down are refused, left and right swap, back answers Back", "[pause_menu]") {
    YesNoBox box;
    std::vector<int> cues;
    box.setSoundSink([&cues](int cue) { cues.push_back(cue); });
    box.open("Quit?", "Yes", "No");
    CHECK_FALSE(box.yesSelected());
    CHECK_FALSE(box.handle(MenuCommand::Up).has_value());
    CHECK(cues.back() == YesNoBox::kRefusedCue);
    CHECK_FALSE(box.handle(MenuCommand::Right).has_value());
    CHECK(box.yesSelected());
    CHECK(box.handle(MenuCommand::Back) == YesNoAnswer::Back);
    CHECK_FALSE(box.isOpen());
}

TEST_CASE("mission failed: the reason, rows of 1 and 2, and the title by language", "[pause_menu]") {
    GlobalStrings strings = syntheticStrings();
    MissionFailedMenu menu;
    menu.setStrings(&strings);
    menu.open(99, "", 1000);
    CHECK(menu.reason().text() == "#da");
    CHECK(menu.reason().style().y == Approx(MissionFailedMenu::kReasonFirstY));
    REQUIRE(menu.grid().items() == 3);
    CHECK(menu.grid().rows() == 2);
    CHECK(menu.grid().code(0) == static_cast<int>(FailedItem::LastCheckpoint));
    CHECK(menu.usage().text() == "#1b");
    menu.update(GuiFrame{1033, nullptr});
    menu.update(GuiFrame{1066, nullptr});
    CHECK(menu.reason().style().y == Approx(MissionFailedMenu::kReasonY));

    CHECK(MissionFailedMenu::titleRecord(coney::Language::English) == 0x20b);
    CHECK(MissionFailedMenu::titleRecord(coney::Language::Spanish) == 0x20d);
    CHECK(MissionFailedMenu::titleRecord(coney::Language::French) == 0x20c);

    MissionFailedMenu hangout;
    hangout.setStrings(&strings);
    hangout.open(95, "Busted", 1000);
    CHECK(hangout.reason().text() == "Busted");
    CHECK(hangout.grid().rows() == 1);
    CHECK(hangout.grid().code(0) == static_cast<int>(FailedItem::ToHangout));
}

TEST_CASE("mission failed: Last checkpoint ends at once; Quit asks first", "[pause_menu]") {
    GlobalStrings strings = syntheticStrings();
    MissionFailedMenu menu;
    menu.setStrings(&strings);
    menu.open(99, "Busted", 10'000);
    Driver<MissionFailedMenu> driver{menu};
    driver.idle();
    driver.tap(coney::pad::kCross);
    CHECK(menu.outcome() == PauseOutcome::RestartCheckpoint);

    MissionFailedMenu quit;
    quit.setStrings(&strings);
    quit.open(99, "Busted", 10'000);
    Driver<MissionFailedMenu> quitDriver{quit};
    quitDriver.idle();
    quitDriver.tap(coney::pad::kDown);
    quitDriver.tap(coney::pad::kRight);
    REQUIRE(quit.grid().code(quit.grid().selected()) == static_cast<int>(FailedItem::Quit));
    quitDriver.tap(coney::pad::kCross);
    REQUIRE(quit.yesNo().isOpen());
    CHECK(quit.yesNo().question().text() == "#102");
    quitDriver.tap(coney::pad::kLeft);
    quitDriver.tap(coney::pad::kCross);
    CHECK(quit.outcome() == PauseOutcome::QuitToMainMenu);
}
