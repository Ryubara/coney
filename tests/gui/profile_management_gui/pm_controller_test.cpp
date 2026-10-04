// SPDX-License-Identifier: GPL-3.0-or-later
// The profile manager's screens and flow: PM_Greet, PM_Mode, the stand-ins and the transition table
// (docs/research/frontend.md#profile-manager), with a synthetic font and sheet and hand-made pad samples.
#include "gui/profile_management_gui/pm_controller.h"

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "core/pad.h"
#include "graphics/sprite_batch.h"
#include "gui/global_strings.h"
#include "gui/profile_management_gui/pm_greet.h"
#include "gui/profile_management_gui/pm_mode.h"
#include "support/font_fixtures.h"
#include "support/recording_device.h"

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
    std::unique_ptr<PmController> controller;
    Pad pad;
    std::uint64_t frame = 0;
    std::vector<int> cues;
    std::vector<std::pair<std::string, double>> scripts;

    explicit Harness(bool europe = false) {
        strings.set(PmGreet::kPromptString, "PRESS START");
        strings.set(PmMode::kStoryString, "STORY");
        strings.set(PmMode::kExtrasString, "EXTRAS");
        strings.set(PmMode::kQuickRumbleString, "RUMBLE");
        strings.set(0x1f, "<X> ok");
        shared.strings = &strings;
        shared.menuSprites = &menuBatch;
        shared.europe = europe;
        shared.canvas.fonts = [this](int /*slot*/) { return &font; };
        shared.canvas.textBatch = [this](int /*slot*/) { return &textBatch; };
        shared.playSound = [this](int cue) { cues.push_back(cue); };
        shared.callScript = [this](std::string_view function, double argument) {
            scripts.emplace_back(std::string(function), argument);
        };
        controller = std::make_unique<PmController>(shared);
    }

    // The game time of frame `n` on the 1/30 s step, in milliseconds.
    static std::uint64_t msOf(std::uint64_t n) { return n * 100 / 3; }

    // Runs one frame with `buttons` held; returns whether the flow is done. Batches are emptied after each frame.
    bool step(std::uint16_t buttons = 0) {
        PadSample sample;
        sample.connected = true;
        sample.buttons = buttons;
        sample.pressure.fill(255);
        pad.update(sample);
        shared.frame = coney::gui::GuiFrame{.timeMs = msOf(frame++), .pad = &pad};
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
    // The logo and its shadow; the prompt starts invisible but its sprites are still laid out.
    CHECK(h.menuBatch.sprites().size() == 2);
    CHECK(h.textBatch.sprites().size() == 2 * std::string("PRESSSTART").size());
}

TEST_CASE("PM_Greet: the prompt fades in and out in 1,500 ms halves", "[profile_manager]") {
    CHECK(PmGreet::promptAlpha(1000, 1000) == 0);
    CHECK(PmGreet::promptAlpha(1000, 1750) == 127);
    CHECK(PmGreet::promptAlpha(1000, 2500) == 255);
    CHECK(PmGreet::promptAlpha(1000, 3250) == 127);
    CHECK(PmGreet::promptAlpha(1000, 4000) == 0);
    CHECK(PmGreet::promptAlpha(1000, 4750) == 127);
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

TEST_CASE("PM_Greet: 70 s without input call Menu.playMovie(2), and input restarts the wait", "[profile_manager]") {
    Harness h;
    h.start();
    // 70,000 ms is frame 2,100; input on frame 1,000 pushes it to frame 3,100.
    for (int i = 0; i < 1000; ++i) {
        h.step();
    }
    h.step(coney::pad::kSquare);
    while (h.frame < 3099) {
        h.step();
    }
    CHECK(h.scripts.empty());
    h.step();
    h.step();
    REQUIRE(h.scripts.size() == 1);
    CHECK(h.scripts[0].first == "Menu.playMovie");
    CHECK(h.scripts[0].second == 2.0);
    CHECK(h.controller->currentName() == "PM_Greet");
}

TEST_CASE("PM_Mode: three items, the first selected; extras left out with the flag 0x02", "[profile_manager]") {
    Harness h;
    h.start();
    h.step(coney::pad::kStart);
    h.step();
    const auto& grid = h.controller->mode().grid();
    REQUIRE(grid.items() == 3);
    CHECK(grid.item(0).text() == "STORY");
    CHECK(grid.code(0) == PmMode::kStory);
    CHECK(grid.code(1) == PmMode::kExtras);
    CHECK(grid.code(2) == PmMode::kQuickRumble);
    CHECK(grid.selected() == 0);
    CHECK(h.controller->mode().usage().text() == "<X> ok");

    Harness europe(true);
    europe.start();
    europe.step(coney::pad::kStart);
    REQUIRE(europe.controller->mode().grid().items() == 2);
    CHECK(europe.controller->mode().grid().code(1) == PmMode::kQuickRumble);
}

TEST_CASE("PM_Mode: down and cross on extras lead to PM_Extras; back returns to PM_Mode", "[profile_manager]") {
    Harness h;
    h.start();
    h.step(coney::pad::kStart);
    h.wait();
    h.tap(coney::pad::kDown);
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

TEST_CASE("PM_Mode: story leads to PM_Profile, quick rumble stays, back leads to PM_Greet", "[profile_manager]") {
    Harness h;
    h.start();
    h.step(coney::pad::kStart);
    h.wait();
    h.tap(coney::pad::kCross);
    CHECK(h.controller->currentName() == "PM_Profile");
    h.wait();
    h.tap(coney::pad::kCircle);
    CHECK(h.controller->currentName() == "PM_Mode");

    // Up wraps to the last item, quick rumble, whose code has no transition.
    h.wait();
    h.tap(coney::pad::kUp);
    h.wait();
    h.tap(coney::pad::kCross);
    CHECK(h.controller->currentName() == "PM_Mode");

    h.wait();
    h.tap(coney::pad::kTriangle);
    CHECK(h.controller->currentName() == "PM_Greet");
    CHECK(h.controller->flow().size() == 1);
}

TEST_CASE("profile manager: every screen of the table exists; stop empties the flow", "[profile_manager]") {
    Harness h;
    h.start();
    h.step();
    h.controller->stop();
    CHECK(h.controller->current() == nullptr);
    CHECK(h.step()); // an empty flow is done
    CHECK(PmController::kPlaceholderNames.size() + 2 == 14);
}
