// SPDX-License-Identifier: GPL-3.0-or-later
// The pause menu, its Yes/No box and the mission-failed menu (docs/research/pause.md), driven frame by frame with a
// synthetic pad and synthetic strings.
// Each new case here failed first on the unit's stubbed source (coney-tools original fails-first).
#include "gui/pause_menu/pause_menu.h"

#include <array>
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
    // Expected: pause.md#the-items, confirmed (code)
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
    // Rules : Options : Controls / Replay : Resume : Quit: row 1 ends after Controls in a Rumble level.
    CHECK(rumble.items[1].separator);
    CHECK_FALSE(rumble.items[2].separator);
}

TEST_CASE("pause items: the hangout is offered in the 15 listed story levels only", "[pause_menu]") {
    // Expected: pause.md#the-items, confirmed (code)
    CHECK(coney::gui::offersHangout(34));
    CHECK(coney::gui::offersHangout(11));
    CHECK_FALSE(coney::gui::offersHangout(99));
    CHECK_FALSE(coney::gui::offersHangout(95));
    CHECK(coney::gui::usesArmiesPauseMenu(60));
    CHECK_FALSE(coney::gui::usesArmiesPauseMenu(70));
    CHECK(coney::gui::isRumbleLevel(100));
}

TEST_CASE("pause menu: opening tints the world over 1.4 s; the background is drawn at the menu's alpha",
          "[pause_menu]") {
    // Expected: pause.md#opening-the-menu, confirmed (code)
    GlobalStrings strings = syntheticStrings();
    PauseMenu menu;
    menu.setStrings(&strings);
    menu.open(PauseMenuSetup{.level = 99, .missionTitle = "1:Coney", .levelName = "New Blood", .checkpoint = 2}, 1000);
    CHECK(menu.worldTint(1000) == Approx(0.0F));
    CHECK(menu.worldTint(1700) == Approx(0.5F));
    CHECK(menu.worldTint(3000) == Approx(1.0F));

    // The first item selected, the mission's title on the top header, its label 0xe7 hidden.
    CHECK(menu.grid().selected() == 0);
    CHECK(menu.grid().items() == 7);
    CHECK(menu.topHeader().title().text() == "Coney");
    CHECK(menu.topHeader().subtitle().text() == "New Blood");
    CHECK(menu.topHeader().circles() == 2);
    CHECK(menu.topHeader().letter(1).text() == "B");
    CHECK(menu.topHeader().label().text() == "#e7");
    CHECK_FALSE(menu.topHeader().label().visible());
    CHECK_FALSE(menu.headers()[0].visible());
    CHECK(menu.alpha() == Approx(255.0F));
    CHECK(menu.usage().text() == "#1a");

    // The 3 s fade-in the update computes is replaced by the menu's alpha before every draw, so it never shows.
    menu.update(GuiFrame{1000 + 1500, nullptr});
    CHECK(menu.background().colour().a == 255);
}

TEST_CASE("pause menu: the background's colour steps on the first update past 5 s, each leg starting from 0",
          "[pause_menu]") {
    // Expected: pause.md#opening-the-menu, confirmed (code)
    PauseMenu menu;
    menu.open(PauseMenuSetup{.level = 99}, 1000);
    const auto colour = [&menu](std::uint64_t now) {
        menu.update(GuiFrame{now, nullptr});
        const coney::graphics::Rgba c = menu.background().colour();
        return std::array<int, 3>{c.r, c.g, c.b};
    };
    // Red to green over the first leg: at 5 s exactly it is still the first leg, at its end.
    CHECK(colour(1000 + 2500) == std::array<int, 3>{100, 100, 50});
    CHECK(colour(1000 + 5000) == std::array<int, 3>{50, 150, 50});
    // The first update past 5 s moves on to green and restarts the leg's clock there.
    CHECK(colour(1000 + 5001) == std::array<int, 3>{50, 150, 50});
    CHECK(colour(1000 + 7501) == std::array<int, 3>{50, 100, 100});
    CHECK(colour(1000 + 10001) == std::array<int, 3>{50, 50, 150});
    // Without updates between, a late update steps only once, and its leg starts at 0.
    CHECK(colour(1000 + 30000) == std::array<int, 3>{50, 50, 150});
    CHECK(colour(1000 + 32500) == std::array<int, 3>{100, 50, 100});
}

