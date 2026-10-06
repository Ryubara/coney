// SPDX-License-Identifier: GPL-3.0-or-later
// The profile manager's screens and flow: PM_Greet, PM_Mode, the story screens, the stand-ins and the transition table
// (docs/research/frontend.md#profile-manager), with a synthetic font and sheet and hand-made pad samples.
#include "gui/profile_management_gui/pm_controller.h"

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "core/pad.h"
#include "graphics/screen_fade.h"
#include "graphics/sprite_batch.h"
#include "gui/colour_table.h"
#include "gui/global_strings.h"
#include "gui/profile_management_gui/pm_greet.h"
#include "gui/profile_management_gui/pm_mode.h"
#include "gui/profile_management_gui/pm_new_game_screens.h"
#include "gui/profile_management_gui/pm_profile_screens.h"
#include "gui/profile_management_gui/pm_widgets.h"
#include "support/font_fixtures.h"
#include "support/recording_device.h"
#include "warriors/game_state.h"
#include "warriors/profile_store.h"

using coney::Pad;
using coney::PadSample;
using coney::graphics::SpriteBatch;
using coney::graphics::SpriteSheet;
using coney::graphics::UvRect;
using coney::gui::PmController;
using coney::gui::PmGreet;
using coney::gui::PmMode;

namespace {

// A menu sheet with one rectangle (the logo) over a fake texture.
SpriteSheet menuSheet() {
    SpriteSheet sheet;
    sheet.page.rects = {UvRect{0.0F, 0.0F, 0.5F, 0.25F}};
    sheet.texture = std::make_shared<coney::test::FakeTexture>(512, 256);
    return sheet;
}

// A profile manager over synthetic strings, font and sheet, run frame by frame with a hand-driven pad.
struct Harness {
    coney::gui::GlobalStrings strings;
    coney::graphics::Font font = coney::test::testFont();
    SpriteBatch textBatch{font.sheet(), 4096, 9000.0F};
    SpriteBatch menuBatch{menuSheet(), 50, 8500.0F};
    coney::gui::PmShared shared;
    coney::graphics::ScreenFade fade;
    coney::GameState state;
    coney::SessionProfileStore profiles;
    std::unique_ptr<PmController> controller;
    Pad pad;
    Pad pad2;
    std::uint64_t frame = 0;
    std::vector<int> cues;
    std::vector<std::pair<std::string, std::vector<double>>> scripts;

    explicit Harness(bool europe = false) {
        strings.set(PmGreet::kPromptString, "PRESS START");
        strings.set(PmMode::kStoryString, "STORY");
        strings.set(PmMode::kExtrasString, "EXTRAS");
        strings.set(PmMode::kQuickRumbleString, "RUMBLE");
        strings.set(0x1f, "<X> ok");
        // The story screens' texts, and a keyboard of 45 characters with two blank cells in its last row.
        for (std::uint32_t id = 0x77; id <= 0x9a; ++id) {
            if (strings.get(id).empty()) {
                strings.set(id, "T" + std::to_string(id));
            }
        }
        strings.set(0x97, "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-.  !?#&");
        strings.set(0x99, "OK");
        strings.set(0x9a, "DEL");
        shared.state = &state;
        shared.profiles = &profiles;
        shared.secondPad = &pad2;
        shared.strings = &strings;
        shared.menuSprites = &menuBatch;
        shared.video.flag02 = europe;
        shared.frontSprites = &menuBatch;
        shared.canvas.fonts = [this](int /*slot*/) { return &font; };
        shared.canvas.textBatch = [this](int /*slot*/) { return &textBatch; };
        shared.playSound = [this](int cue) { cues.push_back(cue); };
        shared.callScript = [this](std::string_view function, std::span<const double> args) {
            scripts.emplace_back(std::string(function), std::vector<double>(args.begin(), args.end()));
        };
        shared.fade = &fade;
        controller = std::make_unique<PmController>(shared);
    }

    // The game time of frame `n` on the 1/30 s step, in milliseconds.
    static std::uint64_t msOf(std::uint64_t n) { return n * 100 / 3; }

