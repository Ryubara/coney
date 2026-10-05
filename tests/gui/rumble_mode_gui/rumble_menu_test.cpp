// SPDX-License-Identifier: GPL-3.0-or-later
// The Rumble menu's screens and the set-up each confirm writes (docs/research/frontend.md#rumble-setup), driven by
// hand-made pad samples, with a synthetic font so the screens' text is laid out too.
#include "gui/rumble_mode_gui/rumble_menu.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

#include <catch2/catch_test_macros.hpp>

#include "core/pad.h"
#include "graphics/font.h"
#include "graphics/sprite_batch.h"
#include "gui/widget.h"
#include "support/font_fixtures.h"
#include "warriors/game_state.h"

using coney::Pad;
using coney::PadSample;
using coney::RumbleSetup;
using coney::gui::RumbleMenu;
using coney::gui::RumbleMenuResult;
using coney::gui::RumbleScreen;

namespace {

// The default 1 ON 1 set-up read at run time on a fresh boot, the 23 values in order.
constexpr std::array<std::uint16_t, RumbleSetup::kValues> kDefaultValues{
    3, 12, 1, 4, 2, 91, 94, 91, 92, 93, 94, 91, 92, 93, 225, 226, 224, 225, 226, 227, 228, 225, 226};

// The left stick's y byte pushed `percent` of the way down (0 up, 255 down, 128 at rest).
std::uint8_t stickDown(int percent) {
    return static_cast<std::uint8_t>(coney::pad::kStickCentre + percent * 127 / 100);
}

// A Rumble menu over a synthetic font, run frame by frame with a hand-driven pad.
struct Harness {
    coney::graphics::Font font = coney::test::testFont();
    coney::graphics::SpriteBatch textBatch{font.sheet(), 4096, 9000.0F};
    coney::gui::GuiCanvas canvas;
    RumbleMenu menu;
    RumbleSetup setup;
    Pad pad;
    std::size_t pads = 1;
    std::uint64_t frame = 0;

    Harness() {
        canvas.fonts = [this](int /*slot*/) { return &font; };
        canvas.textBatch = [this](int /*slot*/) { return &textBatch; };
        menu.start(msOf(frame));
    }

    // The game time of frame `n` on the 1/30 s step, in milliseconds.
    static std::uint64_t msOf(std::uint64_t n) { return n * 100 / 3; }

    // Runs one frame with `buttons` held and the left stick's y byte at `stickY`; returns the menu's result.
    RumbleMenuResult step(std::uint16_t buttons = 0, std::uint8_t stickY = coney::pad::kStickCentre) {
        PadSample sample;
        sample.connected = true;
        sample.buttons = buttons;
        sample.sticks[3] = stickY;
        sample.pressure.fill(255);
        pad.update(sample);
        textBatch.clear();
        const RumbleMenuResult result =
            menu.update(coney::gui::GuiFrame{.timeMs = msOf(frame++), .pad = &pad}, pads, setup);
        menu.render(canvas);
        return result;
    }

    // Taps `button` (held one frame, let go the next), then waits past the gap between commands; returns what the
    // release gave.
    RumbleMenuResult tap(std::uint16_t button) {
        step(button);
        const RumbleMenuResult result = step();
        wait();
        return result;
    }

    // Pushes the left stick `percent` of the way down for a frame, then lets it go and waits.
    void stickDownOnce(int percent) {
        step(0, stickDown(percent));
        wait();
    }

    // Lets enough frames pass for the next command (more than 110 ms).
    void wait() {
        for (int i = 0; i < 4; ++i) {
            step();
        }
    }
};

} // namespace

TEST_CASE("the Rumble menu's defaults are the fresh boot's 1 ON 1 set-up in the Fight Pen", "[rumble_menu]") {
    const RumbleSetup setup = coney::gui::rumbleMenuDefaults();
    CHECK(setup.values == kDefaultValues);
    CHECK(setup.gangNames[0] == "BASEBALL FURIES");
    CHECK(setup.gangNames[1] == "ORPHANS");
    CHECK(setup.levelNumber == 102);
}