TEST_CASE("pause menu: the fade out truncates the alpha, zeroes it a frame late, and holds from the last fading frame",
          "[pause_menu]") {
    // Expected: pause.md#leaving, confirmed (code)
    PauseMenu menu;
    menu.open(PauseMenuSetup{.level = 99}, 1000);
    Pad pad;
    const auto frame = [&](std::uint64_t now, std::uint16_t buttons = 0) {
        pad.update(sample(buttons));
        menu.update(GuiFrame{now, &pad});
    };
    // START at exactly 1.5 s after opening is too early; a millisecond later it closes.
    frame(2500, coney::pad::kStart);
    CHECK_FALSE(menu.closing());
    frame(2501);
    frame(2502, coney::pad::kStart);
    REQUIRE(menu.closing());
    frame(2502 + 750);
    CHECK(menu.alpha() == 127.0F);
    // The last frame inside the fade still has some alpha; the next one only zeroes it.
    frame(2502 + 1350);
    CHECK(menu.alpha() == 25.0F);
    frame(2502 + 1600);
    CHECK(menu.alpha() == 0.0F);
    CHECK_FALSE(menu.closed());
    // The hold counts from the last frame inside the fade, and must be passed.
    frame(2502 + 1350 + 500);
    CHECK_FALSE(menu.closed());
    frame(2502 + 1350 + 501);
    CHECK(menu.closed());
    CHECK_FALSE(menu.closing());
}

TEST_CASE("pause menu: triangle resumes with a 1.5 s fade and a 0.5 s hold", "[pause_menu]") {
    // Expected: pause.md#leaving, confirmed (code)
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
    // Expected: pause.md#leaving, confirmed (code)
    Paused paused(99);
    paused.driver.tap(coney::pad::kStart);
    CHECK_FALSE(paused.menu.closing());
    paused.driver.idle(40);
    paused.driver.step(coney::pad::kStart);
    CHECK(paused.menu.closing());
}

TEST_CASE("pause menu: Resume closes, and the grid moves left and right through the rows", "[pause_menu]") {
    // Expected: pause.md#input, confirmed (code)
    Paused paused(99);
    paused.selectItem(PauseItem::Resume);
    paused.driver.tap(coney::pad::kCross);
    CHECK(paused.menu.closing());
    CHECK(paused.menu.outcome() == PauseOutcome::Resume);
}

TEST_CASE("pause menu: Objectives shows the lists, None for an empty one, and up and down choose", "[pause_menu]") {
    // Expected: pause.md#the-items-screens, confirmed (code)
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
    // The overview gets no None: only the current and bonus lists are given one.
    paused.driver.tap(coney::pad::kDown);
    CHECK(paused.menu.selection() == 2);
    CHECK(paused.menu.screenLines().empty());
    paused.driver.tap(coney::pad::kDown);
    CHECK(paused.menu.selection() == 2);
    CHECK(paused.cues.back() == PauseMenu::kRefusedCue);
    // Left and right are refused while a screen is open.
    paused.cues.clear();
    paused.driver.tap(coney::pad::kLeft);
    CHECK(paused.cues == std::vector<int>{PauseMenu::kRefusedCue});
    // Up from the first refuses; triangle goes back to the grid and its usage line.
    paused.driver.tap(coney::pad::kUp);
    paused.driver.tap(coney::pad::kUp);
    paused.driver.tap(coney::pad::kUp);
    CHECK(paused.cues.back() == PauseMenu::kRefusedCue);
    paused.driver.tap(coney::pad::kTriangle);
    CHECK(paused.menu.screen() == PauseScreen::Grid);
    CHECK(paused.menu.usage().text() == "#1a");
    CHECK_FALSE(paused.menu.closing());
}