    // Runs one frame with `buttons` held; returns whether the flow is done. Batches are emptied after each frame.
    bool step(std::uint16_t buttons = 0, std::uint16_t buttons2 = 0) {
        PadSample sample;
        sample.connected = true;
        sample.buttons = buttons;
        sample.pressure.fill(255);
        pad.update(sample);
        sample.buttons = buttons2;
        pad2.update(sample);
        shared.frame = coney::gui::GuiFrame{.timeMs = msOf(frame++), .pad = &pad};
        fade.update(shared.frame.timeMs);
        textBatch.clear();
        menuBatch.clear();
        return controller->update();
    }

    // Starts the controller on the current frame's time.
    void start() {
        shared.frame = coney::gui::GuiFrame{.timeMs = msOf(frame), .pad = &pad};
        controller->start("Menu.fadeToRMI");
    }

    // Taps `button`: held one frame, let go the next.
    void tap(std::uint16_t button) {
        step(button);
        step();
    }

    // Lets enough frames pass for the next command (more than 110 ms).
    void wait() {
        for (int i = 0; i < 4; ++i) {
            step();
        }
    }
};

} // namespace

TEST_CASE("profile manager: starts at PM_Greet with the logo and the prompt", "[profile_manager]") {
    Harness h;
    h.start();
    CHECK(h.controller->currentName() == "PM_Greet");
    CHECK(h.controller->onRumble() == "Menu.fadeToRMI");
    CHECK_FALSE(h.step());
    CHECK(h.controller->greet().prompt().text() == "PRESS START");
    // The logo alone (no shadow); the prompt starts invisible but its sprites are still laid out.
    REQUIRE(h.menuBatch.sprites().size() == 1);
    CHECK(h.menuBatch.sprites()[0].colour == coney::gui::kMenuRed);
    CHECK(h.textBatch.sprites().size() == 2 * std::string("PRESSSTART").size());
}

TEST_CASE("PM_Greet: the logo's left edge at x 0, centred on y 0.2, 0.33 overlay units high, red; the prompt at "
          "(0, 0.81)",
          "[profile_manager]") {
    Harness h;
    h.start();
    h.step();
    const coney::gui::BaseWidget& logo = h.controller->greet().logo();
    const auto [width, height] = logo.overlaySize();
    CHECK(height == Catch::Approx(0.33F));
    // The sheet's rectangle is 256 x 64 texels: four times as wide as high.
    CHECK(width == Catch::Approx(4.0F * 0.33F));
    const auto [x, y] = logo.centre();
    CHECK(x == Catch::Approx(width * 448.0F / 640.0F / 2.0F));
    CHECK(y == Catch::Approx(0.2F));
    const coney::gui::TextStyle& prompt = h.controller->greet().prompt().style();
    CHECK(prompt.x == Catch::Approx(0.0F));
    CHECK(prompt.y == Catch::Approx(0.81F));
    CHECK(prompt.scale == Catch::Approx(1.15F));
    CHECK(prompt.colour == coney::gui::kMenuRed);
    CHECK(prompt.fontSlot == coney::gui::kBigFontSlot);
}

TEST_CASE("PM_Greet: the prompt ramps up then down over 1,500 ms each, flipping phase", "[profile_manager]") {
    CHECK(PmGreet::blinkAlpha(true, 0) == 0);
    CHECK(PmGreet::blinkAlpha(true, 750) == 127);
    CHECK(PmGreet::blinkAlpha(true, 1500) == 255);
    CHECK(PmGreet::blinkAlpha(false, 0) == 255);
    CHECK(PmGreet::blinkAlpha(false, 750) == 128);
    CHECK(PmGreet::blinkAlpha(false, 1500) == 0);
    // On the screen: dark at entry, lit after 1.5 s, dark again after 3 s.
    Harness h;
    h.start();
    h.step();
    CHECK(h.controller->greet().prompt().style().fade < 0.05F);
    while (h.frame < 45) {
        h.step();
    }
    CHECK(h.controller->greet().prompt().style().fade > 0.95F);
    while (h.frame < 90) {
        h.step();
    }
    CHECK(h.controller->greet().prompt().style().fade < 0.05F);
}

TEST_CASE("PM_Greet: START leads to PM_Mode with front-end sound cue 9", "[profile_manager]") {
    Harness h;
    h.start();
    for (int i = 0; i < 10; ++i) {
        h.step();
    }
    CHECK(h.controller->currentName() == "PM_Greet");
    h.step(coney::pad::kStart);
    CHECK(h.controller->currentName() == "PM_Mode");
    CHECK(h.cues == std::vector<int>{PmGreet::kStartCue});
    // Cross does nothing on PM_Greet.
    Harness g;
    g.start();
    g.tap(coney::pad::kCross);
    CHECK(g.controller->currentName() == "PM_Greet");
}