TEST_CASE("accepting each Rumble screen's first entry writes the default set-up, screen by screen", "[rumble_menu]") {
    Harness h;
    h.wait();
    CHECK(h.menu.screen() == RumbleScreen::GameMode);
    CHECK(h.menu.grid().items() == 2);
    CHECK(h.menu.grid().item(0).text() == "1 ON 1");
    CHECK(h.menu.grid().item(1).text() == "WAR PARTY");
    CHECK(!h.textBatch.sprites().empty());

    // Game Mode: the mode's number and the gang size, nothing else.
    CHECK(h.tap(coney::pad::kCross) == RumbleMenuResult::Stay);
    CHECK(h.menu.screen() == RumbleScreen::GameType);
    CHECK(h.setup.values.at(RumbleSetup::kGameType) == 12);
    CHECK(h.setup.values.at(RumbleSetup::kGangSize) == 1);
    CHECK(h.setup.values.at(RumbleSetup::kGameMode) == 0);
    CHECK(h.menu.grid().items() == 2);
    CHECK(h.menu.grid().item(0).text() == "1 Player : Vs.");

    // Game Type: the players.
    CHECK(h.tap(coney::pad::kCross) == RumbleMenuResult::Stay);
    CHECK(h.menu.screen() == RumbleScreen::ChooseGangs);
    CHECK(h.setup.values.at(RumbleSetup::kGameMode) == 3);
    // The gang names stay empty until the gangs are confirmed.
    CHECK(h.setup.gangNames[0].empty());
    CHECK(h.menu.grid().item(0).text() == "BASEBALL FURIES");

    // Choose Gangs: the packs, the nine types of each side and the names.
    CHECK(h.tap(coney::pad::kCross) == RumbleMenuResult::Stay);
    CHECK(h.menu.screen() == RumbleScreen::ChooseArea);
    CHECK(h.setup.values == kDefaultValues);
    CHECK(h.setup.gangNames[0] == "BASEBALL FURIES");
    CHECK(h.setup.gangNames[1] == "ORPHANS");
    CHECK(h.menu.grid().item(0).text() == "Fight Pen");

    // Choose Area: the level number, and the menu is done.
    CHECK(h.tap(coney::pad::kCross) == RumbleMenuResult::Started);
    CHECK(h.setup.levelNumber == 102);
}

TEST_CASE("WAR PARTY, chosen with the stick, is five a side and offers co-op only with a second pad", "[rumble_menu]") {
    Harness h;
    h.wait();
    h.stickDownOnce(70);
    CHECK(h.menu.grid().selected() == 1);
    CHECK(h.tap(coney::pad::kCross) == RumbleMenuResult::Stay);
    CHECK(h.setup.values.at(RumbleSetup::kGameType) == 14);
    CHECK(h.setup.values.at(RumbleSetup::kGangSize) == 5);
    REQUIRE(h.menu.grid().items() == 3);
    CHECK(h.menu.grid().item(1).text() == "COOP");

    // Co-op with one pad is not accepted; with two it is.
    h.stickDownOnce(65);
    CHECK(h.tap(coney::pad::kCross) == RumbleMenuResult::Stay);
    CHECK(h.menu.screen() == RumbleScreen::GameType);
    h.pads = 2;
    CHECK(h.tap(coney::pad::kCross) == RumbleMenuResult::Stay);
    CHECK(h.menu.screen() == RumbleScreen::ChooseGangs);
    CHECK(h.setup.values.at(RumbleSetup::kGameMode) == 1);
}

TEST_CASE("back returns to the Rumble screen before, and cancels from the first", "[rumble_menu]") {
    Harness h;
    h.wait();
    h.stickDownOnce(70);
    CHECK(h.tap(coney::pad::kCross) == RumbleMenuResult::Stay);
    CHECK(h.tap(coney::pad::kCross) == RumbleMenuResult::Stay);
    CHECK(h.menu.screen() == RumbleScreen::ChooseGangs);
    CHECK(h.tap(coney::pad::kTriangle) == RumbleMenuResult::Stay);
    CHECK(h.menu.screen() == RumbleScreen::GameType);
    CHECK(h.tap(coney::pad::kCircle) == RumbleMenuResult::Stay);
    CHECK(h.menu.screen() == RumbleScreen::GameMode);
    // The mode chosen before is still selected.
    CHECK(h.menu.grid().selected() == 1);
    CHECK(h.tap(coney::pad::kTriangle) == RumbleMenuResult::Cancelled);
}