TEST_CASE("pause menu: the header's circles take the level's colour, the last one red", "[pause_menu]") {
    // Expected: pause.md#the-top-header, confirmed (code)
    CHECK(coney::gui::pauseLevelColour(99) == coney::graphics::Rgba{186, 139, 53, 255});
    CHECK(coney::gui::pauseLevelColour(92) == coney::graphics::Rgba{37, 100, 60, 255});
    CHECK(coney::gui::pauseLevelColour(3) == coney::graphics::Rgba{38, 61, 94, 255});
    // 80 and 83 are in the gold list too, but the brown one is tested first.
    CHECK(coney::gui::pauseLevelColour(80) == coney::graphics::Rgba{76, 53, 44, 255});
    CHECK(coney::gui::pauseLevelColour(95) == coney::graphics::kBlack);
    // The lists are as long as their levels: nothing pads them.
    CHECK(coney::gui::pauseLevelColour(-1) == coney::graphics::kBlack);
    CHECK(coney::gui::pauseLevelColour(0) == coney::graphics::kBlack);

    PauseMenuSetup setup;
    setup.missionTitle = "1:Coney";
    Paused paused(99, setup);
    // The circles are coloured when the header is drawn.
    paused.menu.render(coney::gui::GuiCanvas{});
    CHECK(paused.menu.topHeader().circle(0).colour().r == 186);
    CHECK(paused.menu.topHeader().circle(1).colour().r == 170);
}

TEST_CASE("pause menu: Stats opens the Warrior's screen, and triangle closes it", "[pause_menu]") {
    // Expected: pause.md#stats-screen, confirmed (code)
    PauseMenuSetup setup;
    setup.stats = coney::gui::WarriorStats{.characterType = 30,
                                           .damageScale = 150,
                                           .powerMax = 400,
                                           .staminaMax = 200,
                                           .rageMax = 78,
                                           .powerClassByte40 = 35};
    Paused paused(99, setup);
    paused.selectItem(PauseItem::Stats);
    paused.driver.tap(coney::pad::kCross);
    REQUIRE(paused.menu.screen() == PauseScreen::Stats);
    CHECK(paused.menu.usage().text() == "#19");
    CHECK(paused.menu.screenLines() == std::vector<std::string>{"#f3", "#f4", "#f5", "#f6", "#f0"});
    paused.driver.tap(coney::pad::kDown);
    CHECK(paused.cues.back() == PauseMenu::kRefusedCue);
    paused.driver.tap(coney::pad::kTriangle);
    CHECK(paused.menu.screen() == PauseScreen::Grid);
    CHECK(paused.cues.back() == PauseMenu::kBackCue);
    CHECK_FALSE(paused.menu.closing());
}

TEST_CASE("pause menu: Stats opens nothing without stats; Options opens its list, Controls its screen",
          "[pause_menu]") {
    // Expected: pause.md#the-items-screens, confirmed (code)
    Paused paused(99);
    paused.selectItem(PauseItem::Stats);
    paused.driver.tap(coney::pad::kCross);
    CHECK(paused.menu.screen() == PauseScreen::Grid);
    // The usage line is still set to the Stats screen's, and the grid keeps the commands.
    CHECK(paused.menu.usage().text() == "#19");
    paused.selectItem(PauseItem::Options);
    paused.driver.tap(coney::pad::kCross);
    CHECK(paused.menu.screen() == PauseScreen::Options);
    CHECK(paused.menu.headers()[0].text() == "#116");
    CHECK(paused.menu.screenLines().size() == coney::gui::kOptionPages);
    paused.driver.tap(coney::pad::kTriangle);
    paused.selectItem(PauseItem::Controls);
    paused.driver.tap(coney::pad::kCross);
    CHECK(paused.menu.screen() == PauseScreen::Controls);
    CHECK(paused.menu.headers()[0].text() == "#173");
    // The Controls screen's entries; triangle leaves it for the grid.
    CHECK(paused.menu.screenLines() == std::vector<std::string>{"#130", "#131"});
    paused.driver.tap(coney::pad::kTriangle);
    CHECK(paused.menu.screen() == PauseScreen::Grid);
}

TEST_CASE("pause menu: Controls takes the d-pad and cross: down to Tutorial, cross opens it, triangle twice resumes "
          "the grid",
          "[pause_menu][controls]") {
    // Expected: pause.md#controls-layout, confirmed (code)
    Paused paused(99);
    paused.selectItem(PauseItem::Controls);
    paused.driver.tap(coney::pad::kCross);
    REQUIRE(paused.menu.screen() == PauseScreen::Controls);
    CHECK(paused.menu.usage().text() == "#1a");
    paused.driver.tap(coney::pad::kCross);
    REQUIRE(paused.menu.controls().page() == coney::gui::ControlPage::Controller);
    CHECK(paused.menu.usage().text() == "#1e");
    paused.driver.tap(coney::pad::kTriangle);
    paused.driver.tap(coney::pad::kDown);
    CHECK(paused.menu.controls().selected() == 1);
    paused.driver.tap(coney::pad::kCross);
    CHECK(paused.menu.controls().page() == coney::gui::ControlPage::Tutorial);
    CHECK(paused.menu.usage().text() == "#19");
    paused.driver.tap(coney::pad::kTriangle);
    CHECK(paused.menu.controls().page() == coney::gui::ControlPage::None);
    CHECK(paused.menu.usage().text() == "#1a");
    CHECK(paused.menu.screen() == PauseScreen::Controls);
    paused.driver.tap(coney::pad::kTriangle);
    CHECK(paused.menu.screen() == PauseScreen::Grid);
    CHECK_FALSE(paused.menu.closing());
}