TEST_CASE("PM_Greet: 70 s without a fade call Menu.playMovie(2); the pad does not restart the wait",
          "[profile_manager]") {
    Harness h;
    h.start();
    // 70,000 ms is frame 2,100 (the screen entered on frame 0); a button held on frame 1,000 changes nothing.
    for (int i = 0; i < 1000; ++i) {
        h.step();
    }
    h.step(coney::pad::kSquare);
    while (h.frame < 2100) {
        h.step();
    }
    CHECK(h.scripts.empty());
    h.step();
    REQUIRE(h.scripts.size() == 1);
    CHECK(h.scripts[0].first == "Menu.playMovie");
    CHECK(h.scripts[0].second == std::vector<double>{2.0});
    CHECK(h.controller->currentName() == "PM_Greet");
}

TEST_CASE("PM_Greet: a screen fade keeps the prompt lit and restarts the idle wait", "[profile_manager]") {
    Harness h;
    h.start();
    h.step();
    // A 1.5 s fade in queued on frame 1: the prompt stays fully lit while it runs, then blinks again.
    h.fade.queue(coney::graphics::ScreenFade::kFadeIn, 1.5, Harness::msOf(h.frame));
    for (int i = 0; i < 20; ++i) {
        h.step();
        CHECK(h.controller->greet().prompt().style().fade == 1.0F);
    }
    while (h.fade.active()) {
        h.step();
    }
    const std::uint64_t clearFrame = h.frame;
    h.step();
    CHECK(h.controller->greet().prompt().style().fade < 1.0F);
    // The idle wait counts from the last frame of the fade: 70 s later, not 70 s after the screen's entry.
    while (h.frame < 2101) {
        h.step();
    }
    CHECK(h.scripts.empty());
    while (h.frame < clearFrame + 2101) {
        h.step();
    }
    CHECK(h.scripts.size() == 1);
}

TEST_CASE("PM_Mode: three items, the first selected; extras left out with the flag 0x02", "[profile_manager]") {
    Harness h;
    h.start();
    h.step(coney::pad::kStart);
    h.step();
    const auto& grid = h.controller->mode().grid();
    REQUIRE(grid.items() == 3);
    CHECK(grid.item(0).text == "STORY");
    CHECK(grid.code(0) == PmMode::kStory);
    CHECK(grid.code(1) == PmMode::kExtras);
    CHECK(grid.code(2) == PmMode::kQuickRumble);
    CHECK(grid.selected() == 0);
    CHECK(h.controller->mode().usage().text() == "<X> ok");

    // Rows {2, 1} at (0, 0.76): "STORY : EXTRAS" over "RUMBLE", red, the selection grey.
    CHECK(grid.rows() == 2);
    CHECK(grid.item(0).separator);
    CHECK_FALSE(grid.item(1).separator);
    const auto [storyX, storyY] = grid.itemPosition(0, h.shared.canvas);
    const auto [extrasX, extrasY] = grid.itemPosition(1, h.shared.canvas);
    const auto [rumbleX, rumbleY] = grid.itemPosition(2, h.shared.canvas);
    CHECK(storyX == Catch::Approx(0.0F));
    CHECK(storyY == Catch::Approx(0.76F));
    CHECK(extrasY == Catch::Approx(0.76F));
    CHECK(extrasX > storyX);
    CHECK(rumbleX == Catch::Approx(0.0F));
    CHECK(rumbleY == Catch::Approx(0.76F + 0.0505F).margin(0.0005F));
    CHECK(grid.itemColour(0) == coney::gui::kSelectedGrey);
    CHECK(grid.itemColour(1) == coney::gui::kMenuRed);
    CHECK(grid.item(0).scale == Catch::Approx(1.15F));
    CHECK(grid.item(2).scale == Catch::Approx(1.15F));
    // The usage line left-aligned at (0, 0.87).
    CHECK(h.controller->mode().usage().style().x == Catch::Approx(0.0F));
    CHECK(h.controller->mode().usage().style().y == Catch::Approx(0.87F));

    Harness europe(true);
    europe.start();
    europe.step(coney::pad::kStart);
    REQUIRE(europe.controller->mode().grid().items() == 2);
    CHECK(europe.controller->mode().grid().code(1) == PmMode::kQuickRumble);
    CHECK(europe.controller->mode().grid().rows() == 1);
    // The flag 0x02 also picks its own layout column: the one-row y is 0.734 there.
    CHECK(europe.controller->mode().grid().itemPosition(0, europe.shared.canvas).second == Catch::Approx(0.734F));
}

TEST_CASE("PM_Mode: left and right walk the three items with wrap; up and down keep the column", "[profile_manager]") {
    Harness h;
    h.start();
    h.step(coney::pad::kStart);
    const auto& grid = h.controller->mode().grid();
    const auto press = [&h](std::uint16_t button) {
        h.wait();
        h.tap(button);
    };
    press(coney::pad::kRight);
    CHECK(grid.selected() == 1);
    CHECK(h.cues.back() == coney::gui::pm::kMoveCue);
    press(coney::pad::kRight);
    CHECK(grid.selected() == 2);
    press(coney::pad::kRight);
    CHECK(grid.selected() == 0);
    press(coney::pad::kLeft);
    CHECK(grid.selected() == 2);
    // Up from QUICK RUMBLE: STORY; down from EXTRAS: QUICK RUMBLE (the column clamped); down wraps to the first row.
    press(coney::pad::kUp);
    CHECK(grid.selected() == 0);
    press(coney::pad::kRight);
    press(coney::pad::kDown);
    CHECK(grid.selected() == 2);
    press(coney::pad::kDown);
    CHECK(grid.selected() == 0);

    // With two items (the flag 0x02) nothing wraps: a refused move plays 0xe.
    Harness europe(true);
    europe.start();
    europe.step(coney::pad::kStart);
    europe.wait();
    europe.tap(coney::pad::kLeft);
    CHECK(europe.controller->mode().grid().selected() == 0);
    CHECK(europe.cues.back() == coney::gui::pm::kRefusedCue);
}

TEST_CASE("PM_Mode: right and cross on extras lead to PM_Extras; back returns to PM_Mode", "[profile_manager]") {
    Harness h;
    h.start();
    h.step(coney::pad::kStart);
    h.wait();
    h.tap(coney::pad::kRight);
    h.wait();
    CHECK(h.controller->mode().grid().selected() == 1);
    h.tap(coney::pad::kCross);
    CHECK(h.controller->currentName() == "PM_Extras");
    h.wait();
    h.tap(coney::pad::kTriangle);
    CHECK(h.controller->currentName() == "PM_Mode");
    // Re-entered: the selection starts at the first item again.
    CHECK(h.controller->mode().grid().selected() == 0);
}

TEST_CASE("PM_Mode: story leads to PM_Profile, quick rumble calls the first callback, back leads to PM_Greet",
          "[profile_manager]") {
    Harness h;
    h.start();
    h.step(coney::pad::kStart);
    h.wait();
    h.tap(coney::pad::kCross);
    CHECK(h.controller->currentName() == "PM_Profile");
    CHECK(h.cues == std::vector<int>{PmGreet::kStartCue, coney::gui::pm::kAcceptCue});
    h.wait();
    h.tap(coney::pad::kCircle);
    CHECK(h.controller->currentName() == "PM_Mode");

    // Up wraps to the last item, quick rumble: the profile manager's first Lua callback, and the menu stays.
    h.wait();
    h.tap(coney::pad::kUp);
    h.wait();
    h.tap(coney::pad::kCross);
    CHECK(h.controller->currentName() == "PM_Mode");
    REQUIRE(h.scripts.size() == 1);
    CHECK(h.scripts[0].first == "Menu.fadeToRMI");
    CHECK(h.scripts[0].second.empty());

    // Back: PM_Greet, with cue 0xf.
    h.wait();
    h.tap(coney::pad::kTriangle);
    CHECK(h.controller->currentName() == "PM_Greet");
    CHECK(h.controller->flow().size() == 1);
    CHECK(h.cues.back() == coney::gui::pm::kBackCue);
}