TEST_CASE("pause menu: Restart Last Check Point is selected first and asks 0x103, No selected", "[pause_menu]") {
    // Expected: pause.md#the-items-screens, confirmed (code)
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
    // Expected: pause.md#the-items-screens, confirmed (code)
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
    // Expected: pause.md#the-items-screens, confirmed (code)
    Paused rumble(102);
    rumble.driver.tap(coney::pad::kCross);
    CHECK(rumble.menu.screen() == PauseScreen::Objectives);
    CHECK(rumble.menu.selection() == 2);
    // A Rumble level's objectives stay on the overview: up and down are refused.
    rumble.driver.tap(coney::pad::kUp);
    CHECK(rumble.menu.selection() == 2);
    CHECK(rumble.cues.back() == PauseMenu::kRefusedCue);
    rumble.driver.tap(coney::pad::kTriangle);
    rumble.selectItem(PauseItem::Restart);
    rumble.driver.tap(coney::pad::kCross);
    REQUIRE(rumble.menu.yesNo().isOpen());
    CHECK(rumble.menu.yesNo().question().text() == "#105");
}

TEST_CASE("yes/no box: up and down are refused, left and right swap, back answers Back", "[pause_menu]") {
    // Expected: pause.md#the-items-screens, confirmed (code)
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
    // Expected: pause.md#the-mission-failed-screen, confirmed (code)
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
    // Expected: pause.md#the-mission-failed-screen, confirmed (code)
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

TEST_CASE("pause menu: the Controls screen takes its layout from the language and video mode the menu opens with",
          "[pause_menu][controls]") {
    // Expected: pause.md#controls-layout, confirmed (code)
    PauseMenuSetup setup;
    setup.language = coney::Language::French;
    setup.widescreen = true;
    Paused paused(99, setup);
    paused.selectItem(PauseItem::Controls);
    paused.driver.tap(coney::pad::kCross);
    REQUIRE(paused.menu.screen() == PauseScreen::Controls);
    CHECK(paused.menu.controls().label(1).style().x == Catch::Approx(0.16F));
    CHECK(paused.menu.controls().tips().visibleCount() == 10);
}

TEST_CASE("pause menu: Options takes the pad: cross opens Camera, left turns Invert P1 on and the game is told, "
          "triangle twice returns to the grid",
          "[pause_menu][options]") {
    // Expected: pause.md#options, confirmed (code)
    PauseMenuSetup setup;
    setup.options.invertCamera = {false, false};
    std::vector<coney::gui::OptionValues> applied;
    Paused paused(99, setup);
    coney::gui::OptionMenu::Hooks hooks;
    hooks.apply = [&applied](const coney::gui::OptionValues& values) { applied.push_back(values); };
    paused.menu.setOptionHooks(hooks);
    paused.selectItem(PauseItem::Options);
    paused.driver.tap(coney::pad::kCross);
    REQUIRE(paused.menu.screen() == PauseScreen::Options);
    paused.driver.tap(coney::pad::kDown);
    CHECK(paused.menu.options().selectedTitle() == 1);
    paused.driver.tap(coney::pad::kCross);
    REQUIRE(paused.menu.options().pageOpen());
    paused.driver.tap(coney::pad::kLeft);
    REQUIRE(applied.size() == 1);
    CHECK(applied.back().invertCamera[0]);
    // Cross keeps it and closes the page; triangle leaves Options for the grid.
    paused.driver.tap(coney::pad::kCross);
    CHECK_FALSE(paused.menu.options().pageOpen());
    CHECK(paused.menu.options().values().invertCamera[0]);
    paused.driver.tap(coney::pad::kTriangle);
    CHECK(paused.menu.screen() == PauseScreen::Grid);
    CHECK_FALSE(paused.menu.closing());
}