TEST_CASE("PM_Mode: input waits while the screen is faded; story with two pads asks for the players",
          "[profile_manager]") {
    Harness h;
    h.start();
    h.step(coney::pad::kStart);
    h.wait();
    // Faded out: cross does nothing.
    h.fade.queue(coney::graphics::ScreenFade::kFadeOut, 0.0, Harness::msOf(h.frame));
    h.tap(coney::pad::kCross);
    CHECK(h.controller->currentName() == "PM_Mode");
    // Faded back in: two pads connected make story lead to PM_NumPlayers.
    h.fade.queue(coney::graphics::ScreenFade::kFadeIn, 0.0, Harness::msOf(h.frame));
    h.shared.connectedPads = 2;
    h.wait();
    h.tap(coney::pad::kCross);
    CHECK(h.controller->currentName() == "PM_NumPlayers");
}

namespace {

// Drives `h` from PM_Greet through STORY to the screen after PM_Mode.
void toStory(Harness& h) {
    h.start();
    h.step(coney::pad::kStart);
    h.wait();
    h.tap(coney::pad::kCross);
    h.wait();
}

// Taps `button` and waits for the next command.
void press(Harness& h, std::uint16_t button) {
    h.tap(button);
    h.wait();
}

// The list screen on top of `h`'s flow; null when the top is not one.
const coney::gui::PmListScreen* listOnTop(const Harness& h) {
    return dynamic_cast<const coney::gui::PmListScreen*>(h.controller->current());
}

} // namespace

TEST_CASE("profile manager: a new profile goes PM_Profile, PM_Create, PM_Difficulty, PM_Light, PM_Subtitles",
          "[profile_manager]") {
    Harness h;
    toStory(h);
    REQUIRE(h.controller->currentName() == "PM_Profile");
    // No profile: create (selected) and reload, two rows.
    const coney::gui::PmListScreen* profile = listOnTop(h);
    REQUIRE(profile != nullptr);
    REQUIRE(profile->choices().items().size() == 2);
    CHECK(profile->choices().items()[0].code == coney::gui::PmProfile::kCreate);
    CHECK(profile->choices().items()[1].code == coney::gui::PmProfile::kReload);
    press(h, coney::pad::kCross);
    REQUIRE(h.controller->currentName() == "PM_Create");
    CHECK(h.shared.session.slot == 0U);

    // "AB", then right seven more times to cell 8 and up from it to OK.
    h.cues.clear();
    press(h, coney::pad::kCross);
    press(h, coney::pad::kRight);
    press(h, coney::pad::kCross);
    CHECK(h.shared.session.name == "AB");
    CHECK(h.cues == std::vector<int>{0xa, 7, 0xa});
    for (int i = 0; i < 7; ++i) {
        press(h, coney::pad::kRight);
    }
    CHECK(h.controller->create().keyboard().selected() == 8);
    press(h, coney::pad::kUp);
    CHECK(h.controller->create().keyboard().selected() == h.controller->create().keyboard().okIndex());
    press(h, coney::pad::kCross);
    CHECK(h.cues.back() == 0xb);
    REQUIRE(h.controller->currentName() == "PM_Difficulty");
    // PM_Difficulty: three items, the middle one selected; down to the last (a second down is refused), accept.
    const coney::gui::PmListScreen* difficulty = listOnTop(h);
    REQUIRE(difficulty != nullptr);
    CHECK(difficulty->choices().items().size() == 3);
    CHECK(difficulty->choices().selected() == 1);
    press(h, coney::pad::kDown);
    CHECK(h.cues.back() == 5);
    press(h, coney::pad::kDown);
    CHECK(h.cues.back() == 0xe);
    press(h, coney::pad::kCross);
    CHECK(h.state.profileDifficulty == 2.0);
    REQUIRE(h.controller->currentName() == "PM_Light");
    // PM_Light: 40, two steps up and one down.
    CHECK(h.controller->light().value() == 40);
    press(h, coney::pad::kRight);
    press(h, coney::pad::kRight);
    press(h, coney::pad::kLeft);
    CHECK(h.controller->light().value() == 45);
    CHECK(h.state.brightness == 45);
    CHECK(h.cues.back() == 6);
    press(h, coney::pad::kCross);
    CHECK(h.profiles.inUse());
    REQUIRE(h.controller->currentName() == "PM_Subtitles");
    // English: OFF selected. Accept ends the menus with a new game and a profile to create.
    const coney::gui::PmListScreen* subtitles = listOnTop(h);
    REQUIRE(subtitles != nullptr);
    CHECK(subtitles->choices().selected() == 1);
    h.step(coney::pad::kCross);
    CHECK(h.step());
    CHECK(h.shared.session.done);
    CHECK(h.shared.session.newGame);
    CHECK(h.shared.session.createOnExit);
    CHECK_FALSE(h.state.subtitles);
    CHECK(h.cues.back() == 9);
}

TEST_CASE("PM_Create: at most 8 characters, DEL removes one, a used name is refused, back empties the name",
          "[profile_manager]") {
    Harness h;
    h.profiles.create(3, coney::Profile{.name = "AAAAAAAA"});
    toStory(h);
    REQUIRE(h.controller->currentName() == "PM_Profile");
    // With one profile: use, create, delete, reload; create is the second.
    const coney::gui::PmListScreen* profile = listOnTop(h);
    REQUIRE(profile != nullptr);
    CHECK(profile->choices().items().size() == 4);
    press(h, coney::pad::kDown);
    press(h, coney::pad::kCross);
    REQUIRE(h.controller->currentName() == "PM_Create");
    CHECK(h.shared.session.slot == 0U);
    // Eight A's put the cursor on OK.
    for (int i = 0; i < 8; ++i) {
        press(h, coney::pad::kCross);
    }
    CHECK(h.shared.session.name == "AAAAAAAA");
    CHECK(h.controller->create().keyboard().selected() == h.controller->create().keyboard().okIndex());
    // OK on a name slot 3 already has: the message, and the screen stays.
    press(h, coney::pad::kCross);
    CHECK(h.controller->create().nameUsedShown());
    CHECK(h.controller->currentName() == "PM_Create");
    // DEL removes one.
    press(h, coney::pad::kRight);
    press(h, coney::pad::kCross);
    CHECK(h.shared.session.name == "AAAAAAA");
    CHECK(h.cues.back() == 0xc);
    // Back empties the name and returns to PM_Profile.
    press(h, coney::pad::kTriangle);
    CHECK(h.shared.session.name.empty());
    CHECK(h.controller->currentName() == "PM_Profile");
}

TEST_CASE("NameKeyboard: rows of 12, 12, 12 and 11, blank cells skipped, the jumps to OK", "[profile_manager]") {
    using coney::gui::MenuCommand;
    coney::gui::NameKeyboard keys;
    keys.set("ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-.  !?#&", "OK", "DEL");
    CHECK(keys.rows() == std::vector<std::size_t>{12, 12, 12, 11});
    CHECK(keys.okIndex() == 45);
    CHECK_FALSE(keys.move(MenuCommand::Left));
    CHECK_FALSE(keys.move(MenuCommand::Up));
    // Down three rows from column 3 meets a blank cell: the nearest selectable cell to its left.
    for (const MenuCommand command : {MenuCommand::Right, MenuCommand::Right, MenuCommand::Right, MenuCommand::Down,
                                      MenuCommand::Down, MenuCommand::Down}) {
        keys.move(command);
    }
    CHECK(keys.selected() == 38);
    // Right skips the two blanks.
    CHECK(keys.move(MenuCommand::Right));
    CHECK(keys.selected() == 41);
    // Down from cell 32 jumps to OK.
    keys.set("ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-.  !?#&", "OK", "DEL");
    for (int i = 0; i < 8; ++i) {
        keys.move(MenuCommand::Right);
    }
    keys.move(MenuCommand::Down);
    keys.move(MenuCommand::Down);
    CHECK(keys.selected() == 32);
    CHECK(keys.move(MenuCommand::Down));
    CHECK(keys.selected() == 45);
}

TEST_CASE("PmChoices: rows, clamped columns and refused moves", "[profile_manager]") {
    using coney::gui::MenuCommand;
    coney::gui::PmChoices choices;
    choices.set({{"a", 0}, {"b", 1}, {"c", 2}, {"d", 3}, {"e", 4}}, {2, 2, 1});
    CHECK_FALSE(choices.move(MenuCommand::Left));
    CHECK(choices.move(MenuCommand::Right));
    CHECK_FALSE(choices.move(MenuCommand::Right));
    CHECK(choices.move(MenuCommand::Down));
    CHECK(choices.selected() == 3);
    CHECK(choices.move(MenuCommand::Down));
    CHECK(choices.selected() == 4); // the column clamped to the short row
    CHECK_FALSE(choices.move(MenuCommand::Down));
    CHECK(choices.selectedCode() == 4);
}

TEST_CASE("PM_NumPlayers: two players wait for player 2's START", "[profile_manager]") {
    Harness h;
    h.shared.connectedPads = 2;
    toStory(h);
    REQUIRE(h.controller->currentName() == "PM_NumPlayers");
    const auto* screen = dynamic_cast<const coney::gui::PmNumPlayers*>(h.controller->current());
    REQUIRE(screen != nullptr);
    press(h, coney::pad::kRight);
    press(h, coney::pad::kCross);
    CHECK(screen->waitingForPlayerTwo());
    CHECK(screen->prompt().visible());
    CHECK(h.controller->currentName() == "PM_NumPlayers");
    // Back to one player hides the prompt; then two again, and player 2's START chooses.
    press(h, coney::pad::kLeft);
    CHECK_FALSE(screen->waitingForPlayerTwo());
    press(h, coney::pad::kRight);
    press(h, coney::pad::kCross);
    h.step(0, coney::pad::kStart);
    h.step();
    CHECK(h.state.twoPlayers);
    CHECK(h.controller->currentName() == "PM_Profile");
}

TEST_CASE("PM_Profile: use and delete go to PM_Load; PM_Load loads, or deletes through PM_Delete",
          "[profile_manager]") {
    Harness h;
    h.profiles.create(0, coney::Profile{.name = "ONE"});
    h.profiles.create(2, coney::Profile{.name = "TWO"});
    toStory(h);
    REQUIRE(h.controller->currentName() == "PM_Profile");
    // Delete (the third item): PM_Load in delete mode, then PM_Delete with NO selected.
    press(h, coney::pad::kDown);
    press(h, coney::pad::kDown);
    press(h, coney::pad::kCross);
    REQUIRE(h.controller->currentName() == "PM_Load");
    CHECK(h.shared.session.deleteMode);
    press(h, coney::pad::kRight);
    press(h, coney::pad::kCross);
    REQUIRE(h.controller->currentName() == "PM_Delete");
    CHECK(h.shared.session.slot == 2U);
    const coney::gui::PmListScreen* sure = listOnTop(h);
    REQUIRE(sure != nullptr);
    CHECK(sure->choices().selected() == 1);
    press(h, coney::pad::kLeft);
    press(h, coney::pad::kCross);
    CHECK(h.profiles.profile(2) == nullptr);
    REQUIRE(h.scripts.size() == 1);
    CHECK(h.scripts[0].first == "Menu.deleteProfile");
    REQUIRE(h.controller->currentName() == "PM_Profile");
    // Use existing: PM_Load lists ONE; accept loads it and ends the menus.
    press(h, coney::pad::kCross);
    REQUIRE(h.controller->currentName() == "PM_Load");
    CHECK_FALSE(h.shared.session.deleteMode);
    h.step(coney::pad::kCross);
    CHECK(h.step());
    CHECK(h.profiles.loaded() == 0U);
    CHECK(h.profiles.inUse());
    CHECK(h.shared.session.done);
    CHECK_FALSE(h.shared.session.createOnExit);
}

TEST_CASE("profile manager: every screen of the table exists; stop empties the flow", "[profile_manager]") {
    Harness h;
    h.start();
    h.step();
    h.controller->stop();
    CHECK(h.controller->current() == nullptr);
    CHECK(h.step()); // an empty flow is done
    CHECK(PmController::kPlaceholderNames.size() + 12 == 14);
}

TEST_CASE("PM_Extras: TRAILER at (0, 0.81); cross plays Menu.playMovie(1) and stays", "[profile_manager]") {
    Harness g;
    g.start();
    g.step(coney::pad::kStart);
    g.wait();
    g.tap(coney::pad::kRight);
    g.wait();
    g.tap(coney::pad::kCross);
    REQUIRE(g.controller->currentName() == "PM_Extras");
    const auto& grid = g.controller->extras().grid();
    REQUIRE(grid.items() == 1);
    CHECK(grid.itemPosition(0, g.shared.canvas).first == Catch::Approx(0.0F));
    CHECK(grid.itemPosition(0, g.shared.canvas).second == Catch::Approx(0.81F));
    g.wait();
    g.tap(coney::pad::kCross);
    CHECK(g.controller->currentName() == "PM_Extras");
    REQUIRE(g.scripts.size() == 1);
    CHECK(g.scripts[0].first == "Menu.playMovie");
    CHECK(g.scripts[0].second == std::vector<double>{1.0});
    CHECK(g.cues.back() == coney::gui::pm::kAcceptCue);
}
